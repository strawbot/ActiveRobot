"""
test_cross_port.py – Cross-port consistency tests.

Runs the same CLI queries over every available transport and verifies that
the answers are consistent.  A discrepancy between serial and telnet (for
example) can reveal synchronisation bugs, buffering issues, or port-specific
code paths in the firmware.

Single-output-stream constraint
────────────────────────────────
The device has ONE output stream, always directed at the port that most
recently sent it input.  The harness handles this transparently:

  • The session-wide `cli_lock` in conftest.py ensures only one transport
    sends a command at a time.
  • CLI.cmd() automatically sends a silent re-claim ping whenever a different
    transport last owned the stream, before issuing the real command.

As a result, callers here need no special bookkeeping – just use the CLI
fixtures normally.

However, be aware of one consequence for multi-transport comparisons:
because the harness must alternate ownership between transports to compare
their responses, you should not assume that state set in one command is
instantly visible from the other port in the same test.  The device may
process the two commands several milliseconds apart.  For state-mutation
tests, prefer a settle delay or a "read-back via the same port that wrote".

How it works
────────────
These tests use the `any_cli` parametric fixture from conftest.py.  pytest
expands each test into one instance per transport (serial / usb_serial /
telnet) and auto-skips instances whose transport is not connected.

Expected output:
  PASSED  test_cross_port.py::TestCrossPort::test_prompt_alive_on_all_ports[serial]
  PASSED  test_cross_port.py::TestCrossPort::test_prompt_alive_on_all_ports[telnet]
  SKIPPED test_cross_port.py::TestCrossPort::test_prompt_alive_on_all_ports[usb_serial]

What to customise
─────────────────
  Replace every `pytest.skip(...)` with real assertions using your CLI commands.
"""

import pytest


class TestCrossPort:
    """
    The same test body running on every available transport sequentially.
    Uses the `any_cli` parametric fixture.
    """

    # ── Liveness ──────────────────────────────────────────────────────────────

    def test_prompt_alive_on_all_ports(self, any_cli):
        """
        Device should respond with its prompt on every connected interface.
        This is also the basic re-claim smoke test: the harness must
        successfully take ownership of the output stream for each transport.
        """
        assert any_cli.ping(), (
            "Device did not return a prompt on this transport "
            "(re-claim ping may have failed)"
        )

    # ── Consistency checks ────────────────────────────────────────────────────
    # These tests verify that the same CLI command returns structurally
    # consistent output regardless of which port you ask through.

    def test_version_consistent(self, any_cli):
        """
        'version' output should be non-empty and share the same format on
        every transport.
        TODO: Replace 'version' and the expected string with your real command.
        """
        pytest.skip("Replace 'version' with your actual version command")
        # resp = any_cli.cmd_ok("version")
        # assert resp, "Empty version response"
        # any_cli.cmd_expect("version", "v1.")   # check format, not exact value

    def test_status_consistent(self, any_cli):
        """
        'status' output should be structurally consistent across transports.
        TODO: Replace with your actual status command.
        """
        pytest.skip("Replace with your actual status command")
        # any_cli.cmd_ok("status")

    # ── State mutation visible from all ports ─────────────────────────────────
    #
    # To verify that a state change made via one port is visible from another,
    # use session-scoped fixtures to share state between transport instances.
    #
    # Important: the re-claim ping means two consecutive cross-port reads are
    # separated by at least one round-trip.  If your device needs time to
    # settle after a state change, add a short sleep before reading back.
    #
    # Pattern:
    #
    #   @pytest.fixture(scope="session", autouse=True)
    #   def set_led_on(serial_cli):
    #       """Set LED once at session start via serial."""
    #       serial_cli.cmd_ok("led on")
    #       yield
    #       serial_cli.cmd_ok("led off")   # teardown
    #
    #   def test_led_visible_from_all_ports(self, any_cli):
    #       """Every port should see the LED as 'on'."""
    #       any_cli.cmd_expect("led status", "on")
    #
    # The `set_led_on` fixture runs once before any test that requests it.
    # The `any_cli` fixture then polls via serial, USB, and telnet in turn,
    # with automatic re-claiming between each.
