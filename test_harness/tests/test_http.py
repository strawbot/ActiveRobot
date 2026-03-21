"""
test_http.py – HTTP server tests.

Verifies that the device's embedded HTTP server responds correctly.  Tests
are skipped automatically when the HTTP port is not reachable.

What to customise
─────────────────
  • http_expected_status in config.yaml (default 200).
  • http_root_path in config.yaml (default '/').
  • Replace the placeholder tests (search for TODO) with checks against your
    actual web page content, form endpoints, or REST API paths.
"""

import pytest


pytestmark = pytest.mark.http


class TestHttpServer:
    """HTTP server smoke tests."""

    @pytest.fixture(autouse=True)
    def setup(self, http):
        self.http = http

    # ── Basic availability ────────────────────────────────────────────────────

    def test_root_returns_200(self, cfg):
        """The root URL must respond with HTTP 200."""
        self.http.assert_ok(cfg.http_root_path)

    def test_root_returns_html(self, cfg):
        """The root URL should return an HTML content-type."""
        r = self.http.assert_ok(cfg.http_root_path)
        content_type = r.headers.get("Content-Type", "")
        assert "html" in content_type.lower(), (
            f"Expected HTML content-type, got: {content_type!r}"
        )

    def test_response_is_non_empty(self, cfg):
        """The HTTP body should not be empty."""
        r = self.http.assert_ok(cfg.http_root_path)
        assert len(r.content) > 0, "HTTP response body is empty"

    # ── Content checks ────────────────────────────────────────────────────────
    # TODO: Replace the placeholders below with strings that actually appear
    #       in your device's web page.

    def test_page_contains_device_name(self, cfg):
        """
        The root page should contain the device's product name or title.
        TODO: update 'My Device' to match your actual page title or content.
        """
        pytest.skip("Replace 'My Device' with a real string from your web page")
        # self.http.assert_contains(cfg.http_root_path, "My Device")

    def test_page_contains_version(self, cfg):
        """
        The root page should contain the firmware version.
        TODO: update the expected string.
        """
        pytest.skip("Replace 'v1.' with a real version prefix from your web page")
        # self.http.assert_contains(cfg.http_root_path, "v1.")

    # ── Error handling ────────────────────────────────────────────────────────

    def test_404_for_unknown_path(self):
        """Requesting a non-existent path should return 4xx (typically 404)."""
        r = self.http.get("/this-path-should-not-exist-xyzzy-9999")
        assert 400 <= r.status_code < 500, (
            f"Expected 4xx for unknown path, got {r.status_code}"
        )

    # ── Form / API endpoints ──────────────────────────────────────────────────
    # TODO: Add tests for your specific REST endpoints or form submissions.
    #
    # Example:
    #
    # def test_api_status_endpoint(self):
    #     r = self.http.assert_ok("/api/status")
    #     data = r.json()
    #     assert "uptime" in data
    #
    # def test_api_set_led(self):
    #     r = self.http.post("/api/led", json={"state": "on"})
    #     assert r.status_code == 200

    # ── Response headers ──────────────────────────────────────────────────────

    def test_server_header_present(self, cfg):
        """
        Many embedded servers include a 'Server' header.
        This test is informational; adjust or remove if yours doesn't.
        """
        r = self.http.get(cfg.http_root_path)
        # Not a hard assertion – just log what the server says about itself
        server = r.headers.get("Server", "(no Server header)")
        print(f"\nHTTP Server header: {server}")
