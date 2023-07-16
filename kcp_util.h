#include "uv.h"
#include "ikcp.h"
#include <string.h>
#include <unordered_map>
#include <algorithm>

uint32_t s_conv = 1000;
std::unordered_map<IUINT32, ikcpcb *> kcp_map;
uv_timer_t stop_timer;

int64_t recv_count = 0;
int64_t recv_bytes = 0;

int64_t send_count = 0;
int64_t send_bytes = 0;

typedef int (*output_cb)(const char *buf, int len, ikcpcb *kcp, void *user);

typedef struct udp_req {
    uv_udp_send_t req;
    char buf[1];
} udp_req;

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

double calc_time(struct timespec * begin, struct timespec * end) {
    long seconds = end->tv_sec - begin->tv_sec;
    long nanoseconds = end->tv_nsec - begin->tv_nsec;
    double elapsed = seconds + nanoseconds * 1e-9;
    return elapsed;
}

int create_timer(uv_timer_t * handle, uv_timer_cb cb, uint64_t timeout, uint64_t repeat) {
    int err = uv_timer_init(uv_default_loop(), handle);
    if (0 != err) {
        printf("uv_timer_init error: %s\n", uv_strerror(err));
        return err;
    }

    err = uv_timer_start(handle, cb, timeout, repeat);
    if (0 != err) {
        printf("uv_timer_start error: %s\n", uv_strerror(err));
        return err;
    }

    return 0;
}

// 信号

void stop_cb(uv_timer_t* handle) {
    uv_stop(uv_default_loop());
}

void signal_cb(uv_signal_t* handle, int signum) {
    uv_stop(uv_default_loop());
    create_timer(&stop_timer, stop_cb, 10, 0);
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

// 发送udp数据包

void udp_send_cb(uv_udp_send_t* req, int status) {
    if (0 != status) {
        printf("udp_send_cb error: %s\n", uv_strerror(status));
    }
    udp_req * r = (udp_req *)req->data;
    free(r);
}

int send_udp_packet(uv_udp_t * handle, const sockaddr *addr, char cmd, IUINT32 conv) {
    printf("send_udp_packet %d %u\n", cmd, conv);
    udp_req * req = (udp_req *)malloc(sizeof(udp_req) + 5);
    req->req.data = req;

    char * ptr = req->buf;
    ptr = encode32u(ptr, conv);
    ptr = encode8u(ptr, cmd);
    uv_buf_t buf = uv_buf_init(req->buf, 5);

    int err = uv_udp_send(&req->req, handle, &buf, 1, addr, udp_send_cb);
    if (0 != err) {
        printf("uv_udp_send error: %s\n", uv_strerror(err));
    } else {
        send_count += 1;
        send_bytes += 5;
    }

    return err;
}

int send_kcp_packet(uv_udp_t * handle, const sockaddr *addr, const char *buffer, int len) {
    uint32_t conv;
    decode32u(buffer, &conv);
    // printf("send_kcp_packet %u %d\n", conv, len);
    udp_req * req = (udp_req *)malloc(sizeof(udp_req) + len);
    req->req.data = req;

    memcpy(req->buf, buffer, len);
    uv_buf_t buf = uv_buf_init(req->buf, len);

    int err = uv_udp_send(&req->req, handle, &buf, 1, addr, udp_send_cb);
    if (0 != err) {
        printf("uv_udp_send error: %s\n", uv_strerror(err));
    } else {
        send_count += 1;
        send_bytes += len;
    }

    return err;
}

void alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    static char buffer[64 * 1024];
    *buf = uv_buf_init(buffer, sizeof(buffer));
}

void walk_cb(uv_handle_t* handle, void* arg) {
    printf("walk_cb: %s\n", uv_handle_type_name(handle->type));
    if (uv_is_closing(handle) == 0) {
        uv_close(handle, NULL);
    }
}

