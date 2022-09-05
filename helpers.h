#ifndef _HELPERS_H
#define _HELPERS_H 1

#include <algorithm>
#include <arpa/inet.h>
#include <exception>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

/*
 * Macro de verificare a erorilor
 * Exemplu:
 *     int fd = open(file_name, O_RDONLY);
 *     DIE(fd == -1, "open failed");
 */

#define DIE(assertion, call_description)	\
	do {									\
		if (assertion) {					\
			fprintf(stderr, "(%s, %d): ",	\
					__FILE__, __LINE__);	\
			perror(call_description);		\
			exit(EXIT_FAILURE);				\
		}									\
	} while(0)

#define BUFLEN 4096
#define QUEUE_LEN 64

#define PACKET_INT 0
#define PACKET_SHORT_REAL 1
#define PACKET_FLOAT 2
#define PACKET_STRING 3
#define PACKET_REPLY 4

#define CLIENT_DISCONNECTED -1

typedef struct __attribute__((__packed__)) packet {
    char topic[50];
    uint8_t data_t;
    char payload[1500];
    struct sockaddr_in cli_addr;
} packet;

typedef struct __attribute__((__packed__)) packet_float {
    char sign;
    uint32_t val;
    uint8_t power;
} packet_float;

typedef struct __attribute__((__packed__)) packet_short_real {
    uint16_t val;
} packet_short_real;

typedef struct __attribute__((__packed__)) packet_int {
    char sign;
    uint32_t val;
} packet_int;

struct pollfd new_fd(int fd, short int events);
ssize_t send_packet(int socket, char *data, size_t data_size);
ssize_t recv_packet(int socket, char *buffer, size_t data_size);
ssize_t recv_variable(int socket, char *buffer, size_t buffer_len);
void set_socket_options(int sockfd);

#endif
