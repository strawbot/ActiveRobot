// http_server.c — LwIP raw TCP HTTP/1.0 server, port 80
//
// Lives in Robot/net/http/ — board-agnostic, depends only on TimbreOS and lwIP.
//
// Design:
//   - Fixed connection pool — no heap (mem_malloc removed; was the source of
//     unexpected heap usage flagged by the canary diagnostic).
//   - LwIP raw/callback API throughout — no blocking, no netconn.
//   - Route dispatch: terminal built-ins → SSE registry → board route table → 404.
//   - Board supplies its route table via http_server_set_routes().
//   - Board registers SSE endpoints via http_sse_bind(); engine handles the
//     TCP upgrade and chan->pcb lifecycle automatically.
//   - Built-in /term_stream (SSE out) and /term_in (POST in) wire the
//     TimbreOS CLI to any HTTP client without board involvement.

#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "tea.h"
#include "cli.h"
#include "byteq.h"
#include "printers.h"

#include "lwip/tcp.h"
#include "lwip/err.h"
#include "lwip/mem.h"

#include "http_server.h"

// ── Configuration ─────────────────────────────────────────────────────────────

#define HTTP_PORT               80
#define HTTP_MAX_CONNECTIONS    4
#define HTTP_REQ_BUF_SIZE       1024    // enough for GET line + Host header

// ── Built-in responses ────────────────────────────────────────────────────────

static const char s_404[] =
    "HTTP/1.0 404 Not Found\r\n"
    "Content-Length: 0\r\nConnection: close\r\n\r\n";
static const char s_204[] =
    "HTTP/1.0 204 No Content\r\n"
    "Content-Length: 0\r\nConnection: close\r\n\r\n";

const http_response_t http_404 = { s_404, sizeof(s_404) - 1 };
const http_response_t http_204 = { s_204, sizeof(s_204) - 1 };

// ── Connection state machine ──────────────────────────────────────────────────

typedef enum {
    HTTP_IDLE,          // slot free
    HTTP_RECEIVING,     // accumulating request
    HTTP_SENDING,       // streaming response
    HTTP_CLOSING,       // draining then closing
    HTTP_SSE,           // persistent SSE stream — never auto-closed
} http_state_t;

typedef struct {
    struct tcp_pcb  *pcb;
    http_state_t     state;
    char             req_buf[HTTP_REQ_BUF_SIZE];
    uint16_t         req_len;
    const char      *tx_ptr;
    uint32_t         tx_remaining;
} http_conn_t;

// Fixed pool — no heap allocation.
static http_conn_t  conns[HTTP_MAX_CONNECTIONS];
static struct tcp_pcb *listener;

// ── Board route table ─────────────────────────────────────────────────────────

static const http_route_t *board_routes      = NULL;
static int                 board_route_count = 0;

// ── SSE channel registry ──────────────────────────────────────────────────────

typedef struct {
    const char      *path;
    http_sse_chan_t  *chan;
} sse_reg_t;

static sse_reg_t sse_registry[HTTP_MAX_SSE_CHANNELS];
static int       sse_registry_count = 0;

// ── Terminal SSE stream ───────────────────────────────────────────────────────
// Built-in: /term_stream (GET → SSE) and /term_in (POST → keyIn).
// Uses TimbreOS emitq to relay CLI output to the connected browser.

static http_conn_t *term_conn = NULL;    // active terminal SSE connection

// Called by TimbreOS whenever emitq has data.  Drains emitq into an SSE frame.
static void http_sse_emit(void)
{
    if (!term_conn || !term_conn->pcb) return;
    if (!qbq(emitq)) return;

    // Guard: if send buffer is nearly full, leave data in emitq.
    // http_sent() will call us again once ACKs free space.
    u16_t room = tcp_sndbuf(term_conn->pcb);
    if (room < 32) return;

    char buf[600];
    int  pos         = 0;
    bool last_was_nl = false;
    int  limit       = (room < (u16_t)sizeof(buf)) ? (int)room : (int)sizeof(buf);

    memcpy(buf, "data: ", 6); pos = 6;

    while (qbq(emitq) && pos < limit - 16) {
        char ch = (char)pullbq(emitq);
        if (ch == '\r') continue;           // strip CR from CRLF pairs
        last_was_nl = (ch == '\n');
        if (ch == '\n') {
            buf[pos++] = '\n';
            if (qbq(emitq))
                memcpy(buf + pos, "data: ", 6), pos += 6;
        } else {
            buf[pos++] = ch;
        }
    }

    // Preserve trailing newline: empty field stops SSE from stripping it.
    if (last_was_nl)
        memcpy(buf + pos, "data: \n", 7), pos += 7;

    buf[pos++] = '\n';    // blank line terminates the SSE event
    buf[pos++] = '\n';

    tcp_write(term_conn->pcb, buf, (uint16_t)pos, TCP_WRITE_FLAG_COPY);
    tcp_output(term_conn->pcb);
}

