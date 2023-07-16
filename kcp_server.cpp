#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "kcp_util.h"

uv_timer_t watch_timer;

void watch_cb(uv_timer_t* handle) {
    printf("%lu\n", uv_now(uv_default_loop()));
    printf("recv: %ld, recv_bytes: %ld, send: %ld, send_bytes: %ld\n", recv_count, recv_bytes, send_count, send_bytes);
}

int kcp_output(const char *buf, int len, ikcpcb *kcp, void *user) {
    KcpContext * kcp_ctx = (KcpContext *)user;
    return send_kcp_packet(kcp_ctx->udp_handle, &kcp_ctx->addr.addr, buf, len);
}

void udp_recv_cb(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags) {
    if (nread < 0) {
        printf("udp_recv_cb error: %s\n", uv_strerror(nread));
        return;
    }

    recv_count += 1;
    recv_bytes += nread;
    
    if (nread < 5) {
        // printf("udp_recv_cb nread: %ld\n", nread);
        return;
    }

    IUINT32 conv_or_key;
    decode32u(buf->base, &conv_or_key);

    if (nread < 24) { // udp
        char cmd = buf->base[4];
        if (cmd == 1) { // SYN
            IUINT32 conv = ++s_conv;
            KcpContext * kcp_ctx = new KcpContext(handle, addr, conv, kcp_output);
            if (!kcp_ctx) {
                printf("new KcpContext error\n");
                return;
            }

            if (!kcp_ctx->IsCreated()) {
                delete kcp_ctx;
                return;
            }

            kcp_map.emplace(std::make_pair(conv, kcp_ctx->kcp));
            printf("syn %u %u\n", conv, conv_or_key);
            send_udp_packet(handle, addr, 2, conv ^ conv_or_key); // ACK

        } else if (cmd == 2) { // ACK

        } else if (cmd == 3) { // FIN
            auto it = kcp_map.find(conv_or_key);
            if (it == kcp_map.end()) {
                printf("udp_recv_cb not exist conv: %u\n", conv_or_key);
                return;
            }

            KcpContext * kcp_ctx = (KcpContext *)it->second->user;
            delete kcp_ctx;
            kcp_map.erase(it);

        } else {
            printf("udp_recv_cb unknown udp packet: %ld\n", nread);
        }

    } else { // kcp
        // printf("kcp recv: %u, %ld\n", conv_or_key, nread);
        auto it = kcp_map.find(conv_or_key);
        if (it == kcp_map.end()) {
            printf("udp_recv_cb not exist conv: %u\n", conv_or_key);
            send_udp_packet(handle, addr, 3, conv_or_key);
            return;
        }

        nread = ikcp_input(it->second, buf->base, nread);
        if (nread < 0) {
            printf("ikcp_input error: %ld\n", nread);
            KcpContext * kcp_ctx = (KcpContext *)it->second->user;
            delete kcp_ctx;
            kcp_map.erase(it);
            send_udp_packet(handle, addr, 3, it->second->conv);
            return;
        }

        KcpContext * kcp_ctx = (KcpContext *)it->second->user;
        kcp_ctx->last_recv_time = (uint32_t)uv_now(uv_default_loop());

        while (true) { // echo 回显
            nread = ikcp_recv(it->second, buf->base, buf->len);
            if (nread < 0) {
                if (nread != -1) {
                    printf("ikcp_recv error: %ld\n", nread);
                }
                return;
            }

            nread = ikcp_send(it->second, buf->base, nread);
            if (nread < 0) {
                printf("ikcp_send error: %ld\n", nread);
                return;
            }
        }
    }
}

int main(int argc, const char ** argv) {
    printf("kcp server pid: %d\n", uv_os_getpid());
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

    create_timer(&watch_timer, watch_cb, 10000, 10000);
    struct timespec begin;
    struct timespec end;
    clock_gettime(CLOCK_REALTIME, &begin);
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);
    clock_gettime(CLOCK_REALTIME, &end);
    printf("clear resource %ld.%ld\n", end.tv_sec, end.tv_nsec);
    double use_time = calc_time(&begin, &end);
    printf("clear resource recv: %ld, recv_bytes: %ld, send: %ld, send_bytes: %ld\n", recv_count, recv_bytes, send_count, send_bytes);
    printf("clear resource per second recv: %lf, recv_bytes: %lf, send: %lf, send_bytes: %lf\n", (double)recv_count / use_time, (double)recv_bytes / use_time, (double)send_count / use_time, (double)send_bytes / use_time);

    for (auto & it: kcp_map) {
        KcpContext * kcp_ctx = (KcpContext *)it.second->user;
        kcp_ctx->CloseUpdateTimer();
        kcp_ctx->CloseSendTimer();
    }

    uv_walk(uv_default_loop(), walk_cb, NULL);
    uv_run(uv_default_loop(), UV_RUN_ONCE);
    uv_loop_close(uv_default_loop());

    for (auto & it: kcp_map) {
        KcpContext * kcp_ctx = (KcpContext *)it.second->user;
        delete kcp_ctx;
    }
    kcp_map.clear();

    return 0;
}
