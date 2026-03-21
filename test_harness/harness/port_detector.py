"""
Port detection utilities.

Probes which physical and logical interfaces are available so the test
session can skip tests that cannot run on the current bench setup.
"""

from __future__ import annotations
import socket
import sys
import logging
from dataclasses import dataclass, field
from typing import Optional

log = logging.getLogger(__name__)


@dataclass
class PortStatus:
    """Outcome of a single interface probe."""
    serial: bool = False
    serial_port_name: str = ""

    usb_serial: bool = False
    usb_serial_port_name: str = ""

    telnet: bool = False
    http: bool = False

    @property
    def network(self) -> bool:
        """True when any network path is reachable."""
        return self.telnet or self.http


class PortDetector:
    """Probes all configured interfaces and returns a PortStatus."""

    # Keywords that identify a USB-to-serial or USB CDC port in pyserial's
    # device description / hardware ID strings.
    _USB_SERIAL_HINTS = ("usb", "uart", "serial", "ftdi", "ch340", "cp210",
                         "prolific", "silabs")
    _USB_CDC_HINTS    = ("cdc", "acm")

    def __init__(self, config):
        self.config = config

    # ──────────────────────────────────────────────────────────────────────────
    # Public API
    # ──────────────────────────────────────────────────────────────────────────

    def detect_all(self) -> PortStatus:
        status = PortStatus()

        # Serial ports
        serial_port  = self._find_serial(prefer_cdc=False)
        usb_cdc_port = self._find_serial(prefer_cdc=True,
                                         exclude=serial_port)

        if serial_port:
            status.serial = True
            status.serial_port_name = serial_port

        if usb_cdc_port and usb_cdc_port != serial_port:
            status.usb_serial = True
            status.usb_serial_port_name = usb_cdc_port

        # Network services
        ip = self.config.device_ip
        status.telnet = self._tcp_reachable(ip, self.config.telnet_port)
        status.http   = self._tcp_reachable(ip, self.config.http_port)

        self._log_status(status)
        return status

    # ──────────────────────────────────────────────────────────────────────────
    # Serial helpers
    # ──────────────────────────────────────────────────────────────────────────

    def _find_serial(self, prefer_cdc: bool,
                     exclude: Optional[str] = None) -> Optional[str]:
        """
        Return the best matching serial port name.

        If config has an explicit port name set, that is returned (unless it
        matches *exclude*).  Otherwise pyserial's port list is searched.
        """
        # Honour explicit configuration overrides
        explicit = (self.config.usb_serial_port if prefer_cdc
                    else self.config.serial_port)
        if explicit and explicit != exclude:
            return explicit if self._port_exists(explicit) else None

        try:
            import serial.tools.list_ports as lp  # type: ignore
        except ImportError:
            log.warning("pyserial not installed – cannot enumerate serial ports")
            return None

        ports = lp.comports()

        # First pass: look for the preferred type
        hints = self._USB_CDC_HINTS if prefer_cdc else self._USB_SERIAL_HINTS
        for p in ports:
            if p.device == exclude:
                continue
            combined = f"{p.description or ''} {p.hwid or ''}".lower()
            if any(h in combined for h in hints):
                return p.device

        # Second pass: any remaining port that isn't excluded
        if not prefer_cdc:
            for p in ports:
                if p.device != exclude:
                    return p.device

        return None

    @staticmethod
    def _port_exists(port: str) -> bool:
        """Quick check that a named serial port appears in the system list."""
        try:
            import serial.tools.list_ports as lp  # type: ignore
            return any(p.device == port for p in lp.comports())
        except ImportError:
            return True  # Can't check; assume it exists if explicitly configured.

    # ──────────────────────────────────────────────────────────────────────────
    # Network helpers
    # ──────────────────────────────────────────────────────────────────────────

    def _tcp_reachable(self, host: str, port: int) -> bool:
        """Return True when a TCP connection to host:port succeeds."""
        try:
            with socket.create_connection((host, port),
                                          timeout=self.config.network_timeout):
                return True
        except (socket.timeout, ConnectionRefusedError, OSError):
            return False

    # ──────────────────────────────────────────────────────────────────────────
    # Reporting
    # ──────────────────────────────────────────────────────────────────────────

    @staticmethod
    def _log_status(status: PortStatus) -> None:
        lines = [
            "",
            "=" * 62,
            "  PORT DETECTION",
            "=" * 62,
        ]

        def row(label: str, ok: bool, detail: str = "") -> str:
            icon = "✓" if ok else "✗"
            suffix = f"  ({detail})" if detail else ""
            return f"  {icon}  {label:<25}{suffix}"

        lines.append(row("Serial UART",
                         status.serial, status.serial_port_name))
        lines.append(row("USB CDC Serial",
                         status.usb_serial, status.usb_serial_port_name))
        lines.append(row("Telnet (TCP 23)",  status.telnet))
        lines.append(row("HTTP   (TCP 80)",  status.http))
        lines.append("=" * 62)
        print("\n".join(lines))
