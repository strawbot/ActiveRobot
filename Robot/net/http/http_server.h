#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <stdint.h>
#include <stdbool.h>

// ── Flash-resident HTTP response ──────────────────────────────────────────────
// Each entry is a complete HTTP response: status line + headers + blank + body.
// The data pointer may reference flash or a long-lived static buffer.
// tcp_write() with TCP_WRITE_FLAG_COPY is always used so stack buffers are fine.

typedef struct {
    const char *data;
    uint32_t    length;
} http_response_t;

typedef const http_response_t *(*http_handler_fn)(const char *req, uint16_t len);

// ── Route table entry ─────────────────────────────────────────────────────────
typedef struct {
    const char            *method;     // "GET" or "POST"
    const char            *url;
    const http_response_t *response;   // non-NULL: static flash response
    http_handler_fn        handler;    // non-NULL: dynamic handler
} http_route_t;

// ── Standard built-in responses ───────────────────────────────────────────────
extern const http_response_t http_404;
extern const http_response_t http_204;

// ── SSE channel ───────────────────────────────────────────────────────────────
// Board allocates one http_sse_chan_t per persistent stream.  The engine owns
// the pcb field — do not write it from board code.

struct tcp_pcb;    // forward declaration; board code need not include lwip/tcp.h

typedef struct {
    struct tcp_pcb *pcb;          // NULL when no client connected; set by engine
    void (*on_connect)(void);     // called by engine after SSE upgrade; may be NULL
} http_sse_chan_t;

#ifndef HTTP_MAX_SSE_CHANNELS
#define HTTP_MAX_SSE_CHANNELS  8
#endif

// ── Lifecycle ─────────────────────────────────────────────────────────────────

// Call once from network_init() before any connections can arrive.
void http_server_init(void);

// Call from link_callback when IP is confirmed — opens TCP listener port 80.
void http_server_start(void);

// Call from link_callback on link down — closes listener and all connections.
void http_server_stop(void);

// Fill *active and *idle with current connection counts.
void http_server_stats(uint8_t *active, uint8_t *idle);

// ── Board registration ────────────────────────────────────────────────────────

// Supply the board's route table.  Call from board init before any connections
// arrive.  The array must remain valid for the lifetime of the server.
// Terminal stream routes (/term_stream, /term_in) are built-in; omit them.
// SSE paths registered via http_sse_bind() are also handled automatically;
// do not add them to the route table.
void http_server_set_routes(const http_route_t *routes, int count);

// Register a persistent SSE GET endpoint backed by chan.  When a client
// connects to path, the engine upgrades the connection and sets chan->pcb.
// A new connection silently evicts any existing one.
// path must remain valid for the server's lifetime (use a string literal).
// At most HTTP_MAX_SSE_CHANNELS channels.
void http_sse_bind(const char *path, http_sse_chan_t *chan);

// Push len bytes to a registered SSE channel.  No-op if no client is
// connected.  data need only be valid for the duration of the call —
// TCP_WRITE_FLAG_COPY is used.
void http_sse_push(http_sse_chan_t *chan, const char *data, uint16_t len);

// ── Diagnostics ───────────────────────────────────────────────────────────────
// Incremented on every http_recv call.  Useful for verifying that the LwIP
// TCP callback chain is alive.  Declared here so board CLI code can extern it.
extern volatile uint32_t dbg_http_recv;

// ── Terminal stream keepalive ─────────────────────────────────────────────────
// Sends an SSE comment ping (": ping\n\n") on the /term_stream connection to
// keep it alive through proxies.  Scheduled by the engine on connect and
// reschedules itself while a client is connected.
void http_sse_keepalive(void);

#endif // HTTP_SERVER_H
