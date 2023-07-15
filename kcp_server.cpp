#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "include/uv.h"
#include "ikcp.h"
#include <unordered_map>
#include <string.h>

static IUINT32 s_conv = 1000;
static std::unordered_map<IUINT32, ikcpcb *> kcp_map;

typedef struct udp_req {
    uv_udp_send_t req;
    char buf[1];
} udp_req;

typedef struct kcp_context {
    ikcpcb * kcp;
    uv_udp_t * handle;
    union {
        struct sockaddr_in6 in6;
        struct sockaddr_in in;
        struct sockaddr addr;
    } addr;
    uv_timer_t update_timer;
} kcp_context;

/* encode 8 bits unsigned int */
static inline char *encode8u(char *p, unsigned char c)
{
	*(unsigned char*)p++ = c;
	return p;
}

/* decode 8 bits unsigned int */
static inline const char *decode8u(const char *p, unsigned char *c)
{
	*c = *(unsigned char*)p++;
	return p;
}

/* encode 32 bits unsigned int (lsb) */
static inline char *encode32u(char *p, IUINT32 l)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*(unsigned char*)(p + 0) = (unsigned char)((l >>  0) & 0xff);
	*(unsigned char*)(p + 1) = (unsigned char)((l >>  8) & 0xff);
	*(unsigned char*)(p + 2) = (unsigned char)((l >> 16) & 0xff);
	*(unsigned char*)(p + 3) = (unsigned char)((l >> 24) & 0xff);
#else
	memcpy(p, &l, 4);
#endif
	p += 4;
	return p;
}

/* decode 32 bits unsigned int (lsb) */
static inline const char *decode32u(const char *p, IUINT32 *l)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*l = *(const unsigned char*)(p + 3);
	*l = *(const unsigned char*)(p + 2) + (*l << 8);
	*l = *(const unsigned char*)(p + 1) + (*l << 8);
	*l = *(const unsigned char*)(p + 0) + (*l << 8);
#else 
	memcpy(l, p, 4);
#endif
	p += 4;
	return p;
}

void signal_cb(uv_signal_t* handle, int signum) {
    uv_stop(uv_default_loop());
}

int set_signal_handle(uv_signal_t* handle, int signum) {
    int err = uv_signal_init(uv_default_loop(), handle);
    if (0 != err) {
        printf("uv_signal_init error: %s\n", uv_strerror(err));
        return err;
    }

    err = uv_signal_start(handle, signal_cb, signum);
    if (0 != err) {
        printf("uv_signal_start error: %s\n", uv_strerror(err));
        return err;
    }

    return 0;
}

void udp_send_cb(uv_udp_send_t* req, int status) {
    if (0 != status) {
        printf("udp_send_cb error: %s\n", uv_strerror(status));
    }
    udp_req * r = (udp_req *)req->data;
    free(r);
}

void send_udp_packet(uv_udp_t * handle, const sockaddr *addr, char cmd, IUINT32 conv) {
    udp_req * req = (udp_req *)malloc(sizeof(udp_req) + 5);
    req->req.data = req;

    char * ptr = req->buf;
    ptr = encode32u(ptr, conv);
    ptr = encode8u(ptr, cmd);
    uv_buf_t buf = uv_buf_init(req->buf, 5);

    int err = uv_udp_send(&req->req, handle, &buf, 1, addr, udp_send_cb);
    if (0 != err) {
        printf("uv_udp_send error: %s\n", uv_strerror(err));
    }
}

void send_kcp_packet(uv_udp_t * handle, const sockaddr *addr, const char *buffer, int len) {
    udp_req * req = (udp_req *)malloc(sizeof(udp_req) + len);
    req->req.data = req;

    memcpy(req->buf, buffer, len);
    uv_buf_t buf = uv_buf_init(req->buf, len);

    int err = uv_udp_send(&req->req, handle, &buf, 1, addr, udp_send_cb);
    if (0 != err) {
        printf("uv_udp_send error: %s\n", uv_strerror(err));
    }
}

int kcp_output(const char *buf, int len, ikcpcb *kcp, void *user) {
    kcp_context * kcp_ctx = (kcp_context *)user;
    send_kcp_packet(kcp_ctx->handle, &kcp_ctx->addr.addr, buf, len);
}

void timer_cb(uv_timer_t* handle) {
    kcp_context * kcp_ctx = (kcp_context *)handle->data;
    IUINT32 current_time = (IUINT32)uv_now(uv_default_loop());
    ikcp_update(kcp_ctx->kcp, current_time);
    IUINT32 next_time = ikcp_check(kcp_ctx->kcp, current_time);
    int err = uv_timer_start(&kcp_ctx->update_timer, timer_cb, next_time - current_time, kcp_ctx->kcp->interval);
    if (0 != err) {
        printf("uv_timer_start error: %s\n", uv_strerror(err));
        return;
    }
}

void alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    static char buffer[64 * 1024];
    *buf = uv_buf_init(buffer, sizeof(buffer));
}

