#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include "udp_socket.h"

int Setsockopt(int __fd, int __level, int __optname, const void *__optval, socklen_t __optlen) {
    if (-1 == setsockopt(__fd, __level, __optname, __optval, __optlen)) {
        close(__fd);
        printf("setsockopt error: %d\n", errno);
        return -1;
    }
    return 0;
}

int Getsockopt(int __fd, int __level, int __optname, void * __optval, socklen_t * __optlen) {
    if (-1 == getsockopt(__fd, __level, __optname, __optval, __optlen)) {
        close(__fd);
        printf("getsockopt error: %d\n", errno);
        return -1;
    }
    return 0;
}

double calc_time(struct timespec * begin, struct timespec * end) {
    long seconds = end->tv_sec - begin->tv_sec;
    long nanoseconds = end->tv_nsec - begin->tv_nsec;
    double elapsed = seconds + nanoseconds * 1e-9;
    return elapsed;
}

int udp_create_socket() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (-1 == sockfd) {
        printf("socket error: %d\n", errno);
        return -1;
    }

    int on = 1;
    int opt_val;
    socklen_t opt_len = sizeof(opt_val);
    if (-1 == Setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) {
        return -1;
    }
    if (-1 == Getsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt_val, &opt_len)) {
        return -1;
    }
    printf("SO_REUSEADDR: %d\n", opt_val);

    // if (-1 == Setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on))) {
    //     return -1;
    // }
    // if (-1 == Getsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &opt_val, &opt_len)) {
    //     return -1;
    // }
    // printf("SO_REUSEPORT: %d\n", opt_val);

    if (-1 == Getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &opt_val, &opt_len)) {
        return -1;
    }
    printf("SO_RCVBUF: %d\n", opt_val);

    on = 1024 * 1024;
    if (-1 == Setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &on, sizeof(on))) {
        return -1;
    }
    if (-1 == Getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &opt_val, &opt_len)) {
        return -1;
    }
    printf("SO_RCVBUF: %d\n", opt_val);

    if (-1 == Getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &opt_val, &opt_len)) {
        return -1;
    }
    printf("SO_SNDBUF: %d\n", opt_val);

    on = 1024 * 1024;
    if (-1 == Setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &on, sizeof(on))) {
        return -1;
    }
    if (-1 == Getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &opt_val, &opt_len)) {
        return -1;
    }
    printf("SO_SNDBUF: %d\n", opt_val);

    return sockfd;
}

int udp_bind(int sockfd, struct sockaddr_in * addr) {
    if (-1 == bind(sockfd, (const struct sockaddr *)addr, sizeof(*addr))) {
        close(sockfd);
        printf("bind error: %d\n", errno);
        return -1;
    }
    return 0;
}

int udp_connect(int sockfd, struct sockaddr_in * addr) {
    if (-1 == connect(sockfd, (const struct sockaddr *)addr, sizeof(*addr))) {
        close(sockfd);
        printf("connect error: %d\n", errno);
        return -1;
    }
    return 0;
}

void udp_send_forever(int sockfd, int block, const char * buf, int len, int * running) {
    int ret;
    long long count = 0;
    long long bytes_count = 0;
    struct timespec begin;
    struct timespec end;
    int flag = block == 1 ? 0 : MSG_DONTWAIT;
    clock_gettime(CLOCK_REALTIME, &begin);
    printf("start send: %ld.%ld\n", begin.tv_sec, begin.tv_nsec);
    while (*running) {
        ret = send(sockfd, buf, len, flag);
        if (-1 == ret) {
            if (block) {
                printf("send error: %d\n", errno);
            }
        } else {
            count += 1;
            bytes_count += ret;
        }
    }
    clock_gettime(CLOCK_REALTIME, &end);
    printf("end send: %ld.%ld\n", end.tv_sec, end.tv_nsec);
    double use_time = calc_time(&begin, &end);
    printf("use send: %lf, send: %lld, sendBytes: %lld, sendPerSecond: %lf, sendBytesPerSecond: %lf\n", use_time, count, bytes_count, (double)count / use_time, (double)bytes_count / use_time);
}

