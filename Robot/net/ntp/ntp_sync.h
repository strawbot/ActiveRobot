// ntp_sync.h — SNTP time synchronisation (board-independent)
//
// Part of the Robot/ shared layer.  Depends only on TimbreOS (tea.h, printers.h)
// and lwIP's SNTP client app.  Knows nothing about Ethernet, USB, or any
// specific physical medium — routing is lwIP's job.
//
// Boards adopt this module by:
//   1. Calling ntp_sync_init() once during network bring-up.
//   2. Calling ntp_sync_start() / _stop() / _kick() in their link callbacks.
//   3. Optionally registering listeners with ntp_on_sync() to react to each
//      successful NTP response (e.g. to push a status update over HTTP).
//
// The SNTP client uses DNS to reach "pool.ntp.org" and "time.cloudflare.com".
// It retries every 15 s until a response is received, then polls once per
// hour per the RFC.

#ifndef ROBOT_NET_NTP_SYNC_H
#define ROBOT_NET_NTP_SYNC_H

#include <stdint.h>
#include <stdbool.h>

// Maximum number of on_sync observers.  Fixed-capacity — no heap.
// Raise if a board legitimately needs more listeners.
#ifndef NTP_MAX_OBSERVERS
#define NTP_MAX_OBSERVERS 4
#endif

// Observer callback type — fired on every successful NTP response.
// utc_seconds: Unix epoch at moment of sync (same value ntp_get_utc() returns).
// Runs in the context of the lwIP timeout action; keep it brief and use
// later()/after() for anything heavy.
typedef void (*ntp_sync_observer_t)(uint32_t utc_seconds);

// ── Lifecycle ─────────────────────────────────────────────────────────────────

// ntp_sync_init — call once from the board's network_init BEFORE start().
// Clears the observer list and resets internal state.  Idempotent.
void ntp_sync_init(void);

// ntp_sync_start — begin SNTP polling.
// Safe to call multiple times; no-op if SNTP is already running.
void ntp_sync_start(void);

// ntp_sync_stop — stop SNTP polling.
void ntp_sync_stop(void);

// ntp_sync_kick — force an immediate re-sync attempt.
// Restarts the SNTP PCB so a fresh request goes out immediately rather than
// waiting for the next scheduled poll.  Useful after DHCP binds and DNS is
// newly configured, or after a MAC reset has invalidated any open sockets.
void ntp_sync_kick(void);

// ── Queries ───────────────────────────────────────────────────────────────────

// ntp_get_utc — seconds since the Unix epoch (UTC).
// Interpolates from the last sync using getTime() (TimbreOS uptime, ms).
// Returns 0 if no successful sync has occurred yet.
uint32_t ntp_get_utc(void);

// ntp_is_synced — true after the first successful NTP response.
bool ntp_is_synced(void);

// ── Observer registration ─────────────────────────────────────────────────────

// ntp_on_sync — register a callback fired on every successful NTP response.
// Registration is at most NTP_MAX_OBSERVERS; further calls are no-ops and
// print a warning.  Callbacks cannot currently be unregistered — match the
// fire-and-forget style of the rest of TimbreOS.
void ntp_on_sync(ntp_sync_observer_t cb);

// ── Internal — called by lwIP's SNTP client via SNTP_SET_SYSTEM_TIME ──────────

// ntp_set_utc_seconds — wired to lwIP's SNTP_SET_SYSTEM_TIME macro in
// lwipopts.h.  Not for direct use by application code.
void ntp_set_utc_seconds(uint32_t sec);

#endif // ROBOT_NET_NTP_SYNC_H
