#pragma once
#include <stdint.h>

void net_tcp_min_init(void);
void net_tcp_min_listen(uint16_t port);
void net_tcp_min_stop(void);
void net_tcp_min_set_http_body(const char* body);
void net_tcp_min_set_file_path(const char* path);
void net_tcp_min_use_inline(void);
void net_tcp_min_status(void);

// Called by IPv4 layer on incoming TCP packets
void net_tcp_on_ipv4(const uint8_t* ip, uint16_t ip_len);
