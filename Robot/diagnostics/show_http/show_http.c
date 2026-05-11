// show_http.c — "show-http" CLI diagnostic (Robot/diagnostics)
//
// Prints HTTP server connection pool state and the recv-callback counter.
// Board-agnostic: depends only on Robot/net/http and TimbreOS printers.h.

#include "printers.h"
#include "http_server.h"

void show_http(void)
{
    uint8_t active = 0, idle = 0;
    http_server_stats(&active, &idle);
    print("HTTP port:   80\r\n");
    print("active:      "); printDec(active); print("\r\n");
    print("idle:        "); printDec(idle);   print("\r\n");
    print("recv calls:  "); printDec(dbg_http_recv); print("\r\n");
}
