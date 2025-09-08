#include "tcp_min.h"
#include "ipv4.h"
#include "../console.h"
#include <stddef.h>
#include "../drivers/fs/neelefs.h"

// Single-connection, minimal TCP responder for HTTP/1.0 GET
// States
enum { T_CLOSED=0, T_LISTEN, T_SYN_RCVD, T_ESTABLISHED, T_LAST_ACK };

static int      s_state = T_CLOSED;
static uint16_t s_listen_port = 80;
static uint32_t s_peer_ip = 0;      // big-endian
static uint16_t s_peer_port = 0;    // host order
static uint32_t s_rcv_nxt = 0;      // next expected seq from peer
static uint32_t s_snd_iss = 0x1000; // initial send seq
static uint32_t s_snd_nxt = 0;      // next seq to send
static char     s_http_body[512] = "<html><body><h1>Mezereon</h1><p>Hello.</p></body></html>\n";
static int      s_use_file = 1;              // default to file mode (NeeleFS)
static char     s_file_path[128] = "/www/index"; // default path on NeeleFS

static uint16_t ip_csum16(const void* data, uint32_t len){
    const uint8_t* p=(const uint8_t*)data; uint32_t sum=0; while(len>1){ sum += ((uint16_t)p[0]<<8)|p[1]; p+=2; len-=2; }
    if (len) sum += ((uint16_t)p[0]<<8);
    while (sum>>16) sum = (sum & 0xFFFF) + (sum>>16);
    return (uint16_t)~sum;
}

