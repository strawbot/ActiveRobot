"""
test_connectivity.py – Network-level reachability tests.

These tests verify that the basic TCP services are accepting connections
before the higher-level CLI/HTTP tests run.  They act as a fast "is the
device alive on the network?" gate.

No CLI commands are sent here – just socket-level probes.
"""

import socket
import pytest


pytestmark = pytest.mark.network


class TestNetworkReachability:
    """Verify the device's network stack is up and accepting connections."""

    def test_telnet_port_accepts_connections(self, cfg, ports):
        """TCP port 23 should accept a connection within the configured timeout."""
        if not ports.telnet:
            pytest.skip("Telnet not detected")
        try:
            with socket.create_connection(
                (cfg.device_ip, cfg.telnet_port),
                timeout=cfg.network_timeout,
            ):
                pass  # Connection itself is the assertion
        except (socket.timeout, ConnectionRefusedError, OSError) as exc:
            pytest.fail(
                f"Could not connect to {cfg.device_ip}:{cfg.telnet_port} – {exc}"
            )

    def test_http_port_accepts_connections(self, cfg, ports):
        """TCP port 80 should accept a connection within the configured timeout."""
        if not ports.http:
            pytest.skip("HTTP not detected")
        try:
            with socket.create_connection(
                (cfg.device_ip, cfg.http_port),
                timeout=cfg.network_timeout,
            ):
                pass
        except (socket.timeout, ConnectionRefusedError, OSError) as exc:
            pytest.fail(
                f"Could not connect to {cfg.device_ip}:{cfg.http_port} – {exc}"
            )

    def test_device_ip_is_configured(self, cfg):
        """Sanity check: device IP must not be empty."""
        assert cfg.device_ip, "device_ip is not set in config.yaml"
        assert cfg.device_ip != "0.0.0.0", "device_ip looks like an unconfigured placeholder"
