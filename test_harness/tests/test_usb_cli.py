"""
test_usb_cli.py – CLI tests run over the USB CDC serial port.

Mirrors test_serial_cli.py but uses the USB transport.  When both serial
and USB are connected, running both test files verifies that all CLI paths
produce consistent results.

Because the CLI is the same firmware behind each transport, most tests here
can be identical to the serial versions.  Consider using the `any_cli`
parametric fixture (defined in conftest.py) if you want one test body to
run automatically on every available transport.
"""

import pytest


pytestmark = pytest.mark.usb_serial


@pytest.fixture(autouse=True)
def _require_usb(usb_cli):
    """Ensure the USB CLI fixture is active (triggers skip if not detected)."""
    pass


class TestUsbCLI:
    """CLI smoke tests over the USB CDC serial port."""

    @pytest.fixture(autouse=True)
    def setup(self, usb_cli):
        self.cli = usb_cli

    # ── Liveness ──────────────────────────────────────────────────────────────

    def test_prompt_is_alive(self):
        """Device should respond to an empty command with its prompt over USB."""
        assert self.cli.ping(), "Device did not respond on USB CDC serial"

    # ── Basic CLI commands ────────────────────────────────────────────────────

    def test_help_lists_commands(self):
        resp = self.cli.cmd("help")
        assert len(resp) > 0, "'help' returned an empty response over USB"
        lines = [l.strip() for l in resp.splitlines() if l.strip()]
        assert len(lines) > 0, "'help' response had no non-blank lines over USB"

    # ── USB-specific: verify TCP/IP stack accessible over USB ─────────────────

    def test_usb_network_cli_mentions_ip(self):
        """
        If 'ifconfig' or similar is available, check the USB network address.
        TODO: Replace 'ifconfig' and '192.168' with your actual command and
        expected IP prefix.
        """
        pytest.skip("Replace with your device's network-info CLI command")
        # Example:
        # resp = self.cli.cmd("ifconfig usb0")
        # assert "192.168" in resp, f"No IP address in USB ifconfig output: {resp}"

    # ── Add more USB-specific tests below ─────────────────────────────────────
