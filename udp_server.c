#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include "udp_socket.h"

static int running = 1;

void signal_int(int signum) {
    running = 0;
}

void * thread_run(void * arg) {
    struct thread_data * data = (struct thread_data *)arg;
    // udp_sendto_forever(data->sockfd, data->block, "12345678", 8, &data->addr, &running);
    udp_send_forever(data->sockfd, data->block, "12345678", 8, &running);
    return NULL;
}

void * thread_recv_run(void * arg) {
    struct thread_data * data = (struct thread_data *)arg;
    udp_recv_forever(data->sockfd, data->block, &running);
    return NULL;
}

int main(int argc, const char ** argv) {
    printf("pid: %d\n", getpid());

    int block = 1;  // 阻塞
    struct sockaddr_in addr;
    if (-1 == ip_addr("0.0.0.0", 7777, &addr)) {
        printf("ip_addr error: %d\n", errno);
        return 0;
    }

    int sockfd = udp_create_socket();
    if (-1 == sockfd) {
        return 0;
    }

    if (-1 == udp_bind(sockfd, &addr)) {
        return 0;
    }

    signal(SIGINT, signal_int);

    char buf[65535];
    struct sockaddr_in client_addr;
    socklen_t client_addrlen = sizeof(client_addr);
    char ip[20];
    int ret = recvfrom(sockfd, buf, 65535, 0, (struct sockaddr *)&client_addr, &client_addrlen);
    if (-1 == ret) {
        printf("recvfrom error: %d\n", errno);
    } else if (-1 == ip_name(&client_addr, ip, 20)) {
        printf("ip_name error: %d\n", errno);
    } else {
        printf("recvfrom addr: %s\n", ip);
        // int new_sockfd = udp_create_socket();
        // udp_bind(new_sockfd, &addr);
        // udp_connect(new_sockfd, &client_addr);
        // udp_connect(sockfd, &client_addr);

        // pthread_t send_thread;
        // struct thread_data data;
        // data.sockfd = sockfd;
        // data.block = block;
        // memcpy(&data.addr, &client_addr, client_addrlen);
        // if (-1 == thread_create(&send_thread, thread_run, &data)) {
        //     close(sockfd);
        //     return 0;
        // }

        udp_recvfrom_forever(sockfd, block, &running);
        // udp_recv_forever(sockfd, block, &running);
    }
    sleep(2);
    close(sockfd);
    return 0;
}

// int main(int argc, const char ** argv) {
//     printf("pid: %d\n", getpid());
//     int block = 0;  // 阻塞
//     struct sockaddr_in addr;
//     ip_addr("0.0.0.0", 7777, &addr);
//     int listenfd = udp_create_socket();
//     udp_bind(listenfd, &addr);

//     signal(SIGINT, signal_int);

//     char buf[65535];
//     pthread_t recv_thread[65535];
//     struct thread_data recv_data[65535];
//     struct sockaddr_in client_addr;
//     socklen_t client_addrlen = sizeof(client_addr);
//     char ip[20];
//     int i = 0;
//     addr.sin_port = htons(0);
//     while (running) {
//         int ret = recvfrom(listenfd, buf, 65535, 0, (struct sockaddr *)&client_addr, &client_addrlen);
//         ip_name(&client_addr, ip, 20);
//         printf("recvfrom %s:%d\n", ip, ntohs(client_addr.sin_port));
//         int new_sockfd = udp_create_socket();
//         udp_bind(new_sockfd, &addr);
//         udp_connect(new_sockfd, &client_addr);
//         recv_data[i].sockfd = new_sockfd;
//         recv_data[i].block = block;
//         memcpy(&(recv_data[i].addr), &client_addr, client_addrlen);
//         thread_create(&recv_thread[i], thread_recv_run, &recv_data[i]);
//         i++;
//     }
//     sleep(2);
//     close(listenfd);
//     for (int j = 0; j < i; j++) {
//         close(recv_data[j].sockfd);
//     }
// }
