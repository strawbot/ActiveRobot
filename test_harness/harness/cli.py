"""
High-level CLI helper.

Wraps a CLITransport and provides assertion-style helper methods that
produce informative pytest failure messages.

Single-output-stream contract
──────────────────────────────
The device has ONE output stream.  It always directs responses to whichever
port most recently sent it input.  The CLI class enforces two rules:

  1. All cmd() calls are serialised through a shared threading.Lock so that
     two transports can never be mid-command at the same time.

  2. On every cmd() call, CLI checks whether *this* transport was the last
     to send.  If not (a different transport sent the previous command), it
     sends a short re-claim ping first so the device redirects its output
     stream here before the real command is sent.

Both behaviours are automatic – callers do not need to think about them.
"""

from __future__ import annotations
import re
import time
import threading
import logging
from typing import Optional, Union
from .transport import CLITransport

log = logging.getLogger(__name__)


# ──────────────────────────────────────────────────────────────────────────────
# Shared mutable "which transport owns the output stream right now?"
# ──────────────────────────────────────────────────────────────────────────────

class ActiveTransportRef:
    """
    A tiny mutable box that holds the transport currently owning the device's
    output stream.  One instance is shared across all CLI objects in a session
    (passed in from conftest.py fixtures).
    """
    def __init__(self) -> None:
        self.transport: Optional[CLITransport] = None


# ──────────────────────────────────────────────────────────────────────────────
# CLI helper
# ──────────────────────────────────────────────────────────────────────────────

class CLI:
    """Thin wrapper around a CLITransport with test-friendly helpers."""

    # How long to wait for a re-claim ping response before giving up and
    # sending the real command anyway.
    RECLAIM_TIMEOUT = 1.0

    def __init__(self, transport: CLITransport, config,
                 lock: Optional[threading.Lock] = None,
                 active_ref: Optional[ActiveTransportRef] = None):
        self.transport  = transport
        self.config     = config
        # If no lock/ref supplied (e.g. standalone use), create private ones.
        self._lock      = lock       or threading.Lock()
        self._active    = active_ref or ActiveTransportRef()

    # ── Raw command ───────────────────────────────────────────────────────────

    def cmd(self, command: str, timeout: Optional[float] = None) -> str:
        """
        Send *command* to the device CLI and return the response string.

        Thread-safe: acquires the session-wide lock before sending.
        Handles output-stream re-claiming automatically when the active
        transport has changed since the last call.
        """
        with self._lock:
            self._maybe_reclaim()
            log.debug("CLI >> %s", command)
            resp = self.transport.send_command(command, timeout)
            log.debug("CLI << %s", resp)
            # We are now definitively the owner of the output stream.
            self._active.transport = self.transport
            return resp

    # ── Assertion helpers (raise AssertionError with a clear message) ─────────

    def cmd_ok(self, command: str, timeout: Optional[float] = None) -> str:
        """
        Send command and assert the response does NOT look like an error.

        Only the FIRST line of the response is checked.  This avoids false
        positives from verbose responses (e.g. 'help' output) whose content
        may legitimately contain words like 'error' or 'fail' as part of a
        command name or description.

        Patterns are deliberately specific (require a colon or stand alone as
        a phrase) rather than bare substrings.
        """
        resp = self.cmd(command, timeout)
        first_line = resp.splitlines()[0].lower() if resp else ""
        error_patterns = (
            "error:",
            "unknown command",
            "invalid command",
            "command not found",
            "not found",
            "syntax error",
        )
        for pat in error_patterns:
            assert pat not in first_line, (
                f"Command '{command}' returned an error on the first line:\n{resp}"
            )
        return resp

    def cmd_expect(self, command: str, expected: str,
                   case_sensitive: bool = False,
                   timeout: Optional[float] = None) -> str:
        """
        Send command and assert that *expected* appears somewhere in the
        response.
        """
        resp = self.cmd(command, timeout)
        needle   = expected if case_sensitive else expected.lower()
        haystack = resp     if case_sensitive else resp.lower()
        assert needle in haystack, (
            f"Command '{command}':\n"
            f"  expected to find : {expected!r}\n"
            f"  actual response  : {resp!r}"
        )
        return resp

    def cmd_expect_re(self, command: str, pattern: str,
                      flags: int = re.IGNORECASE,
                      timeout: Optional[float] = None) -> re.Match:
        """
        Send command and assert the response matches *pattern* (regex).
        Returns the Match object so callers can extract groups.
        """
        resp = self.cmd(command, timeout)
        m = re.search(pattern, resp, flags)
        assert m is not None, (
            f"Command '{command}':\n"
            f"  expected pattern : {pattern!r}\n"
            f"  actual response  : {resp!r}"
        )
        return m

    def cmd_extract_int(self, command: str, pattern: str,
                        group: Union[int, str] = 1,
                        timeout: Optional[float] = None) -> int:
        """
        Send command, apply regex *pattern*, and return the matched group
        converted to int.  Useful for extracting numeric values.

        Example:
            uptime = cli.cmd_extract_int("uptime", r"(\\d+) seconds")
        """
        m = self.cmd_expect_re(command, pattern, timeout=timeout)
        return int(m.group(group))

    # ── Convenience wrappers ──────────────────────────────────────────────────

    def ping(self) -> bool:
        """
        Send a bare newline and check the device responds with a prompt.
        A lightweight liveness check.  Also claims the output stream.
        """
        try:
            self.cmd("")
            return True
        except Exception:
            return False

    # ── Internal: output-stream re-claim ─────────────────────────────────────

    def _maybe_reclaim(self) -> None:
        """
        If a different transport last sent a command, send a silent ping on
        THIS transport first so the device re-directs its output stream here.

        Must be called with self._lock already held.

        Why this works
        ──────────────
        The device routes all output to the port that most recently sent it
        input.  By sending a bare newline (which the device echoes as a fresh
        prompt) we become the "last sender" before the real command lands,
        so the real response comes back to us and not to whichever port was
        previously active.
        """
        if self._active.transport is self.transport:
            return   # We already own the stream – nothing to do.

        prev = getattr(self._active.transport, '__class__', type(None)).__name__
        log.debug(
            "Output stream re-claim: %s → %s",
            prev,
            self.transport.__class__.__name__,
        )
        try:
            # A bare newline just echoes the prompt; we discard the response.
            self.transport.send_command("", timeout=self.RECLAIM_TIMEOUT)
        except Exception as exc:
            # A failed reclaim ping is logged but not fatal – the real command
            # will either succeed or fail on its own merits.
            log.warning("Re-claim ping failed: %s", exc)
        # After the ping we own the stream regardless of whether it succeeded.
        self._active.transport = self.transport
