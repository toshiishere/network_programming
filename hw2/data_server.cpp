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

unordered_map<int, json> users;
unordered_map<int, json> rooms;
int user_cnt, room_cnt;

// when ctrl+c 
void saveUsers() {
    json data = json::array();
    for (const auto& [id, user] : users) {
        data.push_back(user);
    }
    ofstream out("data/users.json");
    if (!out.is_open()) {
        std::cerr << "Error: could not open file for writing.\n";
        return;
    }
    out << data.dump(4); // 4-space indentation for readability
    out.close();
}

unordered_map<int, json> loadUsers(const string& filename) {
    unordered_map<int, json> users;
    ifstream in(filename);

    if (!in.is_open()) {
        std::cerr << "Error: could not open " << filename << "\n";
        return users;
    }

    json data;
    in >> data; // parse the array

    for (auto user : data) {
        int id = user.at("id").get<int>();
        user["status"]="offline";
        users[id] = user;
    }
    return users;
}


void signal_handler(int signum) {
    cout << "Caught signal " << signum << " (SIGINT). Exiting gracefully." << std::endl;
    saveUsers();
    exit(signum); 
}

//return id
int op_create(string type,json data){
    if(type=="user"){
        data["id"]=user_cnt;
        users.insert({user_cnt++,data});
    }
    else if(type=="room"){
        data["id"]=room_cnt;
        rooms.insert({room_cnt++,data});
    }
    else if(type=="gamelog"){
        ofstream outfile("data/gamelog.json",ios_base::app);
        if(!outfile.is_open()){
            cerr<<"cannot open file"<<endl;
            return -1;
        }
        outfile << data.dump()<<endl;
        outfile.close();
    }
    else return -1;
    return 0;
}
json op_query(string type, string name){
    json j;
    if(type=="user"){
        for(auto tmp:users){
            if (tmp.second.at(name).get<string>()==name){
                j["response"]="success";
                j["data"]=tmp.second;
            }
        }
        if(j.empty()){
            j["response"]="failed";
            j["data"]="no such user exist";
        }
    }
    else{
        j["response"]="failed";
        j["data"]="such category doesn't support";
    }
    return j;
}
//for searching online available room/users
json op_search(string type){
    json j;
    json arr=json::array();
    if(type=="user"){
        for(auto tmp:users){
            if(tmp.second.at("status").get<string>()=="idle"){
                arr.push_back(tmp.second);
            }
        }
        if(arr.empty()){
            j["response"]="failed";
            j["data"]="no player online";
        }
        else{
            j["response"]="success";
            j["data"]=arr;
        }
    }
    else if(type=="room"){
        for(auto tmp:rooms){
            if(tmp.second.at("visibility").get<string>()=="public"){
                arr.push_back(tmp.second);
            }
        }
        if(arr.empty()){
            j["response"]="failed";
            j["data"]="no available room";
        }
        else{
            j["response"]="success";
            j["data"]=arr;
        }
    }
    else{
        j["response"]="failed";
        j["data"]="no such type supported";
    }
    return j;
}
//for change one json of room
int op_update(string type,json request){
    if(type=="room"){
        rooms[request.at("id").get<int>()]=request;
        return 1;
    }
    else return -1;
}

//for ending of room
int op_delete(string type, string name){
    if(type=="room"){
        for(auto tmp:rooms){
            if(tmp.second.get<string>()=="name"){
                rooms.erase(tmp.first);
                return 1;
            }
        }
    }
    else return -1;
}
int main() {

    if (signal(SIGINT, signal_handler) == SIG_ERR) {
        printf("Failed to caught signal\n");
    }//if other, then user data is not saved
    users=loadUsers("data/users");

    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == -1) {
        perror("socket");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    inet_pton(AF_INET,IP,&addr.sin_addr);
    addr.sin_port = htons(DATA_SERVER_PORT);

    if (bind(listen_sock, (sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        return 1;
    }
    if(listen(listen_sock,SOMAXCONN)==-1){
        cerr<<"cannot listen";
        return -3;
    }

    cout << "created data server. waiting for game server to connet\n";

    sockaddr_in client;
    socklen_t clientSize = sizeof(client);
    int sockfd=accept(listen_sock,(sockaddr*)&client, &clientSize);
    if(sockfd==-1){
        cerr<<"acceptance failed";
        return -4;
    }
    close(listen_sock);

    cout<<"game server connected, can be used anytime\n";

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
            if (!msgOpt.size()) throw runtime_error("got empty msg");//failed to get message

            json request = json::parse(msgOpt);
            string action = request["action"];
            string type = request["type"];
            if(action=="create"){
                json data=request[data];
                int result=op_create(type,data);
                json j;
                if(result<0){
                    j["response"]="failed";
                    j["request"]=request;
                }
                else j["response"]="success";
                send_message(sockfd,j.dump());
            }
            else if(action=="query"){
                string name=request["name"];
                json j=op_query(type,name);
                send_message(sockfd,j.dump());
            }
            else if(action=="search"){//search for available room/user
                json j=op_search(type);
                send_message(sockfd, j.dump());
            }
            else if(action=="update"){
                json request=request["data"];
                int result=op_update(type,request);
                json j;
                if(result<0){
                    j["response"]="failed";
                    j["data"]=request;
                }
                else j["response"]="success";
                send_message(sockfd,j.dump());
            }
            else if(action=="delete"){
                string name=request["data"];
                int result=op_delete(type,name);
                json j;
                if(result<0){
                    j["response"]="failed";
                    j["data"]=request;
                }
                else j["response"]="success";
                send_message(sockfd,j.dump());
            }
            else{
                cerr<<"you got a typo, find out why"<<endl;
            }
        }
        
    }
    close(sockfd);
    return 0;
}
