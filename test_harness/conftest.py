"""
pytest configuration and session-scoped fixtures.

On every test run this file:
  1. Probes all available interfaces (serial, USB CDC, telnet, HTTP).
  2. Prints a summary table so you know immediately what is connected.
  3. Exposes transport fixtures that auto-skip when a port isn't present.

Single-output-stream constraint
────────────────────────────────
The device has ONE output stream.  It always sends responses to whichever
port most recently received input.  This means:

  • Only one CLI transport may be active (sending) at a time.
  • When a different transport takes over, it must first "claim" the output
    stream by sending a command and verifying the device responds to it –
    before trusting any subsequent response.

This is handled automatically by:
  1. cli_lock  – a session-wide threading.Lock that serialises all cmd() calls
                 across every transport.  pytest runs tests sequentially by
                 default, so this mainly protects against accidental parallel
                 use and makes the intent explicit.
  2. CLI.cmd() – detects a transport switch (via the shared `active_transport`
                 reference) and sends a re-claim ping on the new transport
                 before issuing the real command.

Adding a new interface or test type
────────────────────────────────────
  • Add a new marker in pytest_configure().
  • Add a new fixture that calls pytest.skip() if the interface is missing.
  • Pass cli_lock and active_transport to the new CLI() constructor.
  • Decorate the relevant test functions with the new marker.
"""

from __future__ import annotations
import threading
import pytest

from harness.config        import Config
from harness.port_detector import PortDetector, PortStatus
from harness.transport     import SerialTransport, UsbSerialTransport, TelnetTransport
from harness.cli           import CLI, ActiveTransportRef
from harness.http_client   import HttpClient


# ──────────────────────────────────────────────────────────────────────────────
# Register custom markers so pytest doesn't warn about unknown marks
# ──────────────────────────────────────────────────────────────────────────────

def pytest_configure(config: pytest.Config) -> None:
    config.addinivalue_line(
        "markers",
        "serial: requires physical UART serial port to be connected",
    )
    config.addinivalue_line(
        "markers",
        "usb_serial: requires USB CDC serial port to be connected",
    )
    config.addinivalue_line(
        "markers",
        "telnet: requires device to be reachable on TCP port 23",
    )
    config.addinivalue_line(
        "markers",
        "http: requires device HTTP server to be reachable",
    )
    config.addinivalue_line(
        "markers",
        "network: requires any network path (telnet or HTTP)",
    )
    config.addinivalue_line(
        "markers",
        "ntp: requires device NTP client (implies network reachability)",
    )


# ──────────────────────────────────────────────────────────────────────────────
# Session-level singletons
# ──────────────────────────────────────────────────────────────────────────────

@pytest.fixture(scope="session")
def cfg() -> Config:
    """Loaded configuration (config.yaml, falls back to defaults)."""
    return Config.load()


@pytest.fixture(scope="session")
def ports(cfg: Config) -> PortStatus:
    """
    Run port detection exactly once per session and return the result.
    The detector also prints the summary table to the terminal.
    """
    return PortDetector(cfg).detect_all()


@pytest.fixture(scope="session")
def cli_lock() -> threading.Lock:
    """
    Session-wide mutex that serialises ALL CLI send_command() calls.

    Because the device has a single output stream (always directed at the
    port that last sent input), only one transport may be mid-command at any
    given time.  This lock makes that invariant explicit and safe.
    """
    return threading.Lock()


@pytest.fixture(scope="session")
def active_transport() -> ActiveTransportRef:
    """
    Shared mutable reference to whichever transport currently 'owns' the
    device's output stream.  CLI.cmd() updates this on every call so each
    CLI instance knows whether it needs to re-claim the stream before
    trusting a response.
    """
    return ActiveTransportRef()


# ──────────────────────────────────────────────────────────────────────────────
# Transport fixtures  (session-scoped – one connection per run)
# ──────────────────────────────────────────────────────────────────────────────