static uint16_t tcp_checksum(uint32_t src_be, uint32_t dst_be, const uint8_t* tcp, uint16_t tcp_len){
    // Build checksum over pseudo-header + TCP header/payload using 16-bit words
    uint32_t sum = 0;
    // Pseudo-header: src IP, dst IP (each high16 then low16 in network order)
    sum += (src_be >> 16) & 0xFFFF; sum += (src_be & 0xFFFF);
    sum += (dst_be >> 16) & 0xFFFF; sum += (dst_be & 0xFFFF);
    sum += 0x0006;                   // protocol (TCP)
    sum += (uint32_t)tcp_len;        // TCP length

    // TCP segment (header + data), read as network-order 16-bit words
    const uint8_t* p = tcp; uint16_t l = tcp_len;
    while (l > 1) { sum += ((uint16_t)p[0] << 8) | p[1]; p += 2; l -= 2; }
    if (l) sum += ((uint16_t)p[0] << 8); // odd byte

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

void net_tcp_min_init(void){ s_state=T_CLOSED; s_listen_port=80; }
void net_tcp_min_listen(uint16_t port){ s_listen_port = port?port:80; s_state=T_LISTEN; }
void net_tcp_min_stop(void){ s_state=T_CLOSED; }
void net_tcp_min_set_http_body(const char* body){ if (!body) return; int i=0; while (body[i] && i<(int)sizeof(s_http_body)-1){ s_http_body[i]=body[i]; i++; } s_http_body[i]=0; }
void net_tcp_min_set_file_path(const char* path){ if (!path) return; int i=0; for (; path[i] && i< (int)sizeof(s_file_path)-1; i++) s_file_path[i]=path[i]; s_file_path[i]=0; s_use_file=1; }
void net_tcp_min_use_inline(void){ s_use_file=0; }
void net_tcp_min_status(void){ console_write("http: "); console_write(s_state==T_LISTEN?"LISTEN":s_state==T_SYN_RCVD?"SYN_RCVD":s_state==T_ESTABLISHED?"ESTABLISHED":s_state==T_LAST_ACK?"LAST_ACK":"CLOSED"); console_write(" port="); console_write_dec(s_listen_port); console_write("\n"); }

static void send_tcp(uint32_t dst_ip_be, uint16_t src_port, uint16_t dst_port, uint32_t seq, uint32_t ack, uint8_t flags, const uint8_t* data, uint16_t dlen){
    uint8_t seg[20+1460]; if (dlen>1460) dlen=1460;
    // TCP header
    seg[0]=(uint8_t)(src_port>>8); seg[1]=(uint8_t)src_port; seg[2]=(uint8_t)(dst_port>>8); seg[3]=(uint8_t)dst_port;
    seg[4]=(uint8_t)(seq>>24); seg[5]=(uint8_t)(seq>>16); seg[6]=(uint8_t)(seq>>8); seg[7]=(uint8_t)seq;
    seg[8]=(uint8_t)(ack>>24); seg[9]=(uint8_t)(ack>>16); seg[10]=(uint8_t)(ack>>8); seg[11]=(uint8_t)ack;
    seg[12]= (uint8_t)((5<<4)); // data offset=5 (20 bytes), reserved=0
    seg[13]= flags;
    seg[14]= 0x10; seg[15]= 0x00; // window 4096
    seg[16]= 0; seg[17]= 0; // checksum (to calc)
    seg[18]= 0; seg[19]= 0; // urgent ptr
    for (uint16_t i=0;i<dlen;i++) seg[20+i]=data[i];
    uint16_t tcp_len = (uint16_t)(20 + dlen);
    // obtain our IP
    uint32_t ip,mask,gw; net_ipv4_config_get(&ip,&mask,&gw);
    uint16_t c = tcp_checksum(ip, dst_ip_be, seg, tcp_len); seg[16]=(uint8_t)(c>>8); seg[17]=(uint8_t)c;
    (void)net_ipv4_send(dst_ip_be, 6, seg, tcp_len);
}

static int parse_tcp(const uint8_t* ip, uint16_t ip_len, uint16_t* sport, uint16_t* dport, uint32_t* seq, uint32_t* ack, uint8_t* flags, const uint8_t** data, uint16_t* dlen){
    if (ip_len < 20) return 0; uint8_t ihl = (uint8_t)((ip[0]&0x0F)*4); if (ip_len < ihl+20) return 0;
    const uint8_t* tcp = ip + ihl; uint16_t tcp_total = (uint16_t)(ip_len - ihl);
    *sport = ((uint16_t)tcp[0]<<8)|tcp[1]; *dport=((uint16_t)tcp[2]<<8)|tcp[3];
    *seq = ((uint32_t)tcp[4]<<24)|((uint32_t)tcp[5]<<16)|((uint32_t)tcp[6]<<8)|tcp[7];
    *ack = ((uint32_t)tcp[8]<<24)|((uint32_t)tcp[9]<<16)|((uint32_t)tcp[10]<<8)|tcp[11];
    uint8_t off = (uint8_t)((tcp[12]>>4)*4); *flags = tcp[13];
    if (tcp_total < off) return 0; *data = tcp + off; *dlen = (uint16_t)(tcp_total - off); return 1;
}

static int starts_with_get(const uint8_t* d, uint16_t n){ return n>=3 && d[0]=='G' && d[1]=='E' && d[2]=='T'; }

void net_tcp_on_ipv4(const uint8_t* ip, uint16_t ip_len){
    if (s_state==T_CLOSED) return;
    // build src/dst ip (big-endian 32-bit values)
    uint32_t src = ((uint32_t)ip[12]<<24)|((uint32_t)ip[13]<<16)|((uint32_t)ip[14]<<8)|((uint32_t)ip[15]);
    uint32_t dst = ((uint32_t)ip[16]<<24)|((uint32_t)ip[17]<<16)|((uint32_t)ip[18]<<8)|((uint32_t)ip[19]); (void)dst;
    uint16_t sport,dport; uint32_t seq,ack; uint8_t fl; const uint8_t* data; uint16_t dlen;
    if (!parse_tcp(ip, ip_len, &sport, &dport, &seq, &ack, &fl, &data, &dlen)) return;
    if (dport != s_listen_port) return;

    if (s_state == T_LISTEN) {
        if (fl & 0x02) { // SYN
            s_peer_ip = src; s_peer_port = sport; s_rcv_nxt = seq + 1; s_snd_nxt = s_snd_iss;
            send_tcp(s_peer_ip, s_listen_port, s_peer_port, s_snd_nxt, s_rcv_nxt, (uint8_t)(0x12), NULL, 0); // SYN|ACK
            s_snd_nxt++;
            s_state = T_SYN_RCVD;
        }
        return;
    }

    // Only accept traffic from the recorded peer
    if (sport != s_peer_port || src != s_peer_ip) return;

    if (s_state == T_SYN_RCVD) {
        if ((fl & 0x10) && ack == s_snd_nxt) { // ACK of our SYN
            s_state = T_ESTABLISHED;
        }
        // fall through to possible data
    }

    if (s_state == T_ESTABLISHED) {
        if (fl & 0x01) { // FIN from peer
            s_rcv_nxt = seq + 1; // ack FIN
            send_tcp(s_peer_ip, s_listen_port, s_peer_port, s_snd_nxt, s_rcv_nxt, 0x10, NULL, 0); // ACK
            s_state = T_CLOSED; return;
        }
        if (dlen > 0) {
            // advance receive next by payload length
            s_rcv_nxt = seq + dlen;
            // Select body: from file or inline
            static uint8_t bodybuf[1460];
            const uint8_t* bptr = (const uint8_t*)s_http_body;
            uint16_t body_len = 0;
            int status_200 = 1;
            if (s_use_file) {
                uint32_t out_len=0;
                if (neelefs_read_text(s_file_path, (char*)bodybuf, sizeof(bodybuf), &out_len)) {
                    bptr = bodybuf; body_len = (uint16_t)out_len; status_200 = 1;
                } else {
                    const char* nf="<html><body><h1>404 Not Found</h1></body></html>\n";
                    int i=0; while (nf[i] && i<(int)sizeof(bodybuf)) { bodybuf[i]=(uint8_t)nf[i]; i++; }
                    bptr = bodybuf; body_len = (uint16_t)i; status_200 = 0;
                }
            } else {
                while (s_http_body[body_len] && body_len < sizeof(bodybuf)) body_len++;
            }

            // emit HTTP/1.0 response
            char hdr[256]; int p=0; const char* h1 = status_200 ? "HTTP/1.0 200 OK\r\n" : "HTTP/1.0 404 Not Found\r\n";
            while (h1[p]) { hdr[p]=h1[p]; p++; }
            const char* ctype="Content-Type: text/html\r\n"; for (int i=0; ctype[i]; i++) hdr[p++]=ctype[i];
            const char* conn="Connection: close\r\n"; for (int i=0; conn[i]; i++) hdr[p++]=conn[i];
            const char* cl="Content-Length: "; for (int i=0; cl[i]; i++) hdr[p++]=cl[i];
            // content length decimal
            char num[12]; int ni=0; int v=body_len; if (v==0){ num[ni++]='0'; }
            else { char tmp[10]; int ti=0; while (v>0){ tmp[ti++]=(char)('0'+(v%10)); v/=10; } while (ti--) num[ni++]=tmp[ti]; }
            for (int i=0;i<ni;i++) hdr[p++]=num[i];
            hdr[p++]='\r'; hdr[p++]='\n'; hdr[p++]='\r'; hdr[p++]='\n';
            // build payload buffer
            static uint8_t out[1460];
            // ensure we don't exceed one TCP segment
            uint16_t max_body = (uint16_t)(sizeof(out) - p);
            if (body_len > max_body) body_len = max_body;
            for (int i=0;i<p;i++) out[i]=(uint8_t)hdr[i]; for (uint16_t i=0;i<body_len;i++) out[p+i]=bptr[i];
            uint16_t out_len=(uint16_t)(p+body_len);
            send_tcp(s_peer_ip, s_listen_port, s_peer_port, s_snd_nxt, s_rcv_nxt, 0x18, out, out_len); // PSH|ACK
            s_snd_nxt += out_len;
            // FIN
            send_tcp(s_peer_ip, s_listen_port, s_peer_port, s_snd_nxt, s_rcv_nxt, 0x11, NULL, 0); // FIN|ACK
            s_snd_nxt += 1; s_state = T_LAST_ACK;
            return;
        }
        // pure ACKs ignored
        return;
    }

    if (s_state == T_LAST_ACK) {
        if ((fl & 0x10) && ack == s_snd_nxt) { // final ACK for our FIN
            s_state = T_LISTEN; // ready for next connection
        }
        return;
    }
}
