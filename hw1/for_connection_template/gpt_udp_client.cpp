#include <iostream>
#include <cstring>
#include <thread>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024

void receiveMessages(int sockfd, sockaddr_in &servaddr) {
    char buffer[BUFFER_SIZE];
    socklen_t len = sizeof(servaddr);

    while (true) {
        int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&servaddr, &len);
        if (n > 0) {
            buffer[n] = '\0';
            std::cout << "\nServer: " << buffer << std::endl;
            std::cout << "You: " << std::flush;
        }
    }
}

int main() {
    int sockfd;
    sockaddr_in servaddr{};

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return 1;
    }

    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Server IP

    std::cout << "Connected to UDP Chat Server\n";

    // Start receiving thread
    std::thread recvThread(receiveMessages, sockfd, std::ref(servaddr));

    // Sending messages
    std::string msg;
    socklen_t len = sizeof(servaddr);
    while (true) {
        std::cout << "You: ";
        std::getline(std::cin, msg);
        sendto(sockfd, msg.c_str(), msg.size(), 0, (struct sockaddr*)&servaddr, len);
    }

    recvThread.join();
    close(sockfd);
    return 0;
}
