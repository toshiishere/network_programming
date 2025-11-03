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
#include <set>
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
int datafd;

vector<int> clients;
set<int> logineds;


// Set a socket to non-blocking mode
int make_socket_non_blocking(int sfd) {
    int flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1) return -1;
    flags |= O_NONBLOCK;
    return fcntl(sfd, F_SETFL, flags);
}

int logining(int fd, const std::string& action, const std::string& name, const std::string& password) {
    // Step 1. Query user info from data server
    json request = {
        {"action", "query"},
        {"type", "user"},
        {"name", name}
    };
    send_message(datafd, request.dump());

    std::string reply = recv_message(datafd);
    if (reply.empty()) {
        std::cerr << "[login] Data server read error or empty reply\n";
        json returnmsg = {{"response", "failed"}, {"reason", "data server not responding"}};
        send_message(fd, returnmsg.dump());
        return 0;
    }
    if (reply == "Disconnected") {
        std::cerr << "[login] Data server disconnected\n";
        json returnmsg = {{"response", "failed"}, {"reason", "data server disconnected"}};
        send_message(fd, returnmsg.dump());
        return 0;
    }

    json response;
    try {
        response = json::parse(reply);
    } catch (...) {
        std::cerr << "[login] Invalid JSON from data server: " << reply << "\n";
        json returnmsg = {{"response", "failed"}, {"reason", "data server invalid JSON"}};
        send_message(fd, returnmsg.dump());
        return 0;
    }

    // Step 2. Handle response
    std::string resp = response.value("response", "failed");

    if (resp == "success") {
        json user = response["data"];
        if (action == "login") {
            // ---- login existing user ----
            if (user["name"] == name &&
                user["password"] == password &&
                user["status"] == "offline")
            {
                user["last_login"] = now_time_str();
                user["status"] = "idle";

                json update = {
                    {"action", "update"},
                    {"type", "user"},
                    {"data", user}
                };
                send_message(datafd, update.dump());

                json returnmsg = {{"response", "success"}};
                send_message(fd, returnmsg.dump());
                std::cout << "[login] User '" << name << "' logged in successfully\n";
                return 1;
            } else {
                std::cerr << "[login] Player fd=" << fd << " failed: wrong password or user already online\n";
                json returnmsg = {
                    {"response", "failed"},
                    {"reason", "wrong password or already online"}
                };
                send_message(fd, returnmsg.dump());
                return 0;
            }
        } else {
            // ---- register but user exists ----
            json returnmsg = {
                {"response", "failed"},
                {"reason", "user already exists"}
            };
            send_message(fd, returnmsg.dump());
            return 0;
        }
    }
    else { // response == "failed" (user not found)
        if (action == "login") {
            json returnmsg = {
                {"response", "failed"},
                {"reason", "user does not exist"}
            };
            send_message(fd, returnmsg.dump());
            return 0;
        } else {
            // ---- register new user ----
            json user = {
                {"name", name},
                {"password", password},
                {"last_login", now_time_str()},
                {"status", "idle"}
            };

            json update = {
                {"action", "create"},
                {"type", "user"},
                {"data", user}
            };
            send_message(datafd, update.dump());

            json returnmsg = {{"response", "success"}};
            send_message(fd, returnmsg.dump());
            std::cout << "[register] New user registered: " << name << "\n";
            return 1;
        }
    }
}
/*
int logining(int fd, string action, string name, string password){
    json request={
        {"action","query"},
        {"type","user"},
        {"name",name}
    };
    send_message(datafd,request.dump());
    json response=json::parse(recv_message(datafd).c_str());
    if(response.at("response").get<string>()=="success"){
        json user=response["data"];
        if(action=="login"){//login to existed user
            if(user["name"]==name && user["password"]==password && user["status"]=="offline"){
                user["last_login"]=//to be added
                user["status"]="idle";
                json update={
                    {"action","update"},
                    {"type","user"},
                    {"data",user}
                };
                send_message(datafd,update.dump());
                json returnmsg={{"response","success"}};
                send_message(fd,returnmsg.dump());
                return 1;
            }
            else{
                cerr<<"player with fd="<<fd<<" failed to login, wrong username/passwd\n";
                json returnmsg={{"response","failed"},{"reason","wrong passwd/dulplicate user"}};
                send_message(fd,returnmsg.dump());
                return 0;
            }
        }
        else{//register existed player
            json returnmsg={{"response","failed"},{"reason","such user existed"}};
            send_message(fd,returnmsg.dump());
            return 0;
        }
    }
    else{//failed
        if(action=="login"){//such user DNE, cannot login
            json returnmsg={{"response","failed"},{"reason","such user does not existed"}};
            send_message(fd,returnmsg.dump());
            return 0;
        }
        else{//try to create new acc.
            json user={
                {"name",name},
                {"password",password},
                {"last_login",},//TODO
                {"status","idle"}
            };
            json update={
                {"action","create"},
                {"type","user"},
                {"data",user}
            };
            send_message(datafd,update.dump());
            json returnmsg={{"response","success"}};
            send_message(fd,returnmsg.dump());
            return 1;
        }
    }
}
*/
int client_request(int fd, string msg){
    json j=json::parse(msg.c_str());
    if(logineds.find(fd)!=logineds.end()){//logined
        
    }
    else{
        int result=logining(fd,
            j.at("action").get<string>(),
            j.at("name").get<string>(),
            j.at("password").get<string>()
        );
        if(result)logineds.insert(fd);
        return result;
    }
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

    //connect to data server
    datafd=socket(AF_INET, SOCK_STREAM, 0);
    if(datafd<0)throw runtime_error("failed to create socket");
    sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(DATA_SERVER_PORT);
    if (inet_pton(AF_INET, IP, &server.sin_addr) <= 0) {
        cerr << "Invalid IP address\n";
        return -2;
    }
    if (connect(datafd, (sockaddr*)&server, sizeof(server)) == -1) {
        throw runtime_error("failed to connect to data server");
    }

    int epfd = epoll_create1(0);
    if (epfd == -1) {
        perror("epoll_create1");
        return 1;
    }
    make_socket_non_blocking(listen_sock);
    make_socket_non_blocking(STDIN_FILENO);
    make_socket_non_blocking(datafd);

    epoll_event event{};
    event.data.fd = listen_sock;
    event.events = EPOLLIN;  // ready to read
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_sock, &event);

    epoll_event data_event{};
    data_event.data.fd = datafd;
    data_event.events = EPOLLIN;  // ready to read
    epoll_ctl(epfd, EPOLL_CTL_ADD, datafd, &data_event);

    epoll_event stdin_event{};
    stdin_event.data.fd = STDIN_FILENO;
    stdin_event.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &stdin_event);

    
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
                    buf[count]='\0';
                    //IO for keyboard
                }
            }
            else {
                string msg=recv_message(fd);
                if(fd==datafd){//from data server
                    if(msg=="Disconnected"){
                        close(fd);
                        cout << "data server disconnected, ready to be closed\n";
                        return 0;
                    }
                    //deal with message from data server, well but never used?
                }
                else{
                    if(msg=="Disconnected"){
                        close(fd);
                        clients.erase(std::remove(clients.begin(), clients.end(), fd), clients.end());
                        cout << "Client disconnected: " << fd << "\n";
                    }
                    //deal with client request
                    client_request(fd,msg);
                }

            }
        }
    }

    close(listen_sock);
    close(epfd);
    return 0;
}