#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <vector>
#include <algorithm>
using namespace std;

#define PORT 56432
#define MAX_CLIENTS 10

int main() {
    int listening = socket(AF_INET, SOCK_STREAM, 0);
    if (listening == -1) {
        cerr << "Socket creation failed\n";
        return -1;
    }

    sockaddr_in hint;
    hint.sin_family = AF_INET;
    hint.sin_port = htons(PORT);
    hint.sin_addr.s_addr = INADDR_ANY;

    if (bind(listening, (sockaddr*)&hint, sizeof(hint)) == -1) {
        cerr << "Bind failed\n";
        return -2;
    }

    if (listen(listening, SOMAXCONN) == -1) {
        cerr << "Listen failed\n";
        return -3;
    }

    cout << "Chat server listening on port " << PORT << "...\n";

    fd_set master, read_fds;
    FD_ZERO(&master);
    FD_SET(listening, &master);
    int fdmax = listening;

    vector<int> clients;

    while (true) {
        read_fds = master; // copy master set
        if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1) {
            cerr << "Select error\n";
            break;
        }

        for (int i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) {
                if (i == listening) {
                    // New client
                    sockaddr_in client;
                    socklen_t clientSize = sizeof(client);
                    int clientSocket = accept(listening, (sockaddr*)&client, &clientSize);
                    if (clientSocket == -1) {
                        cerr << "Accept failed\n";
                    } else {
                        FD_SET(clientSocket, &master);
                        if (clientSocket > fdmax) fdmax = clientSocket;
                        clients.push_back(clientSocket);
                        cout << "New client connected, fd=" << clientSocket << endl;
                    }
                } else {
                    // Data from client
                    char buf[4096];
                    memset(buf, 0, sizeof(buf));
                    int bytesRecv = recv(i, buf, sizeof(buf), 0);

                    if (bytesRecv <= 0) {
                        // Client disconnected
                        close(i);
                        FD_CLR(i, &master);
                        clients.erase(
                            remove(clients.begin(), clients.end(), i),
                            clients.end()
                        );

                        cout << "Client " << i << " disconnected\n";
                    } else {
                        string msg = string(buf, 0, bytesRecv);
                        cout << "Client " << i << " says: " << msg;

                        // Broadcast to all other clients
                        for (int clientSocket : clients) {
                            if (clientSocket != i) {
                                send(clientSocket, msg.c_str(), msg.size(), 0);
                            }
                        }
                    }
                }
            }
        }
    }

    close(listening);
    return 0;
}
