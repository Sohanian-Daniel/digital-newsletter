#include "helpers.h"
#include <arpa/inet.h>
#include <exception>
#include <netinet/in.h>
#include <poll.h>
#include <queue>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std;

// Custom exceptions for my findClient method
class ClientNotFound : public exception {
    const char *what() const noexcept override {
        return "Client Not Found\n";
    }
};

class ClientSocketDisconnected : public exception {
public:
    int index;
    const char *what() const noexcept override {
        return "Client Socket Disconnected\n";
    }

    explicit ClientSocketDisconnected(int _index) {
        index = _index;
    }
};

// Client class to hold info
// cli_addr and clilen aren't required but could be useful if this
// was a real app that could require more stuff later
class Client {
public:
    // File descriptor for the client
    int fd;

    // ID of client
    string id;

    // Client IP and length
    struct sockaddr_in cli_addr{};
    socklen_t clilen;

    // A Map to keep track which Topics
    // are subscribed with SF and which aren't
    unordered_map<string, int> topics;

    // findClient has 2 overloads for searching by fd or id
    // returns the index where the client can be found or an exception
    // ClientDisconnected containing the index where the client can be found
    // ClientNotFound if the client doesn't exist with specified search param
    static int findClient(vector<Client *> clients, int fd) {
        for (int i = 0; i < clients.size(); i++) {
            if (fd == clients[i]->fd) {
                if (clients[i]->fd == CLIENT_DISCONNECTED) {
                    throw ClientSocketDisconnected(i);
                }
                return i;
            }
        }
        throw ClientNotFound();
    }

    static int findClient(vector<Client *> clients, const string& id) {
        for (int i = 0; i < clients.size(); i++) {
            if (id == clients[i]->id) {
                if (clients[i]->fd == CLIENT_DISCONNECTED) {
                    throw ClientSocketDisconnected(i);
                }
                return i;
            }
        }
        throw ClientNotFound();
    }

    // Decouples the client from the list, doesn't free the client memory
    static void removeClient(vector<Client *> &clients, Client *toRemove) {
        for (int i = 0; i < clients.size(); i++) {
            if (toRemove->id == clients[i]->id) {
                clients.erase(clients.begin() + i);
            }
        }
    }

    // Simple Constructor
    Client(int _fd, string _id, struct sockaddr_in _cli_addr, socklen_t _clilen) {
        fd = _fd;
        id = string(std::move(_id));
        cli_addr = _cli_addr;
        clilen = _clilen;
    }
};

