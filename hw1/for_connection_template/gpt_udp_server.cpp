#include <iostream>
#include <cstring>
#include <thread>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024

void receiveMessages(int sockfd, sockaddr_in &cliaddr) {
    char buffer[BUFFER_SIZE];
    socklen_t len = sizeof(cliaddr);

    while (true) {
        int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&cliaddr, &len);
        if (n > 0) {
            buffer[n] = '\0';
            std::cout << "\nClient: " << buffer << std::endl;
            std::cout << "You: " << std::flush;
        }
    }
}

int main() {
    int sockfd;
    sockaddr_in servaddr{}, cliaddr{};

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return 1;
    }

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(PORT);

    if (bind(sockfd, (const struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind failed");
        close(sockfd);
        return 1;
    }

    std::cout << "UDP Chat Server started on port " << PORT << std::endl;

    // Start receiving thread
    std::thread recvThread(receiveMessages, sockfd, std::ref(cliaddr));

    // Sending messages
    std::string msg;
    socklen_t len = sizeof(cliaddr);
    while (true) {
        std::cout << "You: ";
        std::getline(std::cin, msg);
        sendto(sockfd, msg.c_str(), msg.size(), 0, (struct sockaddr*)&cliaddr, len);
    }

    recvThread.join();
    close(sockfd);
    return 0;
}
