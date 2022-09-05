#include <netinet/tcp.h>
#include "helpers.h"

struct pollfd new_fd(int fd, short int events) {
    struct pollfd pollfd{};
    pollfd.fd = fd;
    pollfd.events = events;
    pollfd.revents = 0;
    return pollfd;
}

ssize_t send_packet(int socket, char *data, size_t data_size) {
    ssize_t sent = 0, n;
    while (sent != data_size) {
        n = send(socket, data + sent, data_size - sent, 0);
        if (n < 0)
            return -1;
        sent += n;
    }
    return sent;
}

ssize_t recv_packet(int socket, char *buffer, size_t data_size) {
    ssize_t received = 0, n;
    while (received != data_size) {
        n = recv(socket, buffer + received, data_size - received, 0);
        if (n < 0)
            return -1;
        if (n == 0)
            break;
        received += n;
    }
    return received;
}

// receives packet up to line delimitor '\n'
ssize_t recv_variable(int socket, char *buffer, size_t buffer_len) {
    ssize_t received = 0, n;
    while (!(strstr(buffer, "\n"))) {
        n = recv(socket, buffer + received, buffer_len - received, 0);
        if (n < 0)
            return -1;
        if (n == 0)
            break;
        received += n;
    }
    buffer[strcspn(buffer, "\r\n")] = 0;
    return received;
}

void set_socket_options(int sockfd) {
    // Enable a few options
    int ret, enable = 1;
    // Set fd descriptor as reusable
    ret = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
    DIE(ret < 0, "Reuseaddr failed");

    // Disable Nagle's Algorithm
    ret = setsockopt(sockfd, SOL_TCP, TCP_NODELAY, &enable, sizeof(int));
    DIE(ret < 0, "Nagle failed");

    // Disable TCP Corking
    ret = setsockopt(sockfd, SOL_TCP, TCP_CORK, &enable, sizeof(int));
    DIE(ret < 0, "Cork failed");
}