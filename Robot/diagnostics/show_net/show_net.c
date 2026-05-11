// show_net.c — "show-net" CLI diagnostic (Robot/diagnostics)
//
// Prints lwIP compile-time flags and runtime protocol statistics.
// Board-agnostic: depends only on lwIP and TimbreOS printers.h.

#include "printers.h"
#include "lwip/opt.h"
#include "lwip/stats.h"

void show_net(void)
{
    // ── Compile-time config ───────────────────────────────────────────────
#if LWIP_ICMP
    print("ICMP:       enabled\r\n");
#else
    print("ICMP:       DISABLED\r\n");
#endif
#if LWIP_DHCP
    print("DHCP:       enabled\r\n");
#else
    print("DHCP:       disabled\r\n");
#endif
#if LWIP_DNS
    print("DNS:        enabled\r\n");
#else
    print("DNS:        disabled\r\n");
#endif

    print("CKSUM gen: ");
    print(CHECKSUM_GEN_IP   ? " IP"   : "");
    print(CHECKSUM_GEN_TCP  ? " TCP"  : "");
    print(CHECKSUM_GEN_UDP  ? " UDP"  : "");
    print(CHECKSUM_GEN_ICMP ? " ICMP" : "");
    print("\r\n");

    // ── Runtime statistics ────────────────────────────────────────────────
#if LWIP_STATS
    print("IP  RX:     "); printDec(lwip_stats.ip.recv);    print("\r\n");
    print("IP  TX:     "); printDec(lwip_stats.ip.xmit);    print("\r\n");
    print("IP  drop:   "); printDec(lwip_stats.ip.drop);    print("\r\n");
    print("IP  err:    "); printDec(lwip_stats.ip.err);     print("\r\n");
    print("IP  chkerr: "); printDec(lwip_stats.ip.chkerr);  print("\r\n");
#  if LWIP_ICMP
    print("ICMP RX:    "); printDec(lwip_stats.icmp.recv);  print("\r\n");
    print("ICMP TX:    "); printDec(lwip_stats.icmp.xmit);  print("\r\n");
    print("ICMP drop:  "); printDec(lwip_stats.icmp.drop);  print("\r\n");
#  endif
    print("ARP  RX:    "); printDec(lwip_stats.etharp.recv); print("\r\n");
    print("ARP  TX:    "); printDec(lwip_stats.etharp.xmit); print("\r\n");
    print("TCP  err:   "); printDec(lwip_stats.tcp.err);     print("\r\n");
    print("mem  used:  "); printDec(lwip_stats.mem.used);    print("\r\n");
    print("mem  err:   "); printDec(lwip_stats.mem.err);     print("\r\n");
    print("pbuf used:  "); printDec(lwip_stats.memp[MEMP_PBUF_POOL]->used); print("\r\n");
    print("pbuf max:   "); printDec(lwip_stats.memp[MEMP_PBUF_POOL]->max);  print("\r\n");
#else
    print("(LWIP_STATS disabled)\r\n");
#endif
}