int main(int argc, char *argv[]) {
    // Disable print buffering
    setvbuf(stdout, nullptr, _IONBF, BUFSIZ);

    // file descriptors
    int sockfd, udpfd, newsockfd, portno;

    // ip addresses
    struct sockaddr_in serv_addr{}, cli_addr{};
    socklen_t clilen = sizeof(cli_addr);

    // buffer
    char buffer[BUFLEN];

    // misc variables, i is index, n is number of byes read
    // ret is used for error handling, run_server determines
    // when the server needs to stop running
    int i, ret, run_server = 1;
    ssize_t n;

    // Check usage
    if (argc < 2) {
        fprintf(stderr, "Usage: %s server_port\n", argv[0]);
        return 0;
    }

    // UDP listen fd
    udpfd = socket(AF_INET, SOCK_DGRAM, 0);
    DIE(udpfd < 0, "ERROR: Couldn't open UDP fd.\n");

    // TCP listen fd
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    DIE(sockfd < 0, "ERROR: Couldn't open TCP listen fd.\n");

    // Sets a socket bunch of options
    set_socket_options(sockfd);

    // Set fd to be non blocking.
    int enable = 1;
    ret = ioctl(sockfd, FIONBIO, &enable);
    DIE(ret < 0, "ERROR: ioctl");

    // Server port
    portno = atoi(argv[1]);
    DIE(portno < 0, "ERROR: Bad port.\n");

    // Fill out server address info
    memset((char *)&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portno);
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind both TCP and UDP sockets
    ret = bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr));
    DIE(ret < 0, "ERROR: Couldn't bind.\n");

    ret = bind(udpfd, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr));
    DIE(ret < 0, "ERROR: Couldn't bind.\n");

    // Listen on the TCP fd
    ret = listen(sockfd, QUEUE_LEN);
    DIE(ret < 0, "ERROR: Couldn't listen.\n");

    // Vector of clients, I'm using this like a list (but faster)
    // I admit that I don't particularly need constant time access,
    // but it's faster than linked lists
    vector<Client *> clients;

    // Vector of file descriptors, I'm using poll() instead of select()
    // therefore I'm using the required struct (pollfd)
    // new_fd creates a new struct pollfd with the given params
    vector<struct pollfd> fds;
    fds.push_back(new_fd(sockfd, POLLIN));
    fds.push_back(new_fd(udpfd, POLLIN));
    fds.push_back(new_fd(fileno(stdin), POLLIN));

    // Topic Map keeps a list of Clients subscribed to a certain topic
    // Makes finding and sending the messages to the appropiate clients fast.
    unordered_map<string, vector<Client *>> topic_map;

    // Store and Forward map, keeps a queue of packets for the clients
    // that subscribed with the store and forward option
    unordered_map<Client *, queue<packet *>> sf_map;

    while (run_server) {
        // poll the file descriptors for which is active
        ret = poll(&fds[0], fds.size(), 0);
        DIE(ret < 0, "poll");

        // check what happened to each one
        for (i = 0; i < fds.size(); i++) {
            // if no event, we can skip
            if (fds[i].revents == 0) {
                continue;
            }

            // STDIN file descriptor active
            if (fds[i].fd == fileno(stdin)) {
                // Read from STDIN and remove trailing \r\n's if they exist
                memset(buffer, 0, BUFLEN);
                DIE(!fgets(buffer, BUFLEN - 1, stdin), "ERROR: fgets.\n");
                buffer[strcspn(buffer, "\r\n")] = 0;

                // The only command is exit
                if (!strncmp(buffer, "exit", 4)) {
                    run_server = 0;
                    break;
                }
            }

            // TCP listen active
            else if (fds[i].fd == sockfd) {
                // New client is connecting, accept his connection
                clilen = sizeof(cli_addr);
                newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
                DIE(newsockfd < 0, "accept");

                // Enable socket options
                set_socket_options(newsockfd);

                // As per protocol, client must send his ID when connecting
                memset(buffer, 0, BUFLEN);
                n = recv_variable(newsockfd, buffer, sizeof(buffer));
                DIE(n < 0, "recv");

                // No need to parse the struct since it's just a byte array.
                string id = string(buffer, n);

                try {
                    // If findClient doesn't throw an exception, the client
                    // is already conncted.
                    Client::findClient(clients, id);

                    // Not getting an exception means client already connected
                    printf("Client %s already connected.\n", id.c_str());

                    // Generate a Server-TCP Client packet with data type REPLY
                    packet reply;
                    reply.data_t = PACKET_REPLY;
                    strcpy(reply.payload, "ERRSAMEID");
                    n = send_packet(newsockfd, (char *)&reply, sizeof(reply));
                    DIE(n < 0, "error send");
                    break;
                } catch (ClientSocketDisconnected &ex) {
                    // If the Client exists but is disconnected, we update his fd
                    // And we check if he had any topic subscribed with SF
                    clients[ex.index]->fd = newsockfd;

                    try {
                        // If this throws an exception it means
                        // the client never subscribed with SF
                        auto queue = sf_map.at(clients[ex.index]);

                        // Regardless if there's an exception or not,
                        // nothing will happen if there IS a bucket
                        // with no packets since we check if the queue is empty
                        while (!queue.empty()) {
                            // Extract, send and free packets
                            packet *p = queue.front();
                            n = send_packet(newsockfd, (char *)p, sizeof(packet));
                            DIE(n < 0, "error send");
                            free(p);
                            queue.pop();
                        }

                        // Update the value of the SF Map with the queue
                        sf_map[clients[ex.index]] = queue;
                    } catch (exception &ex) {
                        // client had no packets to be sent to him, simply continue
                    }
                    // No client with this ID ever existed, create new Client instance
                } catch (ClientNotFound &ex) {
                    // create new Client instance
                    clients.push_back(new Client(newsockfd, id, cli_addr, clilen));
                }
                // If we made it here it means that either the Client wasn't found
                // Or his file descriptor was updated accordingly

                // Add the new file descriptor to the list
                fds.push_back(new_fd(newsockfd, POLLIN));

                // Print to stdout
                printf("New client %s connected from %s:%u.\n",
                       id.c_str(), inet_ntoa(cli_addr.sin_addr),
                       ntohs(cli_addr.sin_port));
            }

            // UDP fd active
            else if (fds[i].fd == udpfd) {
                // Receive UDP packet (UDP is packet oriented, so all is good)
                memset(buffer, 0, BUFLEN);
                clilen = sizeof(cli_addr);
                n = recvfrom(udpfd, buffer, sizeof(buffer), 0,
                             (struct sockaddr *)&cli_addr, &clilen);
                DIE(n < 0, "recvfrom");

                // Extract topic from the packet
                auto *info = (packet *)malloc(sizeof(packet));
                memcpy(info, buffer, n);

                // Fill out information about who sent packet
                info->cli_addr = cli_addr;

                // Extract list of subscribers to this particular topic
                vector<Client *> subscribers;
                try {
                    subscribers = topic_map.at(info->topic);
                } catch (exception &ex) {
                    break;
                }

                // Forward packet to all subscribers of the given topic
                for (auto client : subscribers) {
                    // If the client isn't disconnected
                    if (client->fd != -1) {
                        n = send_packet(client->fd, (char *)info, sizeof(packet));
                        DIE(n < 0, "error send");
                    }
                    // if SF is enabled and client is disconnected store the packet
                    if (client->topics[info->topic] == 1 && client->fd == -1) {
                        // Get the queue of packets to be sent to this particular client
                        // If it exists, if it doesn't this makes a new one regardless
                        queue<packet *> to_send;
                        try {
                            to_send = sf_map.at(client);
                        } catch (exception &ex) {
                            // well, nothing really.
                        }
                        // Make a copy of the packet and add it to the queue
                        auto *copy = new packet();
                        memcpy(copy, info, sizeof(packet));

                        // Add the packet to the queue and update the value of the map
                        to_send.push(copy);
                        sf_map[client] = to_send;
                    }
                }
                // Free the packet, this is why we made a copy for the queued packets
                free(info);

            } else {
                // We received data from one of our TCP Clients
                // As per protocol receive a client_packet
                // Containing the command
                memset(buffer, 0, BUFLEN);
                n = recv_variable(fds[i].fd, buffer, sizeof(buffer));
                DIE(n < 0, "recv");

                // Connection closed
                if (n == 0) {
                    // no error handling since there CANT be an error here
                    // the client definitely exists
                    int index = Client::findClient(clients, fds[i].fd);
                    printf("Client %s disconnected.\n", clients[index]->id.c_str());

                    // Remove the file descriptor from the list and close the fd
                    fds.erase(fds.begin() + i);
                    close(fds[i].fd);

                    // set fd of client to DISCONNECTED
                    // since clients are pointers, this will update clients in
                    // topic_map and sf_map as well, reduces complexity by a bit
                    clients[index]->fd = CLIENT_DISCONNECTED;
                    break;
                }

                // Otherwise, we actually received data
                // Start parsing the command
                char *token = strtok(buffer, " ");
                if (!strcmp(token, "subscribe")) {
                    // Get the topic from the command
                    token = strtok(nullptr, " ");
                    string topic = string(token);

                    // Extract the list of clients subscribed to this topic
                    vector<Client *> subscribers;
                    try {
                        subscribers = topic_map.at(topic);
                    } catch (exception &ex) {
                        // do nothing
                    }
                    // No error handling, Client HAS to exist
                    int index = Client::findClient(clients, fds[i].fd);

                    // Check if the client is already subscribed
                    int subscribed = 0;
                    for (const auto& client_topics : clients[index]->topics) {
                        if (client_topics.first == topic) {
                            subscribed = 1;
                            break;
                        }
                    }
                    // usually we would also notify the client he is already
                    // subscribed, but it's not part of the assignment
                    if (subscribed)
                        break;

                    // Add the client to the list of subscribers on this topic
                    subscribers.push_back(clients[index]);

                    // Add the topic to the client's list of topics with the
                    // specified option
                    token = strtok(nullptr, " ");
                    int option = atoi(token);
                    DIE(option != 1 && option != 0, "ERROR: bad option");
                    clients[index]->topics.insert(make_pair(topic, option));

                    // Update the bucket with the list of subscribers
                    topic_map[string(topic)] = subscribers;

                    // Generate new Server->Client Reply, notifying that the operation
                    // was successful then send the packet
                    packet reply;
                    reply.data_t = PACKET_REPLY;
                    strcpy(reply.payload, "Subscribed to topic.\n");
                    n = send_packet(clients[index]->fd, (char *)&reply, sizeof(reply));
                    DIE(n < 0, "error send");
                }

                // Unsubscribe command
                if (!strcmp(token, "unsubscribe")) {
                    // Get the topic from the command
                    token = strtok(nullptr, " ");

                    // Extract the list of clients subscribed to this topic
                    vector<Client *> subscribers;
                    try {
                        subscribers = topic_map.at(string(token));
                    } catch (exception &ex) {
                        // Normally a stderr print that topic doesn't exist
                        break;
                    }

                    // Get the client from the list of clients,
                    // No error handling, this has to exist
                    int index = Client::findClient(clients, fds[i].fd);

                    // Check if the client is already subscribed
                    int subscribed = 0;
                    for (const auto& topic : clients[index]->topics) {
                        if (topic.first == string(token)) {
                            subscribed = 1;
                            break;
                        }
                    }
                    // If not subscribed, nothing to do here
                    if (!subscribed)
                        break;

                    // Remove the client from the list of subscribers on this topic
                    Client::removeClient(subscribers, clients[index]);

                    // Remove this topic from the client's list of topics
                    clients[index]->topics.erase(string(token));

                    // Update the bucket with the new list of subscribers
                    topic_map[string(token)] = subscribers;

                    // Notify client with a REPLY that the operation
                    // was successful and is unsubscribed
                    packet reply;
                    reply.data_t = PACKET_REPLY;
                    strcpy(reply.payload, "Unsubscribed from topic.\n");
                    n = send_packet(clients[index]->fd, (char *)&reply, sizeof(reply));
                    DIE(n < 0, "error send");
                }
            }
        }
    }

    // close remaining fds
    for (auto pollfd : fds) {
        shutdown(pollfd.fd, SHUT_RDWR);
        close(pollfd.fd);
    }

    // free any packets that were saved but didn't get sent
    // (client sub to SF, disconnected and never reconnected)
    for (const auto& entry : sf_map) {
        auto queue = entry.second;
        while (!queue.empty()) {
            packet *p = queue.front();
            free(p);
            queue.pop();
        }
    }

    // free memory allocated to clients
    for (auto client : clients) {
        free(client);
    }
    return 0;
}