// Sends an SSE comment ping to keep /term_stream alive through proxies.
// Reschedules itself while a client is connected.
void http_sse_keepalive(void)
{
    if (!term_conn || !term_conn->pcb) return;
    static const char ping[] = ": ping\n\n";
    tcp_write(term_conn->pcb, ping, sizeof(ping) - 1, TCP_WRITE_FLAG_COPY);
    tcp_output(term_conn->pcb);
    after(secs(15), http_sse_keepalive);
}

// ── Pool helpers ──────────────────────────────────────────────────────────────

static http_conn_t *conn_alloc(void)
{
    for (int i = 0; i < HTTP_MAX_CONNECTIONS; i++) {
        if (conns[i].state == HTTP_IDLE) return &conns[i];
    }
    return NULL;
}

static void conn_free(http_conn_t *c)
{
    c->pcb          = NULL;
    c->state        = HTTP_IDLE;
    c->req_len      = 0;
    c->tx_ptr       = NULL;
    c->tx_remaining = 0;
}

// Clear any SSE registrations pointing to this connection.
// Safe to call from http_err (where c->pcb may already be freed by lwIP —
// we only compare pointer values, we never dereference the stored pcb).
static void clear_sse_for_conn(http_conn_t *c)
{
    if (c == term_conn) term_conn = NULL;

    for (int i = 0; i < sse_registry_count; i++) {
        if (sse_registry[i].chan->pcb == c->pcb)
            sse_registry[i].chan->pcb = NULL;
    }
}

static void conn_close(http_conn_t *c)
{
    if (c->pcb == NULL) return;

    clear_sse_for_conn(c);

    struct tcp_pcb *pcb = c->pcb;
    tcp_arg(pcb,  NULL);
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_err(pcb,  NULL);
    conn_free(c);
    tcp_close(pcb);
}

// ── Public registration API ───────────────────────────────────────────────────

void http_server_set_routes(const http_route_t *routes, int count)
{
    board_routes      = routes;
    board_route_count = count;
}

void http_sse_bind(const char *path, http_sse_chan_t *chan)
{
    if (sse_registry_count >= HTTP_MAX_SSE_CHANNELS) {
        print("HTTP: SSE registry full\r\n");
        return;
    }
    sse_registry[sse_registry_count].path = path;
    sse_registry[sse_registry_count].chan = chan;
    sse_registry_count++;
}

void http_sse_push(http_sse_chan_t *chan, const char *data, uint16_t len)
{
    if (!chan || !chan->pcb) return;
    if (tcp_sndbuf(chan->pcb) >= len) {
        tcp_write(chan->pcb, data, len, TCP_WRITE_FLAG_COPY);
        tcp_output(chan->pcb);
    }
}

// ── Request parsing ───────────────────────────────────────────────────────────

static const http_response_t *route_request(const char *req, uint16_t len)
{
    if (len < 10) return &http_404;

    // Extract method and URL from the request line.
    const char *p = (const char *)memchr(req, ' ', len);
    if (!p) return &http_404;
    uint16_t mlen = (uint16_t)(p - req);

    const char *url = p + 1;
    const char *q   = (const char *)memchr(url, ' ', len - mlen - 1);
    if (!q) return &http_404;
    uint16_t ulen = (uint16_t)(q - url);

    // Board route table.
    for (int i = 0; i < board_route_count; i++) {
        const http_route_t *r = &board_routes[i];
        if (strlen(r->method) == mlen && strncmp(r->method, req, mlen) == 0 &&
            strlen(r->url)    == ulen && strncmp(r->url,    url, ulen) == 0) {
            return r->handler ? r->handler(req, len) : r->response;
        }
    }

    return &http_404;
}

// ── SSE headers (same for all streams) ───────────────────────────────────────

