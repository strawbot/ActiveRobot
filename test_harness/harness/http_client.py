"""
HTTP test helpers.

Wraps the `requests` library with device-aware defaults and assertion helpers.
All methods raise AssertionError (or requests exceptions) on failure so they
integrate naturally with pytest.
"""

from __future__ import annotations
import logging
from typing import Optional, Any
import requests

log = logging.getLogger(__name__)


class HttpClient:
    """Thin test-oriented wrapper around `requests` for the device HTTP server."""

    def __init__(self, config):
        self.config  = config
        self._base   = f"http://{config.device_ip}:{config.http_port}"
        self._session = requests.Session()
        self._session.headers.update({"Connection": "close"})

    # ── Raw HTTP methods ──────────────────────────────────────────────────────

    def get(self, path: str = "/",
            timeout: Optional[float] = None,
            **kwargs: Any) -> requests.Response:
        url = self._url(path)
        log.debug("GET %s", url)
        r = self._session.get(url,
                              timeout=timeout or self.config.network_timeout,
                              **kwargs)
        log.debug("  → %d  %d bytes", r.status_code, len(r.content))
        return r

    def post(self, path: str, data: Any = None, json: Any = None,
             timeout: Optional[float] = None,
             **kwargs: Any) -> requests.Response:
        url = self._url(path)
        log.debug("POST %s", url)
        return self._session.post(url, data=data, json=json,
                                  timeout=timeout or self.config.network_timeout,
                                  **kwargs)

    # ── Assertion helpers ─────────────────────────────────────────────────────

    def assert_ok(self, path: str = "/",
                  expected_status: Optional[int] = None) -> requests.Response:
        """GET *path* and assert the response status matches *expected_status*."""
        expected = expected_status or self.config.http_expected_status
        r = self.get(path)
        assert r.status_code == expected, (
            f"GET {path}: expected HTTP {expected}, got {r.status_code}\n"
            f"Body snippet: {r.text[:200]!r}"
        )
        return r

    def assert_contains(self, path: str, text: str,
                        case_sensitive: bool = False) -> requests.Response:
        """GET *path* and assert *text* appears in the response body."""
        r = self.assert_ok(path)
        needle   = text if case_sensitive else text.lower()
        haystack = r.text if case_sensitive else r.text.lower()
        assert needle in haystack, (
            f"GET {path}: expected {text!r} in body.\n"
            f"Body snippet: {r.text[:300]!r}"
        )
        return r

    def assert_header(self, path: str, header: str,
                      expected_value: Optional[str] = None) -> requests.Response:
        """GET *path* and assert the response contains *header* (optionally check value)."""
        r = self.assert_ok(path)
        assert header.lower() in {k.lower() for k in r.headers}, (
            f"GET {path}: expected header '{header}' not in response.\n"
            f"Headers: {dict(r.headers)}"
        )
        if expected_value is not None:
            actual = r.headers.get(header, "")
            assert expected_value.lower() in actual.lower(), (
                f"Header '{header}': expected {expected_value!r}, got {actual!r}"
            )
        return r

    # ── Utility ───────────────────────────────────────────────────────────────

    def _url(self, path: str) -> str:
        return f"{self._base}{path if path.startswith('/') else '/' + path}"
