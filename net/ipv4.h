#pragma once
#include <stdint.h>
#include <stdbool.h>

void net_ipv4_init(void);

// Configure local address, netmask, and optional gateway (0 to clear)
void net_ipv4_config_set(uint32_t ip_be, uint32_t mask_be, uint32_t gw_be);
void net_ipv4_config_get(uint32_t* ip_be, uint32_t* mask_be, uint32_t* gw_be);

// RX entry point from netface (Ethernet frame without FCS)
void net_ipv4_on_frame(const uint8_t* frame, uint16_t len);

// Shell helpers
bool net_ipv4_set_from_strings(const char* ip, const char* mask, const char* gw);
void net_ipv4_print_config(void);

// ICMP ping
bool net_icmp_ping(uint32_t dst_ip_be, int count, uint32_t timeout_ms);

// Send an IPv4 packet with given proto and payload (payload length <=1500). Returns true on success.
bool net_ipv4_send(uint32_t dst_ip_be, uint8_t proto, const uint8_t* payload, uint16_t plen);
