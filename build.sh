#!/bin/bash

# gcc -O2 udp_server.c udp_socket.c utils.c -pthread -o udp_server
# gcc -O2 udp_client.c udp_socket.c utils.c -pthread -o udp_client

# gcc -g udp_server.c udp_socket.c utils.c -pthread -o udp_serverd
# gcc -g udp_client.c udp_socket.c utils.c -pthread -o udp_clientd

g++ -Wall -O3 -fPIC kcp_server.cpp ikcp.c -Iinclude libuv_a.a -pthread -ldl -o kcp_server
g++ -Wall -g -fPIC kcp_server.cpp ikcp.c -Iinclude libuv_a.a -pthread -ldl -o kcp_serverd

g++ -Wall -O3 -fPIC kcp_client.cpp ikcp.c -Iinclude libuv_a.a -pthread -ldl -o kcp_client
g++ -Wall -g -fPIC kcp_client.cpp ikcp.c -Iinclude libuv_a.a -pthread -ldl -o kcp_clientd
