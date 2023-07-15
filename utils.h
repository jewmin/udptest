#ifndef __ADDRESS_INCLUDED__
#define __ADDRESS_INCLUDED__

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>

struct thread_data {
    int sockfd;
    struct sockaddr_in addr;
    int block;
};

typedef void * (*thread_func)(void * arg);

int ip_addr(const char * ip, int port, struct sockaddr_in * addr);
int ip_name(const struct sockaddr_in * addr, char * ip, size_t size);

int thread_create(pthread_t * thread, thread_func func, void * arg);

#endif
