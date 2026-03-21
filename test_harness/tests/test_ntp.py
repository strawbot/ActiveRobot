"""
test_ntp.py – NTP client tests.

These tests verify that the device's NTP client has synchronised and that
the device clock is reasonable.  They rely on a CLI command that reports
NTP/time status and an available network path (Telnet or serial).

Requires
────────
  • A CLI that reports time/NTP state (see TODO markers below).
  • A reachable network interface (tested via the telnet_cli fixture; swap
    for serial_cli if you prefer to query NTP state over serial).

What to customise
─────────────────
  • The CLI command that reports NTP sync state and system time.
  • The expected response strings/patterns.
  • ntp_max_offset_sec in config.yaml (default 10 seconds).
"""

import re
import time
import datetime
import pytest


pytestmark = [pytest.mark.ntp, pytest.mark.network]


class TestNTP:
    """NTP client verification tests."""

    @pytest.fixture(autouse=True)
    def setup(self, telnet_cli, cfg):
        """Uses the Telnet CLI to query NTP state; swap for serial_cli if needed."""
        self.cli = telnet_cli
        self.cfg = cfg

    # ── Sync state ────────────────────────────────────────────────────────────

    def test_ntp_is_synchronised(self):
        """
        The device should report that NTP is synchronised.
        TODO: Replace 'ntp' with your actual CLI command, and 'synced' with
        the expected keyword in your device's NTP status output.
        """
        pytest.skip(
            "Replace 'ntp' and 'synced' with your actual CLI command and keyword"
        )
        # Example:
        # self.cli.cmd_expect("ntp", "synced")

    def test_ntp_server_is_set(self):
        """
        The device should have an NTP server configured.
        TODO: adjust the command and expected pattern.
        """
        pytest.skip("Replace with your actual NTP server query command")
        # Example (if your CLI shows the NTP server address):
        # resp = self.cli.cmd("ntp status")
        # # Check for any IPv4/hostname pattern
        # assert re.search(r'\d+\.\d+\.\d+\.\d+|pool\.ntp\.org', resp), (
        #     f"No NTP server address found in: {resp!r}"
        # )

    # ── Clock accuracy ────────────────────────────────────────────────────────

    def test_device_time_is_reasonable(self):
        """
        The device clock should be within ntp_max_offset_sec of the host clock.

        TODO: Replace 'time' with your CLI command that prints the current
        timestamp, and adjust the regex pattern to match your output format.

        Example output formats:
          Unix timestamp:   "1710000000"
          ISO-8601:         "2024-03-10T12:00:00Z"
          Human-readable:   "Mon Mar 10 12:00:00 2024"
        """
        pytest.skip(
            "Replace 'time' and the regex below with your actual time command"
        )
        # -- Option A: device reports a Unix timestamp -----------------------
        # epoch = self.cli.cmd_extract_int("time", r"(\d{10})")
        # device_time = datetime.datetime.utcfromtimestamp(epoch)
        # host_time   = datetime.datetime.utcnow()
        # offset = abs((device_time - host_time).total_seconds())
        # assert offset <= self.cfg.ntp_max_offset_sec, (
        #     f"Device clock is {offset:.1f}s off host clock "
        #     f"(max allowed: {self.cfg.ntp_max_offset_sec}s)"
        # )
        #
        # -- Option B: device reports ISO-8601 --------------------------------
        # resp = self.cli.cmd("time")
        # m    = re.search(r'(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2})', resp)
        # assert m, f"No ISO timestamp found in: {resp!r}"
        # device_time = datetime.datetime.fromisoformat(m.group(1))
        # host_time   = datetime.datetime.utcnow()
        # offset = abs((device_time - host_time).total_seconds())
        # assert offset <= self.cfg.ntp_max_offset_sec

    def test_device_time_advances(self):
        """
        Reading the device clock twice with a 1-second delay should show
        the time advancing.  This catches a frozen-clock bug.

        TODO: adjust the CLI command and integer-extraction pattern.
        """
        pytest.skip("Replace 'time' and regex with your actual time command")
        # t1 = self.cli.cmd_extract_int("time", r"(\d{10})")
        # time.sleep(1.5)
        # t2 = self.cli.cmd_extract_int("time", r"(\d{10})")
        # assert t2 > t1, "Device clock did not advance between two readings"

    # ── Add more NTP tests below ──────────────────────────────────────────────
