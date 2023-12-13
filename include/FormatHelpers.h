#pragma once

#define BSSID_FMT        "%02X:%02X:%02X:%02X:%02X:%02X"
#define BSSID_ARG(bssid) bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]
#define BSSID_FMT_LEN    18

#define IPV4ADDR_FMT       "%u.%u.%u.%u"
#define IPV4ADDR_ARG(addr) addr[0], addr[1], addr[2], addr[3]
#define IPV4ADDR_FMT_LEN   15

#define IPV6ADDR_FMT       "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x"
#define IPV6ADDR_ARG(addr) addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], addr[6], addr[7], addr[8], addr[9], addr[10], addr[11], addr[12], addr[13], addr[14], addr[15]
#define IPV6ADDR_FMT_LEN   39
