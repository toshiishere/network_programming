#include <iostream>
#include<sys/types.h>
#include <sys/epoll.h>
#include<unistd.h>
#include<sys/socket.h>
#include<netdb.h>
#include <fcntl.h>
#include<arpa/inet.h>
#include<string.h>
#include<string>
#include <netinet/in.h>
#include "utility.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;
using namespace std;

const int GAME_SERVER_PORT=45632;
const int DATA_SERVER_PORT=45631;
// const char *IP="140.113.17.11";
const char *IP="127.0.0.1";
const int MAX_EVENTS=10;

// Set a socket to non-blocking mode
int make_socket_non_blocking(int sfd) {
    int flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1) return -1;
    flags |= O_NONBLOCK;
    return fcntl(sfd, F_SETFL, flags);
}

int main() {
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == -1) {
        perror("socket");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    inet_pton(AF_INET,IP,&addr.sin_addr);
    addr.sin_port = htons(GAME_SERVER_PORT);

    if (bind(listen_sock, (sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        return 1;
    }

    listen(listen_sock, SOMAXCONN);
    make_socket_non_blocking(listen_sock);

    int epfd = epoll_create1(0);
    if (epfd == -1) {
        perror("epoll_create1");
        return 1;
    }

    epoll_event event{};
    event.data.fd = listen_sock;
    event.events = EPOLLIN;  // ready to read
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_sock, &event);

    make_socket_non_blocking(STDIN_FILENO);
    epoll_event stdin_event{};
    stdin_event.data.fd = STDIN_FILENO;
    stdin_event.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &stdin_event);

    std::vector<int> clients;
    epoll_event events[MAX_EVENTS];

    std::cout << "Server running on port " << GAME_SERVER_PORT << "...\n";
    std::cout << "Type a message to broadcast to clients:\n";

    while (true) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;

            if (fd == listen_sock) {
                // New client connection
                sockaddr_in client_addr{};
                socklen_t client_len = sizeof(client_addr);
                int client_sock = accept(listen_sock, (sockaddr*)&client_addr, &client_len);
                make_socket_non_blocking(client_sock);

                epoll_event client_event{};
                client_event.data.fd = client_sock;
                client_event.events = EPOLLIN | EPOLLET;
                epoll_ctl(epfd, EPOLL_CTL_ADD, client_sock, &client_event);

                clients.push_back(client_sock);
                std::cout << "New client connected: " << client_sock << "\n";
            }
            else if (fd == STDIN_FILENO) {
                // Input from server console
                char buf[512];
                int count = read(STDIN_FILENO, buf, sizeof(buf));
                if (count > 0) {
                    for (int client : clients) {
                        write(client, buf, count);
                    }
                }
            }
            else {
                // Data from client
                char buf[512];
                int count = read(fd, buf, sizeof(buf));
                if (count <= 0) {
                    // Client disconnected
                    close(fd);
                    clients.erase(std::remove(clients.begin(), clients.end(), fd), clients.end());
                    std::cout << "Client disconnected: " << fd << "\n";
                } else {
                    // Echo back to sender
                    write(fd, buf, count);
                    std::cout << "Client[" << fd << "]: " << std::string(buf, count);
                }
            }
        }
    }

    close(listen_sock);
    close(epfd);
    return 0;
}