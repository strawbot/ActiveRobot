"""
test_telnet_cli.py – CLI tests run over Telnet (TCP port 23).

The Telnet interface exercises the TCP/IP stack end-to-end as well as the
CLI itself.  Both Ethernet and USB-network connections are served by the
same telnet daemon, so you don't need separate test files for each physical
link – detecting the IP address is sufficient.

TIP: Use the `any_cli` parametric fixture from conftest.py when you want
identical assertions to run over serial, USB, AND telnet in a single test:

    def test_version_all_ports(any_cli):
        any_cli.cmd_expect("version", "v1.")
"""

import pytest


pytestmark = pytest.mark.telnet


@pytest.fixture(autouse=True)
def _require_telnet(telnet_cli):
    """Ensure the Telnet CLI fixture is active (triggers skip if not reachable)."""
    pass


class TestTelnetCLI:
    """CLI smoke tests over the Telnet interface."""

    @pytest.fixture(autouse=True)
    def setup(self, telnet_cli):
        self.cli = telnet_cli

    # ── Liveness ──────────────────────────────────────────────────────────────

    def test_prompt_is_alive(self):
        """Device should respond to an empty command with its prompt over Telnet."""
        assert self.cli.ping(), "Device did not respond on Telnet"

    # ── Basic CLI commands ────────────────────────────────────────────────────

    def test_help_lists_commands(self):
        """
        'help' should return a non-empty command listing.
        Uses cmd() instead of cmd_ok() – the listing is verbose and may
        contain words that would trigger false error-detection.
        """
        resp = self.cli.cmd("help")
        assert len(resp) > 0, "'help' returned an empty response over Telnet"
        lines = [l.strip() for l in resp.splitlines() if l.strip()]
        assert len(lines) > 0, "'help' response had no non-blank lines"

    # ── Network-specific ──────────────────────────────────────────────────────

    def test_network_interface_info(self):
        """
        Query network interface state from the CLI.
        TODO: Replace 'net' with your device's network-status command and
        adjust the expected string.
        """
        pytest.skip("Replace with your device's network info command")
        # Example:
        # resp = self.cli.cmd_expect("net", cfg.device_ip)

    def test_multiple_commands_in_session(self):
        """
        The Telnet connection should remain stable across multiple commands.
        Specifically tests that a verbose response (help) followed by further
        commands doesn't break the session due to buffering issues.
        """
        # First: a verbose command that produces many lines of output.
        resp = self.cli.cmd("help")
        assert resp, "Telnet session lost after 'help'"

        # Second: a second help to confirm the buffer was flushed cleanly
        # and the session is still usable.  Replace with real commands once
        # the CLI command set is known.
        resp2 = self.cli.cmd("help")
        assert resp2, "Telnet session lost on second command after verbose response"

    # ── Add more Telnet tests below ───────────────────────────────────────────
