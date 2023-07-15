#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include "udp_socket.h"

static int running = 1;

void signal_int(int signum) {
    running = 0;
}

void * thread_run(void * arg) {
    struct thread_data * data = (struct thread_data *)arg;
    udp_recv_forever(data->sockfd, data->block, &running);
    return NULL;
}

void * thread_send_run(void * arg) {
    struct thread_data * data = (struct thread_data *)arg;
    udp_send_forever(data->sockfd, data->block, "12345678", 1, &running);
    return NULL;
}

int main(int argc, const char ** argv) {
    printf("pid: %d\n", getpid());
    if (argc != 4) {
        printf("usage: udp_client ip port thread_num\n");
        return 0;
    }

    int block = 0;  // 阻塞
    int thread_num = atoi(argv[3]); // 线程数
    struct sockaddr_in addr;
    if (-1 == ip_addr(argv[1], atoi(argv[2]), &addr)) {
        printf("ip_addr error: %d\n", errno);
        return 0;
    }

    int sockfd = udp_create_socket();
    if (-1 == sockfd) {
        return 0;
    }

    // if (-1 == udp_connect(sockfd, &addr)) {
    //     return 0;
    // }

    signal(SIGINT, signal_int);

    // pthread_t send_thread;
    // struct thread_data data;
    // data.sockfd = sockfd;
    // data.block = block;
    // memcpy(&data.addr, &addr, sizeof(addr));
    // if (-1 == thread_create(&send_thread, thread_run, &data)) {
    //     close(sockfd);
    //     return 0;
    // }
    pthread_t send_threads[thread_num];
    struct thread_data data[thread_num];
    for (int i = 0; i < thread_num; i++) {
        data[i].sockfd = udp_create_socket();
        data[i].block = block;
        memcpy(&(data[i].addr), &addr, sizeof(addr));
        udp_connect(data[i].sockfd, &addr);
        thread_create(&send_threads[i], thread_send_run, &data[i]);
    }

    // udp_send_forever(sockfd, block, "12345678", 8, &running);
    udp_recvfrom_forever(sockfd, 0, &running);
    sleep(2);
    close(sockfd);
    for (int i = 0; i < thread_num; i++) {
        close(data[i].sockfd);
    }
    return 0;
}
