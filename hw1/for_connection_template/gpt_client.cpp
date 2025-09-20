// mini_telnet.cpp
#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstring>

using namespace std;

int main(int argc, char *argv[]) {
    

    string server_ip = argv[1];
    int port = stoi(argv[2]);

    // 1. Create socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        cerr << "Failed to create socket\n";
        return -1;
    }

    // 2. Server address
    sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip.c_str(), &server.sin_addr) <= 0) {
        cerr << "Invalid IP address\n";
        return -2;
    }

    // 3. Connect
    if (connect(sockfd, (sockaddr*)&server, sizeof(server)) == -1) {
        cerr << "Connection failed\n";
        close(sockfd);
        return -3;
    }

    cout << "Connected to " << server_ip << ":" << port << endl;

    // 4. Multiplex stdin and socket using select()
    fd_set master, readfds;
    FD_ZERO(&master);
    FD_SET(STDIN_FILENO, &master); // keyboard
    FD_SET(sockfd, &master);       // server
    int fdmax = max(STDIN_FILENO, sockfd);

    char buf[4096];
    while (true) {
        readfds = master;
        if (select(fdmax + 1, &readfds, NULL, NULL, NULL) == -1) {
            cerr << "select() failed\n";
            break;
        }

        // Check stdin
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            string input;
            if (!getline(cin, input)) {
                cout << "EOF on stdin, closing\n";
                break;
            }
            input += "\n"; // telnet sends newline
            send(sockfd, input.c_str(), input.size(), 0);
        }

        // Check socket
        if (FD_ISSET(sockfd, &readfds)) {
            memset(buf, 0, sizeof(buf));
            int bytesRecv = recv(sockfd, buf, sizeof(buf), 0);
            if (bytesRecv <= 0) {
                cout << "Server disconnected\n";
                break;
            }
            cout << string(buf, 0, bytesRecv);
            cout.flush();
        }
    }

    close(sockfd);
    return 0;
}
