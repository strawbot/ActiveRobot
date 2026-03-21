"""
Configuration for the test harness.

Edit config.yaml in the project root to override defaults.
"""

from __future__ import annotations
import os
from dataclasses import dataclass, field


@dataclass
class Config:
    # ── Serial port ────────────────────────────────────────────────────────────
    serial_port: str = ""           # Leave blank for auto-detect
    serial_baud: int = 115200
    serial_timeout: float = 2.0     # seconds per readline

    # ── USB CDC serial port (separate from main UART) ──────────────────────────
    usb_serial_port: str = ""       # Leave blank for auto-detect
    usb_serial_baud: int = 115200
    usb_serial_timeout: float = 2.0

    # ── Network (Ethernet and/or USB network adapter) ──────────────────────────
    device_ip: str = "192.168.1.1"
    telnet_port: int = 23
    http_port: int = 80
    network_timeout: float = 3.0    # TCP connect probe timeout

    # ── CLI behaviour ─────────────────────────────────────────────────────────
    cli_prompt: str = "> "          # The prompt the device shows after each response
    cli_timeout: float = 3.0        # How long to wait for a full response

    # Echo is set per-transport because the device only echoes on ports where
    # the host is not doing local echo itself:
    #   Serial UART  → device does NOT echo  (terminal app handles local echo)
    #   USB CDC      → device does NOT echo
    #   Telnet       → device DOES echo      (raw TCP, no local echo)
    #   HTTP/web     → device DOES echo      (web terminal has no local echo)
    serial_cli_echo: bool = False
    usb_serial_cli_echo: bool = False
    telnet_cli_echo: bool = True

    # ── NTP ───────────────────────────────────────────────────────────────────
    ntp_max_offset_sec: float = 10.0  # Acceptable time difference from host clock

    # ── HTTP ──────────────────────────────────────────────────────────────────
    http_root_path: str = "/"         # Path to GET for the smoke test
    http_expected_status: int = 200

    @classmethod
    def load(cls, path: str = "config.yaml") -> "Config":
        """
        Load configuration from a YAML file, falling back to defaults for any
        missing key.  The file is optional – if it doesn't exist the defaults
        are used unchanged.
        """
        if not os.path.exists(path):
            return cls()

        try:
            import yaml  # type: ignore
        except ImportError:
            print("[config] PyYAML not installed – using defaults.")
            return cls()

        with open(path) as fh:
            data = yaml.safe_load(fh) or {}

        valid = {k: v for k, v in data.items() if k in cls.__dataclass_fields__}
        return cls(**valid)
