#include <errno.h>
#include <stdio.h>
#include "utils.h"

int ip_addr(const char * ip, int port, struct sockaddr_in * addr) {
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);
    addr->sin_addr.s_addr = inet_addr(ip);
    // addr->sin_addr.s_addr = htonl(INADDR_ANY);
    if (addr->sin_addr.s_addr == INADDR_NONE) {
        return -1;
    }
    return 0;
}

int ip_name(const struct sockaddr_in * addr, char * ip, size_t size) {
    char * ip_addr = inet_ntoa(addr->sin_addr);
    if (ip_addr == NULL) {
        return -1;
    }
    strncpy(ip, ip_addr, size);
    return 0;
}

int thread_create(pthread_t * thread, thread_func func, void * arg) {
    if (-1 == pthread_create(thread, NULL, func, arg)) {
        printf("pthread_create error: %d\n", errno);
        return -1;
    }
    if (-1 == pthread_detach(*thread)) {
        printf("pthread_detach error: %d\n", errno);
        return -1;
    }
    return 0;
}
