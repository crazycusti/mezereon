#include "ipv4.h"
#include "../netface.h"
#include "../console.h"
#include "../platform.h"
#include <stdint.h>
#include <stddef.h>

// Utilities
static uint16_t csum16(const void* data, uint16_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t sum = 0;
    while (len > 1) { sum += ((uint16_t)p[0] << 8) | p[1]; p += 2; len -= 2; }
    if (len) sum += ((uint16_t)p[0] << 8);
    // fold
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static __attribute__((unused)) uint16_t htons16(uint16_t v){ return (uint16_t)((v<<8) | (v>>8)); }
static uint32_t htonl32(uint32_t v){ return ((v&0xFF)<<24)|((v&0xFF00)<<8)|((v&0xFF0000)>>8)|((v>>24)&0xFF); }

// Local config (stored big-endian/network order)
static uint32_t g_ip, g_mask, g_gw;
static uint8_t  g_mac[6];

// ARP cache: store IPv4 in network byte order (big-endian value)
typedef struct { uint32_t ip; uint8_t mac[6]; uint32_t ts; int used; } arp_entry_t;
static arp_entry_t g_arp[8];

static void arp_cache_learn(uint32_t ip, const uint8_t mac[6]){
    int slot = -1;
    for (int i=0;i<8;i++){ if (g_arp[i].used && g_arp[i].ip==ip){ slot=i; break; } if (!g_arp[i].used && slot<0) slot=i; }
    if (slot<0) slot=0;
    g_arp[slot].ip=ip; for (int i=0;i<6;i++) g_arp[slot].mac[i]=mac[i]; g_arp[slot].used=1; g_arp[slot].ts=platform_ticks_get();
}

static int arp_cache_lookup(uint32_t ip, uint8_t mac[6]){
    for (int i=0;i<8;i++){ if (g_arp[i].used && g_arp[i].ip==ip){ for (int j=0;j<6;j++) mac[j]=g_arp[i].mac[j]; return 1; } }
    return 0;
}

void net_ipv4_config_set(uint32_t ip_be, uint32_t mask_be, uint32_t gw_be){
    g_ip=ip_be; g_mask=mask_be; g_gw=gw_be; (void)netface_get_mac(g_mac);
}

void net_ipv4_config_get(uint32_t* ip_be, uint32_t* mask_be, uint32_t* gw_be){ if (ip_be) *ip_be=g_ip; if (mask_be) *mask_be=g_mask; if (gw_be) *gw_be=g_gw; }

static uint32_t parse_ipv4_str(const char* s, int* ok){
    uint32_t a=0; int part=0; uint32_t v=0; int d=0; *ok=0;
    while (*s && part<4){
        if (*s>='0'&&*s<='9'){
            v = v*10 + (uint32_t)(*s-'0');
            if (v>255) return 0;
            d = 1;
        } else if (*s=='.'){
            if (!d) return 0;
            a = (a<<8) | v; v = 0; d = 0; part++;
        } else {
            return 0;
        }
        s++;
    }
    if (part!=3 || !d) return 0;
    a=(a<<8)|v; *ok=1; return a; // return big-endian/network-order value
}

bool net_ipv4_set_from_strings(const char* ip, const char* mask, const char* gw){
    int ok1=0, ok2=0, ok3=1; uint32_t a=parse_ipv4_str(ip,&ok1), m=parse_ipv4_str(mask,&ok2), g=0;
    if (gw && *gw){ g=parse_ipv4_str(gw,&ok3); }
    if (!ok1||!ok2||!ok3) return false;
    net_ipv4_config_set(a,m,g);
    return true;
}

static void print_ip(uint32_t be){
    // be is big-endian; print MSB..LSB
    for (int i=3;i>=0;i--){
        uint8_t oct = (uint8_t)((be>>(i*8)) & 0xFF);
        console_write_dec((uint32_t)oct);
        if (i) console_write(".");
    }
}

void net_ipv4_print_config(void){
    console_write("ip="); print_ip(g_ip); console_write(" mask="); print_ip(g_mask); console_write(" gw="); if (g_gw) print_ip(g_gw); else console_write("0.0.0.0"); console_write("\n");
}

void net_ipv4_init(void){ (void)netface_get_mac(g_mac); g_ip=0; g_mask=0; g_gw=0; for(int i=0;i<8;i++) g_arp[i].used=0; }

// Build and send ARP reply or request
// sip/tip are big-endian (network-order) 32-bit values
static void send_arp_reply(const uint8_t* sha, const uint8_t* tha, uint32_t sip_be, uint32_t tip_be, int is_reply){
    uint8_t f[42]; // 14 eth + 28 arp
    (void)sha; // quiet unused
    // Ethernet
    for(int i=0;i<6;i++) f[i] = tha ? tha[i] : 0xFF; // dst (reply to THA or broadcast for request)
    for(int i=0;i<6;i++) f[6+i] = g_mac[i];
    f[12]=0x08; f[13]=0x06; // ARP
    // ARP
    f[14]=0x00; f[15]=0x01; // HTYPE Ethernet
    f[16]=0x08; f[17]=0x00; // PTYPE IPv4
    f[18]=0x06; f[19]=0x04; // HLEN=6, PLEN=4
    f[20]= is_reply ? 0x00 : 0x00; f[21]= is_reply ? 0x02 : 0x01; // OPER reply/request
    // SHA,SIP,THA,TIP
    for(int i=0;i<6;i++) f[22+i] = g_mac[i];
    // Emit IPv4 addresses from big-endian 32-bit: high→low bytes
    f[28]= (uint8_t)(sip_be>>24); f[29]=(uint8_t)(sip_be>>16); f[30]=(uint8_t)(sip_be>>8); f[31]=(uint8_t)(sip_be);
    for(int i=0;i<6;i++) f[32+i] = tha ? tha[i] : 0x00;
    f[38]=(uint8_t)(tip_be>>24); f[39]=(uint8_t)(tip_be>>16); f[40]=(uint8_t)(tip_be>>8); f[41]=(uint8_t)(tip_be);
    netface_send(f, 42);
}

static void send_arp_request(uint32_t target_ip){ send_arp_reply(NULL, NULL, g_ip, target_ip, 0); }

// Send Ethernet+IPv4 payload
bool net_ipv4_send(uint32_t dst_ip, uint8_t proto, const uint8_t* payload, uint16_t plen){
    uint8_t dst_mac[6]; uint32_t target = dst_ip;
    // Choose next-hop (gateway if outside subnet)
    if (g_mask && g_ip && ((dst_ip & g_mask) != (g_ip & g_mask)) && g_gw) target = g_gw;
    if (!arp_cache_lookup(target, dst_mac)) { send_arp_request(target); return false; }

    // Build frame: Ethernet + IPv4 + payload
    uint8_t buf[14+20+1500]; if (plen>1500) plen=1500;
    for (int i=0;i<6;i++){ buf[i]=dst_mac[i]; buf[6+i]=g_mac[i]; }
    buf[12]=0x08; buf[13]=0x00; // IPv4
    // IPv4 header
    uint8_t* ip = buf+14;
    ip[0]=0x45;                 // Version 4, IHL=5 (no options)
    ip[1]=0;                    // DSCP/ECN
    uint16_t tot= (uint16_t)(20+plen);
    ip[2]=(uint8_t)(tot>>8);    // Total length (BE)
    ip[3]=(uint8_t)tot;
    ip[4]=0; ip[5]=0;           // Identification
    ip[6]=0x40; ip[7]=0;        // Flags/fragment offset: DF set, offset 0
    ip[8]=64;                   // TTL
    ip[9]=proto;                // Protocol
    ip[10]=0; ip[11]=0;         // Header checksum (zero for calc)
    // Source/Destination IPv4 (big-endian values → high byte first)
    ip[12]=(uint8_t)(g_ip>>24); ip[13]=(uint8_t)(g_ip>>16); ip[14]=(uint8_t)(g_ip>>8); ip[15]=(uint8_t)g_ip;
    ip[16]=(uint8_t)(dst_ip>>24); ip[17]=(uint8_t)(dst_ip>>16); ip[18]=(uint8_t)(dst_ip>>8); ip[19]=(uint8_t)dst_ip;
    // Compute header checksum
    uint16_t c=csum16(ip,20); ip[10]=(uint8_t)(c>>8); ip[11]=(uint8_t)c;
    // payload
    for (uint16_t i=0;i<plen;i++) buf[14+20+i]=payload[i];
    return netface_send(buf, (uint16_t)(14+20+plen));
}

// ICMP echo reply to incoming
static void icmp_reply(const uint8_t* ip, const uint8_t* icmp, uint16_t icmp_len){
    uint8_t rep[1500]; if (icmp_len>1500) icmp_len=1500;
    for (uint16_t i=0;i<icmp_len;i++) rep[i]=icmp[i];
    rep[0]=0; rep[1]=0; rep[2]=0; rep[3]=0; // type=0 code=0 csum=0 for calc
    uint16_t c = csum16(rep, icmp_len); rep[2]=(uint8_t)(c>>8); rep[3]=(uint8_t)c;
    // destination for reply is the original source IP (big-endian 32-bit)
    uint32_t src = ((uint32_t)ip[12]<<24)|((uint32_t)ip[13]<<16)|((uint32_t)ip[14]<<8)|((uint32_t)ip[15]);
    (void)net_ipv4_send(src, 1, rep, icmp_len);
}

void net_ipv4_on_frame(const uint8_t* frame, uint16_t len){
    if (len < 14) return;
    uint16_t eth = ((uint16_t)frame[12] << 8) | frame[13];
    if (eth == 0x0806) {
        if (len < 42) return; // ARP
        const uint8_t* arp = frame+14;
        uint16_t op = ((uint16_t)arp[6]<<8)|arp[7];
        // Parse sender/target IPv4 as big-endian 32-bit
        uint32_t sip = ((uint32_t)arp[14]<<24)|((uint32_t)arp[15]<<16)|((uint32_t)arp[16]<<8)|((uint32_t)arp[17]);
        uint32_t tip = ((uint32_t)arp[24]<<24)|((uint32_t)arp[25]<<16)|((uint32_t)arp[26]<<8)|((uint32_t)arp[27]);
        // learn sender
        arp_cache_learn(sip, arp+8);
        // if ARP request for us, reply to sender MAC (ARP SHA)
        if (op==1 && tip==g_ip) send_arp_reply(NULL, arp+8, g_ip, sip, 1);
        return;
    } else if (eth == 0x0800) {
        if (len < 34) return; // IPv4
        const uint8_t* ip = frame+14;
        if ((ip[0]>>4)!=4) return;
        uint8_t ihl = (uint8_t)((ip[0]&0x0F)*4);
        if (len < 14+ihl) return;
        uint32_t dst = ((uint32_t)ip[16]<<24)|((uint32_t)ip[17]<<16)|((uint32_t)ip[18]<<8)|((uint32_t)ip[19]);
        if (dst != g_ip && g_ip!=0) return; // not for us (ignore broadcast handling for now)
        uint8_t proto = ip[9];
        if (proto == 1) { // ICMP
            const uint8_t* icmp = ip+ihl; uint16_t icmp_len = (uint16_t)(len - 14 - ihl);
            if (icmp_len >= 8 && icmp[0]==8) { icmp_reply(ip, icmp, icmp_len); }
        } else if (proto == 6) { // TCP (route to minimal TCP)
            extern void net_tcp_on_ipv4(const uint8_t* ip, uint16_t ip_len);
            uint16_t ip_total = (uint16_t)(((uint16_t)ip[2]<<8) | ip[3]);
            if (ip_total >= ihl) net_tcp_on_ipv4(ip, ip_total);
        }
    }
}

bool net_icmp_ping(uint32_t dst_ip_be, int count, uint32_t timeout_ms){
    if (count<=0) count=1;
    if (timeout_ms==0) timeout_ms=1000;
    static uint16_t ident=0x4242; uint16_t seq=0;
    for (int n=0;n<count;n++){
        // build echo request
        uint8_t pkt[64]; for (int i=0;i<64;i++) pkt[i]=0;
        pkt[0]=8; pkt[1]=0; pkt[2]=0; pkt[3]=0; pkt[4]=(uint8_t)(ident>>8); pkt[5]=(uint8_t)ident; pkt[6]=(uint8_t)(seq>>8); pkt[7]=(uint8_t)seq;
        for (int i=8;i<16;i++) pkt[i]=(uint8_t)i;
        uint16_t c=csum16(pkt, 16); pkt[2]=(uint8_t)(c>>8); pkt[3]=(uint8_t)c;
        (void)net_ipv4_send(dst_ip_be, 1, pkt, 16);
        // wait for reply by monitoring ARP cache and status bar? Simplify: spin delay.
        uint32_t start=platform_ticks_get(); uint32_t hz = platform_timer_get_hz(); (void)hz;
        uint32_t elapsed_ms=0; while (elapsed_ms < timeout_ms){ netface_poll(); elapsed_ms = (platform_ticks_get()-start) * (1000u / (hz?hz:1u)); }
        seq++;
    }
    return true;
}
