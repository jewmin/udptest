#ifndef __UDP_SOCKET_INCLUDED__
#define __UDP_SOCKET_INCLUDED__

#include "utils.h"

int udp_create_socket();
int udp_bind(int sockfd, struct sockaddr_in * addr);
int udp_connect(int sockfd, struct sockaddr_in * addr);
void udp_send_forever(int sockfd, int block, const char * buf, int len, int * running);
void udp_recv_forever(int sockfd, int block, int * running);
void udp_sendto_forever(int sockfd, int block, const char * buf, int len, struct sockaddr_in * addr, int * running);
void udp_recvfrom_forever(int sockfd, int block, int * running);

#endif