@pytest.fixture(scope="session")
def serial_transport(cfg: Config, ports: PortStatus):
    """
    Open serial UART transport for the whole session.
    Tests decorated with @pytest.mark.serial are automatically skipped when
    the serial port is not detected.
    """
    if not ports.serial:
        pytest.skip("Serial UART port not detected – skipping serial tests")

    cfg.serial_port = ports.serial_port_name
    t = SerialTransport(cfg)
    t.open()
    yield t
    t.close()


@pytest.fixture(scope="session")
def usb_serial_transport(cfg: Config, ports: PortStatus):
    """Open USB CDC serial transport for the whole session."""
    if not ports.usb_serial:
        pytest.skip("USB CDC serial port not detected – skipping USB serial tests")

    cfg.usb_serial_port = ports.usb_serial_port_name
    t = UsbSerialTransport(cfg)
    t.open()
    yield t
    t.close()


@pytest.fixture(scope="session")
def telnet_transport(cfg: Config, ports: PortStatus):
    """Open Telnet transport for the whole session."""
    if not ports.telnet:
        pytest.skip("Telnet not reachable – skipping telnet tests")

    t = TelnetTransport(cfg)
    t.open()
    yield t
    t.close()


# ──────────────────────────────────────────────────────────────────────────────
# CLI fixtures  (wraps each transport in a CLI helper)
# ──────────────────────────────────────────────────────────────────────────────

@pytest.fixture(scope="session")
def serial_cli(serial_transport, cfg: Config,
               cli_lock, active_transport) -> CLI:
    """CLI helper over serial UART."""
    return CLI(serial_transport, cfg,
               lock=cli_lock, active_ref=active_transport)


@pytest.fixture(scope="session")
def usb_cli(usb_serial_transport, cfg: Config,
            cli_lock, active_transport) -> CLI:
    """CLI helper over USB CDC serial."""
    return CLI(usb_serial_transport, cfg,
               lock=cli_lock, active_ref=active_transport)


@pytest.fixture(scope="session")
def telnet_cli(telnet_transport, cfg: Config,
               cli_lock, active_transport) -> CLI:
    """CLI helper over Telnet."""
    return CLI(telnet_transport, cfg,
               lock=cli_lock, active_ref=active_transport)


# ──────────────────────────────────────────────────────────────────────────────
# HTTP fixture
# ──────────────────────────────────────────────────────────────────────────────

@pytest.fixture(scope="session")
def http(cfg: Config, ports: PortStatus) -> HttpClient:
    """HTTP client pointed at the device.  Skips if HTTP not reachable."""
    if not ports.http:
        pytest.skip("HTTP server not reachable – skipping HTTP tests")
    return HttpClient(cfg)


# ──────────────────────────────────────────────────────────────────────────────
# Parametric fixture: run the same test over all available CLI transports
# ──────────────────────────────────────────────────────────────────────────────

@pytest.fixture(params=["serial", "usb_serial", "telnet"],
                ids=["serial", "usb_serial", "telnet"])
def any_cli(request, ports: PortStatus) -> CLI:
    """
    Parametric fixture that yields each available CLI transport in turn.

    pytest runs each parameter as a separate (sequential) test instance, so
    the single-output-stream constraint is naturally respected – only one
    transport sends commands at a time.

    Use this when you want the same assertions to run on every connected port:

        def test_version(any_cli):
            any_cli.cmd_expect("version", "v1.")

    Tests are auto-skipped for transports that are not connected.

    Note: fixtures are looked up lazily via request.getfixturevalue() so that
    a skip on one transport (e.g. usb_serial not connected) does NOT propagate
    to the other transport instances – which was the bug when all three were
    listed as direct fixture parameters.
    """
    name = request.param
    if name == "serial":
        if not ports.serial:
            pytest.skip("serial not available")
        return request.getfixturevalue("serial_cli")
    elif name == "usb_serial":
        if not ports.usb_serial:
            pytest.skip("usb_serial not available")
        return request.getfixturevalue("usb_cli")
    elif name == "telnet":
        if not ports.telnet:
            pytest.skip("telnet not available")
        return request.getfixturevalue("telnet_cli")
    raise ValueError(f"Unknown transport param: {name!r}")