static const char sse_headers[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/event-stream\r\n"
    "Cache-Control: no-cache\r\n"
    "Connection: keep-alive\r\n"
    "\r\n";

// ── Response streaming ────────────────────────────────────────────────────────

static void send_chunk(http_conn_t *c)
{
    while (c->tx_remaining > 0) {
        uint16_t space = tcp_sndbuf(c->pcb);
        if (space == 0) break;

        uint16_t chunk = (uint16_t)(c->tx_remaining < space
                                    ? c->tx_remaining : space);
        uint8_t flags = TCP_WRITE_FLAG_COPY;
        if (c->tx_remaining > chunk) flags |= TCP_WRITE_FLAG_MORE;

        err_t err = tcp_write(c->pcb, c->tx_ptr, chunk, flags);
        if (err != ERR_OK) break;

        c->tx_ptr       += chunk;
        c->tx_remaining -= chunk;
    }

    tcp_output(c->pcb);

    if (c->tx_remaining == 0)
        c->state = HTTP_CLOSING;
}

// ── LwIP callbacks ────────────────────────────────────────────────────────────

// Diagnostic counter — incremented on every http_recv call.
// Readable from CLI via show_stats() / dbg_http_recv.
volatile uint32_t dbg_http_recv = 0;

static err_t http_recv(void *arg, struct tcp_pcb *pcb,
                        struct pbuf *p, err_t err)
{
    dbg_http_recv++;
    http_conn_t *c = (http_conn_t *)arg;

    if (!p || err != ERR_OK) {
        conn_close(c);
        return ERR_OK;
    }

    if (c->state == HTTP_RECEIVING) {
        // Accumulate into request buffer.
        struct pbuf *q = p;
        while (q && c->req_len < HTTP_REQ_BUF_SIZE - 1) {
            uint16_t copy = (uint16_t)(HTTP_REQ_BUF_SIZE - 1 - c->req_len);
            if (copy > q->len) copy = q->len;
            memcpy(c->req_buf + c->req_len, q->payload, copy);
            c->req_len += copy;
            q = q->next;
        }
        c->req_buf[c->req_len] = '\0';

        const char *hdr_end = strstr(c->req_buf, "\r\n\r\n");
        if (hdr_end) {
            uint16_t header_len = (uint16_t)(hdr_end - c->req_buf) + 4;

            uint32_t content_length = 0;
            const char *cl = strstr(c->req_buf, "Content-Length: ");
            if (cl) content_length = (uint32_t)strtoul(cl + 16, NULL, 10);

            if (c->req_len < header_len + content_length) goto done;

            // ── 1. Check terminal built-ins ───────────────────────────────────

            // Extract method and URL for built-in checks.
            const char *sp = (const char *)memchr(c->req_buf, ' ', c->req_len);
            uint16_t mlen = sp ? (uint16_t)(sp - c->req_buf) : 0;
            const char *url_start = sp ? sp + 1 : NULL;
            const char *sp2 = url_start
                ? (const char *)memchr(url_start, ' ', c->req_len - mlen - 1)
                : NULL;
            uint16_t ulen = (sp2 && url_start) ? (uint16_t)(sp2 - url_start) : 0;

            bool is_get  = (mlen == 3 && strncmp(c->req_buf, "GET",  3) == 0);
            bool is_post = (mlen == 4 && strncmp(c->req_buf, "POST", 4) == 0);

            if (is_get && ulen == 12 &&
                strncmp(url_start, "/term_stream", 12) == 0) {
                // Upgrade to terminal SSE.
                if (term_conn && term_conn->pcb) conn_close(term_conn);
                term_conn = c;
                c->state  = HTTP_SSE;
                tcp_poll(c->pcb, NULL, 0);
                tcp_write(c->pcb, sse_headers, sizeof(sse_headers) - 1,
                          TCP_WRITE_FLAG_COPY);
                tcp_output(c->pcb);
                after(secs(15), http_sse_keepalive);
                goto done;
            }

            if (is_post && ulen == 8 &&
                strncmp(url_start, "/term_in", 8) == 0) {
                const char *body = hdr_end + 4;
                int blen = (int)(c->req_len - header_len);
                autoEchoOn();
                when(EmitEvent, http_sse_emit);
                for (int i = 0; i < blen; i++)
                    keyIn((uint8_t)body[i]);
                c->tx_ptr       = http_204.data;
                c->tx_remaining = http_204.length;
                c->state        = HTTP_SENDING;
                send_chunk(c);
                goto done;
            }

            // ── 2. Check registered SSE channels ─────────────────────────────

            if (is_get && url_start) {
                for (int i = 0; i < sse_registry_count; i++) {
                    const char *rpath = sse_registry[i].path;
                    size_t rlen = strlen(rpath);
                    if (ulen == (uint16_t)rlen &&
                        strncmp(url_start, rpath, rlen) == 0) {
                        http_sse_chan_t *ch = sse_registry[i].chan;
                        // Evict any existing client on this channel.
                        if (ch->pcb) {
                            for (int j = 0; j < HTTP_MAX_CONNECTIONS; j++) {
                                if (conns[j].pcb == ch->pcb) {
                                    conn_close(&conns[j]);
                                    break;
                                }
                            }
                        }
                        ch->pcb  = c->pcb;
                        c->state = HTTP_SSE;
                        tcp_poll(c->pcb, NULL, 0);
                        tcp_write(c->pcb, sse_headers, sizeof(sse_headers) - 1,
                                  TCP_WRITE_FLAG_COPY);
                        tcp_output(c->pcb);
                        if (ch->on_connect) ch->on_connect();
                        goto done;
                    }
                }
            }

            // ── 3. Board route table ──────────────────────────────────────────

            const http_response_t *resp =
                route_request(c->req_buf, c->req_len);

            c->tx_ptr       = resp->data;
            c->tx_remaining = resp->length;
            c->state        = HTTP_SENDING;
            send_chunk(c);
        }
    done:;
    }

    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    if (c->state == HTTP_CLOSING)
        conn_close(c);

    return ERR_OK;
}

static err_t http_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    (void)pcb; (void)len;
    http_conn_t *c = (http_conn_t *)arg;
    if (!c) return ERR_OK;

    if (c->state == HTTP_SSE) {
        // For the terminal connection, drain any pending emitq data now that
        // send-buffer space has been freed by ACKs.  Other SSE connections
        // are pushed externally and need no action here.
        if (c == term_conn)
            http_sse_emit();
        return ERR_OK;
    }

    if (c->state == HTTP_SENDING)
        send_chunk(c);
    if (c->state == HTTP_CLOSING)
        conn_close(c);

    return ERR_OK;
}

