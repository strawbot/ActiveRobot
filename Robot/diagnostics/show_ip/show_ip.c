// show_ip.c — "show-ip" CLI diagnostic (Robot/diagnostics)
//
// Prints the IP address, gateway, netmask, DHCP state, and link/netif flags
// of the default lwIP network interface (netif_default).
//
// Board-agnostic: depends only on lwIP and TimbreOS printers.h.
// Register in the board's wordlist (tivawords.txt etc.) pointing to show_ip().

#include "printers.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/dhcp.h"

void show_ip(void)
{
    struct netif *n = netif_default;
    if (!n) { print("no netif\r\n"); return; }

    print("IP:       "); print(ip4addr_ntoa(netif_ip4_addr(n)));    print("\r\n");
    print("gateway:  "); print(ip4addr_ntoa(netif_ip4_gw(n)));      print("\r\n");
    print("netmask:  "); print(ip4addr_ntoa(netif_ip4_netmask(n))); print("\r\n");

    print("DHCP:     ");
    struct dhcp *d = netif_dhcp_data(n);
    if (!d)                            print("(static)");
    else if (dhcp_supplied_address(n)) print("bound");
    else                               print("searching");
    print("\r\n");

    print("link:     ");
    print(netif_is_up(n)      ? "up "      : "down ");
    print(netif_is_link_up(n) ? "link-up"  : "link-down");
    print("\r\n");

    print("MAC:      ");
    for (int i = 0; i < n->hwaddr_len; i++) {
        dotnb(2, 2, n->hwaddr[i], 16);
        if (i < n->hwaddr_len - 1) print(":");
    }
    print("\r\n");
}