void udp_recv_forever(int sockfd, int block, int * running) {
    int ret;
    char buf[65535];
    long long count = 0;
    long long bytes_count = 0;
    struct timespec begin;
    struct timespec end;
    int flag = block == 1 ? 0 : MSG_DONTWAIT;
    clock_gettime(CLOCK_REALTIME, &begin);
    printf("start recv: %ld.%ld\n", begin.tv_sec, begin.tv_nsec);
    while (*running) {
        ret = recv(sockfd, buf, 65535, flag);
        if (-1 == ret) {
            if (block) {
                printf("recv error: %d\n", errno);
            }
        } else {
            count += 1;
            bytes_count += ret;
        }
    }
    clock_gettime(CLOCK_REALTIME, &end);
    printf("end recv: %ld.%ld\n", end.tv_sec, end.tv_nsec);
    double use_time = calc_time(&begin, &end);
    printf("use recv: %lf, recv: %lld, recvBytes: %lld, recvPerSecond: %lf, recvBytesPerSecond: %lf\n", use_time, count, bytes_count, (double)count / use_time, (double)bytes_count / use_time);
}

void udp_sendto_forever(int sockfd, int block, const char * buf, int len, struct sockaddr_in * addr, int * running) {
    int ret;
    char ip[20];
    long long count = 0;
    long long bytes_count = 0;
    struct timespec begin;
    struct timespec end;
    int flag = block == 1 ? 0 : MSG_DONTWAIT;
    clock_gettime(CLOCK_REALTIME, &begin);
    printf("start sendto: %ld.%ld\n", begin.tv_sec, begin.tv_nsec);
    while (*running) {
        ret = sendto(sockfd, buf, len, flag, (const struct sockaddr *)addr, sizeof(*addr));
        if (-1 == ret) {
            if (block) {
                if (-1 == ip_name(addr, ip, 20)) {
                    printf("ip_name error: %d\n", errno);
                } else {
                    printf("sendto %s error: %d\n", ip, errno);
                }
            }
        } else {
            count += 1;
            bytes_count += ret;
        }
    }
    clock_gettime(CLOCK_REALTIME, &end);
    printf("end sendto: %ld.%ld\n", end.tv_sec, end.tv_nsec);
    double use_time = calc_time(&begin, &end);
    printf("use sendto: %lf, send: %lld, sendBytes: %lld, sendPerSecond: %lf, sendBytesPerSecond: %lf\n", use_time, count, bytes_count, (double)count / use_time, (double)bytes_count / use_time);
}

void udp_recvfrom_forever(int sockfd, int block, int * running) {
    int ret;
    char buf[65535];
    struct sockaddr_in client_addr;
    socklen_t client_addrlen = sizeof(client_addr);
    // char ip[20];
    long long count = 0;
    long long bytes_count = 0;
    struct timespec begin;
    struct timespec end;
    int flag = block == 1 ? 0 : MSG_DONTWAIT;
    clock_gettime(CLOCK_REALTIME, &begin);
    printf("start recvfrom: %ld.%ld\n", begin.tv_sec, begin.tv_nsec);
    while (*running) {
        ret = recvfrom(sockfd, buf, 65535, flag, (struct sockaddr *)&client_addr, &client_addrlen);
        if (-1 == ret) {
            if (block) {
                printf("recvfrom error: %d\n", errno);
            }
        // } else if (-1 == ip_name(&client_addr, ip, 20)) {
        //     printf("ip_name error: %d\n", errno);
        } else {
            // printf("recvfrom addr: %s\n", ip);
            count += 1;
            bytes_count += ret;
        }
    }
    clock_gettime(CLOCK_REALTIME, &end);
    printf("end recvfrom: %ld.%ld\n", end.tv_sec, end.tv_nsec);
    double use_time = calc_time(&begin, &end);
    printf("use recvfrom: %lf, recv: %lld, recvBytes: %lld, recvPerSecond: %lf, recvBytesPerSecond: %lf\n", use_time, count, bytes_count, (double)count / use_time, (double)bytes_count / use_time);
}
