#include "helpers.h"
#include <arpa/inet.h>
#include <iostream>
#include <netinet/tcp.h>
#include <poll.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace std;

int main(int argc, char *argv[]) {
    // Disable print buffering
    setvbuf(stdout, nullptr, _IONBF, BUFSIZ);

    // ip address of the server
    struct sockaddr_in serv_addr{};

    // misc variables, i is index, n is number of byes read
    // ret is used for error handling, run_client determines
    // when the client needs to stop running
    int sockfd, i, ret, run_client = 1;
    ssize_t n;

    // buffer
    char buffer[BUFLEN];

    // extract client id from args
    char *id = argv[1];

    // check usage
    if (argc < 4) {
        fprintf(stderr, "Usage: %s id_client ip_server port_server\n", argv[0]);
        return 0;
    }

    // Create new fd
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    DIE(sockfd < 0, "fd");

    // Enable a few options
    int enable = 1;
    // Set fd descriptor as reusable
    ret = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
    DIE(ret < 0, "Reuseaddr failed");

    // Disable Nagle's Algorithm
    ret = setsockopt(sockfd, SOL_TCP, TCP_NODELAY, &enable, sizeof(int));
    DIE(ret < 0, "Nagle failed");

    // Disable TCP Corking
    ret = setsockopt(sockfd, SOL_TCP, TCP_CORK, &enable, sizeof(int));
    DIE(ret < 0, "Cork failed");

    // Fill out server information
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[3]));
    ret = inet_aton(argv[2], &serv_addr.sin_addr);
    DIE(ret == 0, "inet_aton");

    // Attempt to connect to the server
    ret = connect(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr));
	DIE(ret < 0, "connect");

    // As per protocol, we send ID immediately after connecting
    // Appending a \n becase of its variable length
    memset(buffer, 0, BUFLEN);
    strcat(id, "\n");
    n = send_packet(sockfd, (char *)id, strlen(id));
    DIE(n < 0, "send");

    // Vector of file descriptors, I'm using poll() instead of select()
    // therefore I'm using the required struct (pollfd)
    vector<struct pollfd> fds;
    fds.push_back(new_fd(sockfd, POLLIN));
    fds.push_back(new_fd(fileno(stdin), POLLIN));

    while (run_client) {
        // poll the file descriptors for which is active
        ret = poll(&fds[0], fds.size(), 0);
        DIE(ret < 0, "select");

        // check what happened to each one
        for (i = 0; i < fds.size(); i++) {
            // if no event, we can skip
            if (fds[i].revents == 0) {
                continue;
            }

            // STDIN file descriptor active
            if (fds[i].fd == fileno(stdin)) {
                // Read from STDIN
                // No removal of trailing \r\n because of variable length
                // message, our app protocol requires \n to delimitate packet
                memset(buffer, 0, BUFLEN);
                DIE(!fgets(buffer, BUFLEN - 1, stdin), "fgets");

                // Check if we want to exit
                if (!strncmp(buffer, "exit", 4)) {
                    run_client = 0;
                    break;
                }

                // Otherwise, it's a message for the server
                n = send_packet(sockfd, buffer, strlen(buffer));
                DIE(n < 0, "send");
            }
            // Server Connection is active
            else if (fds[i].fd == sockfd) {
                // Receive information from Server
                n = recv_packet(sockfd, buffer, sizeof(packet));

                // Check if Server Shutdown
                if (n == 0) {
                    run_client = 0;
                    break;
                }
                DIE(n < 0, "Error Receive");

                // Extract packet from buffer
                auto *info = (packet *) buffer;

                // Check packet data type
                if (info->data_t == PACKET_INT) {
                    auto *p_int = (packet_int *) info->payload;

                    // Recover the value and convert to host order
                    string parse = to_string(ntohl(p_int->val));

                    // Append '-' sign if necessary
                    if (p_int->sign == 1) {
                        parse.insert(parse.begin(), '-');
                    }

                    // Print in a Human Readable way
                    printf("%s:%d - %s - INT - %s\n",
                           inet_ntoa(info->cli_addr.sin_addr),
                           ntohs(info->cli_addr.sin_port),
                           info->topic,
                           parse.c_str());
                }

                if (info->data_t == PACKET_SHORT_REAL) {
                    // Extract short_real packet from the packet
                    auto *p_short_real = (packet_short_real *) info->payload;

                    // Convert to Host order
                    p_short_real->val = ntohs(p_short_real->val);

                    // Number is the modulus times 100, so we add a dot 2 chars
                    // before the end of the end of the string
                    string decimals = to_string(p_short_real->val);
                    decimals.insert(decimals.end() - 2, '.');

                    // Print in a Human Readable way
                    printf("%s:%d - %s - SHORT_REAL - %s\n",
                           inet_ntoa(info->cli_addr.sin_addr),
                           ntohs(info->cli_addr.sin_port),
                           info->topic,
                           decimals.c_str());
                }

                if (info->data_t == PACKET_FLOAT) {
                    // Extract float packet
                    auto *p_float = (packet_float *) info->payload;

                    // Convert value to host order and save in string
                    string parse = to_string(ntohl(p_float->val));

                    // Calculate where the dot needs to be put
                    int dotPos = (int) parse.length() - p_float->power;

                    // If power is 0 we add trailing .00 to the value
                    if (p_float->power == 0) {
                        parse.append(".00");
                    }
                    // If dot pos is lower than 0 we need to pad out
                    // the decimals (0.[dotPos - 1 0's][val])
                    else if (dotPos < 0) {
                        for (int j = -dotPos - 1; j >= 0; j--) {
                            parse.insert(parse.begin(), '0');
                        }
                        parse.insert(parse.begin(), '.');
                        parse.insert(parse.begin(), '0');
                    }
                    // If dot pos is bigger, just put the dot in the correct place
                    else if (dotPos > 0) {
                        parse.insert(parse.end() - p_float->power, '.');
                    }

                    // append negative sign if required
                    if (p_float->sign == 1) {
                        parse.insert(parse.begin(), '-');
                    }

                    // print Human Readable
                    printf("%s:%d - %s - FLOAT - %s\n",
                           inet_ntoa(info->cli_addr.sin_addr),
                           ntohs(info->cli_addr.sin_port),
                           info->topic,
                           parse.c_str());
                }

                if (info->data_t == PACKET_STRING) {
                    // Strings are simple, just print them!
                    printf("%s:%d - %s - STRING - %s\n",
                           inet_ntoa(info->cli_addr.sin_addr),
                           ntohs(info->cli_addr.sin_port),
                           info->topic,
                           info->payload);
                }

                // Special data type "Packet Reply"
                // Used for data the client sends to the client to notify them
                if (info->data_t == PACKET_REPLY) {
                    // ERRSAMEID is a special code, it means connection failed
                    // And the client stops running
                    if (!strcmp(info->payload, "ERRSAMEID")) {
                        run_client = 0;
                        break;
                    }

                    // Otherwise, print the notification
                    printf("%s", info->payload);
                }
            }
        }
    }

    // close the fds, memory management for vector is handled by cpp
    for (auto pollfd : fds) {
        if (pollfd.fd >= 0) {
            shutdown(pollfd.fd, SHUT_RDWR);
            close(pollfd.fd);
        }
    }
    return 0;
}