// ntp_sync.c — SNTP time synchronisation (board-independent)
//
// Part of the Robot/ shared layer.  See ntp_sync.h for the usage contract.
//
// This file was extracted from Discovery/Board/ntp_sync.c as the first
// end-to-end Robot/ slice.  Two changes relative to the original:
//
//   1. The direct call to http_server.h::http_status_push() is replaced by
//      a fan-out through a fixed-capacity observer list.  Boards that want
//      the old behaviour register http_status_push via ntp_on_sync() during
//      network_init.  This breaks the last cross-feature coupling and lets
//      the module live below the HTTP server in the dependency graph.
//
//   2. Added ntp_sync_init() so the observer list is explicitly reset on
//      every board bring-up (previously start/stop/kick were idempotent but
//      there was no single "this feature starts here" entry point).
//
// Architecture
// ─────────────
// lwIP's sntp.c handles all UDP socket management, RFC-compliant retry/back-
// off, and the 1-hour poll interval.  This file provides:
//
//   1. ntp_set_utc_seconds()  — the SNTP_SET_SYSTEM_TIME callback that
//                               records each sync result.
//   2. A simple RAM-based wall clock updated by interpolating getTime()
//                               (TimbreOS uptime, ms resolution).
//   3. ntp_sync_start/stop/kick — lifecycle wrappers called by network_init.
//
// NTP servers
// ────────────
// With SNTP_SERVER_DNS=1 (lwipopts.h) we can use hostnames.  Two servers
// are configured so the SNTP client has a fallback:
//
//   slot 0 : pool.ntp.org       (anycast pool, globally distributed)
//   slot 1 : time.cloudflare.com (reliable single-operator server)
//
// Interface routing
// ─────────────────
// lwIP chooses the output interface based on its routing table.  Ethernet
// (with a DHCP or static gateway) gives the device a default route to the
// internet.  A USB-CDC netif's gateway can be set to the host-assigned
// address so, if the USB host has IP forwarding enabled, NTP packets can
// also reach the internet via USB.  Whichever route resolves first wins.
// This module itself does NOT know which mediums are present.

#include "ntp_sync.h"
#include "printers.h"
#include "tea.h"

#include "lwip/apps/sntp.h"
#include "lwip/ip_addr.h"

// ── Internal state ────────────────────────────────────────────────────────────

static volatile uint32_t utc_at_sync    = 0;  // UTC epoch at last sync
static volatile uint32_t uptime_at_sync = 0;  // getTime() ms at last sync
static volatile bool     synced         = false;

// Observer list.  Fixed-capacity, no heap.  NULL slots are free.
static ntp_sync_observer_t observers[NTP_MAX_OBSERVERS];
static uint8_t             observer_count = 0;

// ── Helpers ───────────────────────────────────────────────────────────────────

static void configure_servers(void)
{
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_setservername(1, "time.cloudflare.com");
}

static void fanout_sync(uint32_t sec)
{
    for (uint8_t i = 0; i < observer_count; i++) {
        if (observers[i]) {
            observers[i](sec);
        }
    }
}

// Print an unsigned decimal without pulling in printf.  Same technique as
// the original Discovery version — kept here to stay self-contained.
static void print_u32(uint32_t n)
{
    char buf[12];
    int i = 10;
    buf[11] = '\0';
    if (n == 0) {
        buf[i] = '0';
    } else {
        while (n > 0 && i >= 0) {
            buf[i--] = (char)('0' + (n % 10));
            n /= 10;
        }
        i++;
    }
    print(&buf[i]);
}

// ── SNTP callback ─────────────────────────────────────────────────────────────
//
// Called via the SNTP_SET_SYSTEM_TIME(sec) macro in lwipopts.h whenever
// sntp.c receives a valid NTP response.

void ntp_set_utc_seconds(uint32_t sec)
{
    utc_at_sync    = sec;
    uptime_at_sync = (uint32_t)getTime();
    synced         = true;

    print("NTP: synced, UTC=");
    print_u32(sec);
    print("\r\n");

    // Fan out to all registered observers (e.g. http_status_push).
    fanout_sync(sec);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void ntp_sync_init(void)
{
    for (uint8_t i = 0; i < NTP_MAX_OBSERVERS; i++) {
        observers[i] = 0;
    }
    observer_count = 0;
    // Leave utc_at_sync / synced alone: init can be called on re-bring-up
    // and the last known time is still a useful fallback.
}

void ntp_sync_start(void)
{
    if (sntp_enabled()) {
        return;  // already running — use ntp_sync_kick() if a fresh burst is needed
    }

    print("NTP: starting SNTP client (pool.ntp.org / time.cloudflare.com)\r\n");
    configure_servers();
    sntp_init();
}

void ntp_sync_stop(void)
{
    if (!sntp_enabled()) {
        return;
    }
    sntp_stop();
    print("NTP: SNTP stopped\r\n");
}

void ntp_sync_kick(void)
{
    // Restart the SNTP client so it sends a fresh request immediately rather
    // than waiting for the next scheduled poll.  Useful after DHCP binds and
    // sets up DNS — without a kick the first request may have already failed
    // with SNTP_STARTUP_DELAY expired but DNS not ready.
    if (sntp_enabled()) {
        sntp_stop();
    }
    print("NTP: kick — requesting immediate sync\r\n");
    configure_servers();
    sntp_init();
}

// ── Queries ───────────────────────────────────────────────────────────────────

uint32_t ntp_get_utc(void)
{
    if (!synced) {
        return 0;
    }
    uint32_t elapsed_ms = (uint32_t)getTime() - uptime_at_sync;
    return utc_at_sync + elapsed_ms / 1000u;
}

bool ntp_is_synced(void)
{
    return synced;
}

// ── Observer registration ────────────────────────────────────────────────────

void ntp_on_sync(ntp_sync_observer_t cb)
{
    if (!cb) {
        return;
    }
    if (observer_count >= NTP_MAX_OBSERVERS) {
        print("NTP: observer list full — raise NTP_MAX_OBSERVERS\r\n");
        return;
    }
    observers[observer_count++] = cb;
}
