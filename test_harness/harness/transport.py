"""
Transport layer abstraction.

Each concrete class wraps a different physical/logical link to the device CLI
but exposes the same interface: open(), close(), send_command().

                  ┌─────────────────────┐
                  │    CLITransport      │  (abstract base)
                  └──────────┬──────────┘
            ┌────────────────┼────────────────┐
            ▼                ▼                ▼
     SerialTransport   TelnetTransport   UsbSerialTransport
        (UART)          (TCP port 23)   (USB CDC-ACM)
"""

from __future__ import annotations
import abc
import time
import socket
import logging
from typing import Optional

log = logging.getLogger(__name__)


# ──────────────────────────────────────────────────────────────────────────────
# Abstract base
# ──────────────────────────────────────────────────────────────────────────────

class CLITransport(abc.ABC):
    """Common interface for all CLI transports."""

    @abc.abstractmethod
    def open(self) -> None:
        """Open/connect the transport.  Raises on failure."""

    @abc.abstractmethod
    def close(self) -> None:
        """Close/disconnect the transport gracefully."""

    @abc.abstractmethod
    def send_command(self, command: str,
                     timeout: Optional[float] = None) -> str:
        """
        Send *command* to the device CLI and return the response text.

        The implementation must:
        - Strip the echoed command (if echo is enabled in config).
        - Strip the trailing prompt line.
        - Return clean response text, possibly multi-line.
        """

    # Context-manager support so transports can be used with `with`
    def __enter__(self) -> "CLITransport":
        self.open()
        return self

    def __exit__(self, *_) -> None:
        self.close()


# ──────────────────────────────────────────────────────────────────────────────
# Serial (UART or USB CDC) transport
# ──────────────────────────────────────────────────────────────────────────────

class SerialTransport(CLITransport):
    """CLI over a physical UART or USB CDC serial port."""

    def __init__(self, config, port: Optional[str] = None,
                 baud: Optional[int] = None, echo: Optional[bool] = None):
        self.config = config
        self._port = port or config.serial_port
        self._baud = baud or config.serial_baud
        self._ser  = None
        # echo: explicit parameter wins; otherwise use the serial-specific
        # config field.  Serial/USB ports typically have echo OFF because the
        # terminal application provides local echo.
        self._echo = echo if echo is not None else config.serial_cli_echo

    def open(self) -> None:
        try:
            import serial  # type: ignore
        except ImportError as exc:
            raise RuntimeError("pyserial is required: pip install pyserial") from exc

        log.debug("Opening serial port %s @ %d", self._port, self._baud)
        self._ser = serial.Serial(
            port=self._port,
            baudrate=self._baud,
            timeout=self.config.serial_timeout,
        )
        time.sleep(0.15)          # Give the device a moment after DTR asserts
        self._flush_to_prompt()

    def close(self) -> None:
        if self._ser and self._ser.is_open:
            try:
                self._ser.close()
            except Exception:
                pass

    def send_command(self, command: str,
                     timeout: Optional[float] = None) -> str:
        timeout = timeout or self.config.cli_timeout
        self._ser.timeout = timeout
        self._ser.write((command + "\r\n").encode())
        return self._collect_response(command, timeout)

    # ── private helpers ───────────────────────────────────────────────────────

    def _flush_to_prompt(self) -> None:
        """Send a bare newline and discard everything until the prompt appears."""
        self._ser.write(b"\r\n")
        time.sleep(0.2)
        self._ser.reset_input_buffer()

    def _collect_response(self, sent_command: str, timeout: float) -> str:
        lines: list[str] = []
        deadline = time.monotonic() + timeout
        prompt_bare = self.config.cli_prompt.strip()

        while time.monotonic() < deadline:
            raw = self._ser.readline()
            if not raw:
                continue
            line = raw.decode(errors="replace").strip()
            if not line:
                continue
            # Drop the echoed command (only when echo is on for this transport)
            if self._echo and line == sent_command:
                continue
            # The prompt signals end of response
            if line == prompt_bare or line.endswith(prompt_bare):
                # Any text before the prompt on the same line is content
                pre = line[: -len(prompt_bare)].strip()
                if pre:
                    lines.append(pre)
                break
            lines.append(line)

        return "\n".join(lines)


# ──────────────────────────────────────────────────────────────────────────────
# USB CDC serial transport  (same implementation, different default port/baud)
# ──────────────────────────────────────────────────────────────────────────────

class UsbSerialTransport(SerialTransport):
    """CLI over the USB CDC-ACM serial port."""

    def __init__(self, config, port: Optional[str] = None,
                 baud: Optional[int] = None, echo: Optional[bool] = None):
        super().__init__(
            config,
            port=port or config.usb_serial_port,
            baud=baud or config.usb_serial_baud,
            # USB CDC also has no device echo; use the USB-specific config field.
            echo=echo if echo is not None else config.usb_serial_cli_echo,
        )


# ──────────────────────────────────────────────────────────────────────────────
# Telnet transport  (raw socket, handles IAC negotiation)
# ──────────────────────────────────────────────────────────────────────────────

