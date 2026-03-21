"""
test_serial_cli.py – CLI tests run over the physical UART serial port.

All tests in this file are automatically skipped when the serial port is
not detected.  Add new test methods to the class below as you build out
the device CLI.

How to add a test
─────────────────
  1. Add a method named test_<feature> to TestSerialCLI.
  2. Call self.cli.cmd_expect(), cmd_ok(), or cmd_extract_int() as needed.
  3. See harness/cli.py for the full set of helper methods.

Anatomy of a typical test:

    def test_version(self):
        # Send the 'version' command and assert the response contains 'v1.'
        self.cli.cmd_expect("version", "v1.")

    def test_uptime_is_positive(self):
        # Extract a number from the response with a regex
        seconds = self.cli.cmd_extract_int("uptime", r"(\\d+)\\s*s")
        assert seconds >= 0, "uptime should be non-negative"
"""

import pytest


pytestmark = pytest.mark.serial


@pytest.fixture(autouse=True)
def _require_serial(serial_cli):
    """Ensure the serial CLI fixture is active (triggers the skip if needed)."""
    pass


class TestSerialCLI:
    """CLI smoke tests over the UART serial port."""

    @pytest.fixture(autouse=True)
    def setup(self, serial_cli):
        self.cli = serial_cli

    # ── Liveness ──────────────────────────────────────────────────────────────

    def test_prompt_is_alive(self):
        """Device should respond to an empty command with its prompt."""
        alive = self.cli.ping()
        assert alive, "Device did not respond to a bare newline on serial"

    # ── Basic CLI commands ────────────────────────────────────────────────────
    # TODO: Replace 'help', 'version', 'status' with your actual CLI commands.

    def test_help_lists_commands(self):
        """
        'help' should return a non-empty list of available commands.
        We use cmd() rather than cmd_ok() here because the help output
        is a large listing that may contain words like 'error' or 'fail'
        as part of legitimate command names or descriptions.
        """
        resp = self.cli.cmd("help")
        assert len(resp) > 0, "'help' returned an empty response"
        # Each line of the help output should look like a command word
        # (non-empty, no leading error indicators).
        lines = [l.strip() for l in resp.splitlines() if l.strip()]
        assert len(lines) > 0, "'help' response had no non-blank lines"

    # ── Add real command tests here ───────────────────────────────────────────
    # Replace the examples below with commands that actually exist on your device.
    #
    # def test_<command>(self):
    #     self.cli.cmd_ok("<command>")
    #
    # def test_<command>_output(self):
    #     self.cli.cmd_expect("<command>", "<expected substring>")

    # ── State mutation example ────────────────────────────────────────────────

    def test_echo_command_round_trip(self):
        """
        If your CLI has an 'echo' command, the device should reflect the argument.
        TODO: remove or adapt this test to match your CLI.
        """
        pytest.skip("Replace with a real state-mutation test for your device")
        # Example:
        # resp = self.cli.cmd("echo hello")
        # assert "hello" in resp

    # ── Add more test methods below ───────────────────────────────────────────
    # def test_<feature>(self):
    #     ...
