#include <iostream>
#include <fstream>
#include<sys/types.h>
#include <sys/epoll.h>
#include<unistd.h>
#include<sys/socket.h>
#include<netdb.h>
#include <fcntl.h>
#include<arpa/inet.h>
#include <string.h>
#include <string>
#include <netinet/in.h>
#include <csignal> // For signal() and SIGINT
#include "utility.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;
using namespace std;

const int GAME_SERVER_PORT=45632;
const int DATA_SERVER_PORT=45631;
// const char *IP="140.113.17.11";
const char *IP="127.0.0.1";

std::unordered_map<int, json> users;
std::unordered_map<int, json> rooms;
std::unordered_map<int, json> gameLogs;

// when ctrl+c 

void save_to_disk(const std::string& filename, const json& data) {
    std::ofstream file(filename);
    file << data.dump(2);
}

json load_from_disk(const std::string& filename) {
    std::ifstream file(filename);
    json data;
    file >> data;
    return data;
}

void signal_handler(int signum) {
    cout << "Caught signal " << signum << " (SIGINT). Exiting gracefully." << std::endl;
    // Perform any necessary cleanup before exiting
    exit(signum); 
}



int main() {

    if (signal(SIGINT, signal_handler) == SIG_ERR) {
        printf("Failed to caught signal\n");
    }//if other, then data is not saved

    int sockfd=socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd==-1){
        cerr<<"failed to create socket"<<endl;
        return -1;
    }
    sockaddr_in server;

    server.sin_family=AF_INET;
    server.sin_port=htons(DATA_SERVER_PORT);
    inet_pton(AF_INET, IP, &server.sin_addr);

    if(connect(sockfd,(sockaddr*)&server, sizeof(server))==-1){
        cerr<<"connection failed"<<endl;
        return -2;
    }

    cout << "Connected to server. Type messages and press Enter.\n";
    fd_set master, readfd;
    FD_ZERO(&master);
    FD_SET(sockfd, &master);
    int fdmax = max(STDIN_FILENO, sockfd);
    char buf[4096];
    string input;

    while(1){
        readfd=master;
        if(select(fdmax+1,&readfd,NULL,NULL,NULL)==-1){
            cerr<<"select failed"<<endl;
            continue;
        }
        if(FD_ISSET(sockfd,&readfd)){
            auto msgOpt = recv_message(sockfd);
            if (!msgOpt.size()) return;//failed to get message

            try {
                json request = json::parse(msgOpt);
                std::string action = request["action"];
                json payload = request["payload"];

                if (action == "getUser") {
                    int id = payload["id"];
                    if (users.contains(id)) {
                        send_message(sock, json({{"status","ok"}, {"data", users[id]}}).dump());
                    } else {
                        send_message(sock, json({{"status","error"}, {"message","User not found"}}).dump());
                    }
                }

            } catch (std::exception& e) {
                send_message(sock, json({{"status","error"},{"message", e.what()}}).dump());
            }

        }
        
    }
    close(sockfd);
    return 0;
}