static void http_err(void *arg, err_t err)
{
    (void)err;
    http_conn_t *c = (http_conn_t *)arg;
    if (c) {
        // PCB is already freed by lwIP — clear our references but do not
        // call tcp_close.  Pointer comparison in clear_sse_for_conn is safe
        // because we only compare values, never dereference.
        clear_sse_for_conn(c);
        c->pcb = NULL;
        conn_free(c);
    }
}

static err_t http_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK) return err;

    http_conn_t *c = conn_alloc();
    if (!c) {
        tcp_abort(newpcb);
        return ERR_MEM;
    }

    // conn_alloc() returns a slot that was already IDLE; clear it fully.
    memset(c, 0, sizeof(http_conn_t));
    c->pcb   = newpcb;
    c->state = HTTP_RECEIVING;

    tcp_arg(newpcb,  c);
    tcp_recv(newpcb, http_recv);
    tcp_sent(newpcb, http_sent);
    tcp_err(newpcb,  http_err);

    return ERR_OK;
}

// ── Public API ────────────────────────────────────────────────────────────────

void http_server_init(void)
{
    for (int i = 0; i < HTTP_MAX_CONNECTIONS; i++)
        conn_free(&conns[i]);
    listener           = NULL;
    term_conn          = NULL;
    board_routes       = NULL;
    board_route_count  = 0;
    sse_registry_count = 0;
    namedAction(http_sse_keepalive);
}

void http_server_start(void)
{
    if (listener != NULL) return;

    listener = tcp_new();
    if (listener == NULL) {
        print("HTTP: tcp_new failed\r\n");
        return;
    }

    tcp_bind(listener, IP_ADDR_ANY, HTTP_PORT);
    listener = tcp_listen(listener);

    if (listener == NULL) {
        print("HTTP: tcp_listen failed\r\n");
        return;
    }

    tcp_accept(listener, http_accept);
    print("HTTP: listening on port 80\r\n");
}

void http_server_stats(uint8_t *active, uint8_t *idle)
{
    *active = 0; *idle = 0;
    for (int i = 0; i < HTTP_MAX_CONNECTIONS; i++) {
        if (conns[i].state == HTTP_IDLE) (*idle)++;
        else                             (*active)++;
    }
}

void http_server_stop(void)
{
    // Clear all SSE channel pcbs before the conn_close loop so the
    // clear_sse_for_conn calls inside conn_close find nothing to clear.
    term_conn = NULL;
    for (int i = 0; i < sse_registry_count; i++)
        sse_registry[i].chan->pcb = NULL;

    for (int i = 0; i < HTTP_MAX_CONNECTIONS; i++) {
        if (conns[i].state != HTTP_IDLE)
            conn_close(&conns[i]);
    }

    if (listener != NULL) {
        tcp_close(listener);
        listener = NULL;
    }

    print("HTTP: stopped\r\n");
}