class KcpContext {
public:
    KcpContext(uv_udp_t * handle, const struct sockaddr * src_addr, uint32_t conv, output_cb cb)
        : udp_handle(handle), update_timer(nullptr), send_timer(nullptr), kcp(nullptr), last_recv_time(0) {
        if (src_addr->sa_family == AF_INET) {
            memcpy(&addr.in, src_addr, sizeof(addr.in));
        } else {
            memcpy(&addr.in6, src_addr, sizeof(addr.in6));
        }

        kcp = ikcp_create(conv, this);
        if (!kcp) {
            printf("ikcp_create error\n");
            return;
        }

        ikcp_nodelay(kcp, 1, 10, 2, 1);
        ikcp_wndsize(kcp, 1024, 1024);
        ikcp_setoutput(kcp, cb);
        kcp->stream = 1;

        // uint32_t current_time = (uint32_t)uv_now(uv_default_loop());
        // uint32_t next_time = ikcp_check(kcp, current_time);
        update_timer = (uv_timer_t *)malloc(sizeof(uv_timer_t));
        if (!update_timer) {
            printf("malloc update_timer error\n");
            return;
        }

        update_timer->data = this;
        // create_timer(update_timer, UpdateTimerCb, std::min(next_time - current_time, (uint32_t)1), kcp->interval);
        create_timer(update_timer, UpdateTimerCb, kcp->interval, kcp->interval);
    }

    ~KcpContext() {
        CloseUpdateTimer();
        CloseSendTimer();

        if (kcp) {
            ikcp_release(kcp);
            kcp = nullptr;
        }
    }

    bool IsCreated() const {
        return kcp != nullptr && update_timer != nullptr;
    }

    bool IsTimeout() const {
        if (last_recv_time == 0) {
            return false;
        }

        return (uint32_t)uv_now(uv_default_loop()) - last_recv_time > 60000;
    }

    void CloseUpdateTimer() {
        if (update_timer) {
            uv_close((uv_handle_t *)update_timer, CloseTimerCb);
            update_timer = nullptr;
        }
    }

    void CloseSendTimer() {
        if (send_timer) {
            uv_close((uv_handle_t *)send_timer, CloseTimerCb);
            send_timer = nullptr;
        }
    }

    int StartSend() {
        send_timer = (uv_timer_t *)malloc(sizeof(uv_timer_t));
        if (!send_timer) {
            printf("malloc send_timer error\n");
            return -1;
        }

        send_timer->data = this;
        return create_timer(send_timer, SendTimerCb, 10, 1);
    }

protected:
    static void CloseTimerCb(uv_handle_t * handle) {
        free(handle);
    }

    static void UpdateTimerCb(uv_timer_t * handle) {
        KcpContext * kcp_ctx = (KcpContext *)handle->data;
        uint32_t current_time = (uint32_t)uv_now(uv_default_loop());
        ikcp_update(kcp_ctx->kcp, current_time);

        // uint32_t next_time = ikcp_check(kcp_ctx->kcp, current_time);
        // int err = uv_timer_start(kcp_ctx->update_timer, UpdateTimerCb, std::min(next_time - current_time, (uint32_t)1), kcp_ctx->kcp->interval);
        // if (0 != err) {
        //     printf("uv_timer_start error: %s\n", uv_strerror(err));
        // }

        if (kcp_ctx->IsTimeout()) {
            send_udp_packet(kcp_ctx->udp_handle, &kcp_ctx->addr.addr, 3, kcp_ctx->kcp->conv);
            auto it = kcp_map.find(kcp_ctx->kcp->conv);
            if (it == kcp_map.end()) {
                printf("UpdateTimerCb not exist conv: %u\n", kcp_ctx->kcp->conv);
                return;
            }
            delete kcp_ctx;
            kcp_map.erase(it);
        }
    }

    static void SendTimerCb(uv_timer_t * handle) {
        KcpContext * kcp_ctx = (KcpContext *)handle->data;
        int num = rand() % 10;
        for (int i = 0; i <= num; i++) {
            ikcp_send(kcp_ctx->kcp, "12345678", 8);
        }
    }

public:
    uv_udp_t * udp_handle;
    uv_timer_t * update_timer;
    uv_timer_t * send_timer;
    ikcpcb * kcp;
    union {
        struct sockaddr_in6 in6;
        struct sockaddr_in in;
        struct sockaddr addr;
    } addr;
    uint32_t last_recv_time;
};