void udp_recv_cb(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags) {
    if (nread < 0) {
        printf("udp_recv_cb error: %s\n", uv_strerror(nread));
        return;
    } else if (nread < 5) {
        printf("udp_recv_cb unknown udp packet: %d", nread);
        return;
    }

    IUINT32 conv_or_key;
    decode32u(buf->base, &conv_or_key);

    if (nread < 24) { // udp
        char cmd = buf->base[4];
        if (cmd == 1) { // SYN
            IUINT32 conv = ++s_conv;
            kcp_context * kcp_ctx = (kcp_context *)malloc(sizeof(kcp_context));
            kcp_ctx->handle = handle;
            if (addr->sa_family == AF_INET) {
                memcpy(&kcp_ctx->addr.in, addr, sizeof(kcp_ctx->addr.in));
            } else {
                memcpy(&kcp_ctx->addr.in6, addr, sizeof(kcp_ctx->addr.in6));
            }
            kcp_ctx->kcp = ikcp_create(conv, kcp_ctx);
            ikcp_nodelay(kcp_ctx->kcp, 1, 10, 2, 1);
            ikcp_wndsize(kcp_ctx->kcp, 1024, 1024);
            ikcp_setoutput(kcp_ctx->kcp, kcp_output);
            kcp_ctx->kcp->stream = 1;
            kcp_map.emplace(std::make_pair(conv, kcp_ctx->kcp));
            send_udp_packet(handle, addr, 2, conv ^ conv_or_key); // ACK

            // 添加update定时器
            IUINT32 current_time = (IUINT32)uv_now(uv_default_loop());
            IUINT32 next_time = ikcp_check(kcp_ctx->kcp, current_time);
            int err = uv_timer_init(uv_default_loop(), &kcp_ctx->update_timer);
            if (0 != err) {
                printf("uv_timer_init error: %s\n", uv_strerror(err));
                return;
            }

            kcp_ctx->update_timer.data = kcp_ctx;
            err = uv_timer_start(&kcp_ctx->update_timer, timer_cb, next_time - current_time, kcp_ctx->kcp->interval);
            if (0 != err) {
                printf("uv_timer_start error: %s\n", uv_strerror(err));
                return;
            }

        } else if (cmd == 2) { // ACK

        } else if (cmd == 3) { // FIN
            auto it = kcp_map.find(conv_or_key);
            if (it == kcp_map.end()) {
                printf("udp_recv_cb not exist conv: %u", conv_or_key);
                return;
            }

            free(it->second->user);
            ikcp_release(it->second);
            kcp_map.erase(it);

        } else {
            printf("udp_recv_cb unknown udp packet: %d", nread);
        }

    } else { // kcp
        auto it = kcp_map.find(conv_or_key);
        if (it == kcp_map.end()) {
            printf("udp_recv_cb not exist conv: %u", conv_or_key);
            return;
        }

        nread = ikcp_input(it->second, buf->base, nread);
        if (nread < 0) {
            printf("ikcp_input error: %d\n", nread);
            free(it->second->user);
            ikcp_release(it->second);
            kcp_map.erase(it);
            send_udp_packet(handle, addr, 3, it->second->conv);
            return;
        }

        while (true) { // echo 回显
            nread = ikcp_recv(it->second, buf->base, buf->len);
            if (nread < 0) {
                if (nread != -1) {
                    printf("ikcp_recv error: %d\n", nread);
                }
                return;
            }

            nread = ikcp_send(it->second, buf->base, nread);
            if (nread < 0) {
                printf("ikcp_send error: %d\n", nread);
                return;
            }
        }
    }
}

void walk_cb(uv_handle_t* handle, void* arg) {
    printf("walk_cb: %s\n", uv_handle_type_name(handle->type));
    if (uv_is_closing(handle) == 0) {
        uv_close(handle, NULL);
    }
}

int main(int argc, const char ** argv) {
    printf("pid: %d\n", uv_os_getpid());
    if (argc < 2) {
        printf("usage: kcp_server port\n");
        return 0;
    }

    int port = atoi(argv[1]);

    int err;
    struct sockaddr_in addr;
    err = uv_ip4_addr("0.0.0.0", port, &addr);
    if (0 != err) {
        printf("uv_ip4_addr error: %s\n", uv_strerror(err));
        return 0;
    }

    uv_udp_t udp_server;
    err = uv_udp_init(uv_default_loop(), &udp_server);
    if (0 != err) {
        printf("uv_udp_init error: %s\n", uv_strerror(err));
        return 0;
    }

    err = uv_udp_bind(&udp_server, (const sockaddr *)&addr, 0);
    if (0 != err) {
        printf("uv_udp_bind error: %s\n", uv_strerror(err));
        return 0;
    }

    uv_signal_t signal_int, signal_term;
    if (0 != set_signal_handle(&signal_int, SIGINT)) {
        return 0;
    }
    if (0 != set_signal_handle(&signal_term, SIGTERM)) {
        return 0;
    }

    err = uv_udp_recv_start(&udp_server, alloc_cb, udp_recv_cb);
    if (0 != err) {
        printf("uv_udp_recv_start error: %s\n", uv_strerror(err));
        return 0;
    }

    uv_run(uv_default_loop(), UV_RUN_DEFAULT);

    uv_walk(uv_default_loop(), walk_cb, NULL);
    uv_run(uv_default_loop(), UV_RUN_ONCE);
    uv_loop_close(uv_default_loop());

    for (auto & it: kcp_map) {
        free(it.second->user);
        ikcp_release(it.second);
    }
    kcp_map.clear();

    return 0;
}
