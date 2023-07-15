#!/bin/bash

gcc -O2 udp_server.c udp_socket.c utils.c -pthread -o udp_server
gcc -O2 udp_client.c udp_socket.c utils.c -pthread -o udp_client

gcc -g udp_server.c udp_socket.c utils.c -pthread -o udp_serverd
gcc -g udp_client.c udp_socket.c utils.c -pthread -o udp_clientd