class TelnetTransport(CLITransport):
    """CLI over Telnet (TCP port 23)."""

    IAC  = 255
    DONT = 254
    DO   = 253
    WONT = 252
    WILL = 251
    SB   = 250
    SE   = 240

    def __init__(self, config):
        self.config = config
        self._sock: Optional[socket.socket] = None
        self._buf  = b""
        # Telnet has no local echo on the host side, so the device echoes
        # typed characters back.  Read from the telnet-specific config field.
        self._echo = config.telnet_cli_echo

    def open(self) -> None:
        log.debug("Connecting to %s:%d (telnet)",
                  self.config.device_ip, self.config.telnet_port)
        self._sock = socket.create_connection(
            (self.config.device_ip, self.config.telnet_port),
            timeout=self.config.network_timeout,
        )
        self._sock.settimeout(self.config.cli_timeout)
        # Wait for the initial prompt
        self._read_until(self.config.cli_prompt.encode(),
                         timeout=self.config.cli_timeout)

    def close(self) -> None:
        if self._sock:
            try:
                self._sock.close()
            except Exception:
                pass
            self._sock = None

    def send_command(self, command: str,
                     timeout: Optional[float] = None) -> str:
        timeout = timeout or self.config.cli_timeout
        # Allow one silent reconnect.  The device's telnet server may close the
        # connection after each command-response cycle or on a short idle timer.
        # _flush_stale_data() will raise ConnectionResetError if it detects the
        # server has already closed the socket; we catch that here and reopen.
        for attempt in range(2):
            try:
                self._flush_stale_data()
                self._sock.sendall((command + "\r\n").encode())
                break   # send succeeded – exit the retry loop
            except (BrokenPipeError, ConnectionResetError, OSError) as exc:
                if attempt == 0:
                    log.warning(
                        "Telnet connection lost (%s); reconnecting and retrying...",
                        exc,
                    )
                    self.close()
                    self.open()
                else:
                    raise   # Second failure – let it propagate as a real error

        raw = self._read_until(self.config.cli_prompt.encode(), timeout=timeout)
        return self._parse_response(raw.decode(errors="replace"), command)

    def _flush_stale_data(self) -> None:
        """
        Drain any bytes already in the socket receive buffer before sending a
        new command.  Uses a very short timeout so it doesn't block.

        Raises ConnectionResetError if the server has closed the connection so
        that send_command() can reconnect before attempting to write.
        """
        self._buf = b""
        self._sock.settimeout(0.05)
        try:
            while True:
                chunk = self._sock.recv(4096)
                if not chunk:
                    # b"" means the server sent a graceful TCP FIN.
                    raise ConnectionResetError(
                        "Telnet server closed the connection"
                    )
                # Discard – just draining leftover data from a verbose response.
        except socket.timeout:
            pass   # Normal: no stale data, recv timed out immediately.
        # Any other OSError (e.g. ECONNRESET) propagates to send_command.
        finally:
            self._sock.settimeout(self.config.cli_timeout)

    # ── private helpers ───────────────────────────────────────────────────────

    def _read_until(self, terminator: bytes,
                    timeout: float = 3.0) -> bytes:
        """Accumulate bytes until *terminator* is seen, stripping IAC sequences."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                chunk = self._sock.recv(4096)
            except socket.timeout:
                break
            if not chunk:
                break
            self._buf += self._strip_iac(chunk)
            if terminator in self._buf:
                idx = self._buf.index(terminator) + len(terminator)
                result    = self._buf[:idx]
                self._buf = self._buf[idx:]
                return result
        # Timeout – return whatever we have
        result    = self._buf
        self._buf = b""
        return result

    def _strip_iac(self, data: bytes) -> bytes:
        """Remove Telnet IAC negotiation sequences from a byte string."""
        out = bytearray()
        i = 0
        while i < len(data):
            b = data[i]
            if b == self.IAC:
                i += 1
                if i >= len(data):
                    break
                cmd = data[i]
                i += 1
                if cmd == self.SB:
                    # Subnegotiation: skip until IAC SE
                    while i < len(data) - 1:
                        if data[i] == self.IAC and data[i + 1] == self.SE:
                            i += 2
                            break
                        i += 1
                elif cmd in (self.WILL, self.WONT, self.DO, self.DONT):
                    # Option byte follows – skip it
                    if i < len(data):
                        i += 1
                # else: bare IAC – ignore
            else:
                out.append(b)
                i += 1
        return bytes(out)

    def _parse_response(self, raw: str, sent_command: str) -> str:
        """Clean up a raw telnet response into plain lines."""
        prompt_bare = self.config.cli_prompt.strip()
        lines: list[str] = []
        for line in raw.splitlines():
            line = line.strip()
            if not line:
                continue
            if self._echo and line == sent_command:
                continue
            if line == prompt_bare or line.endswith(prompt_bare):
                pre = line[: -len(prompt_bare)].strip()
                if pre:
                    lines.append(pre)
                break
            lines.append(line)
        return "\n".join(lines)
