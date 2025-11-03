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
unordered_map<int, int> logineds;  // map client_fd → user_id

// Set a socket to non-blocking mode
int make_socket_non_blocking(int sfd) {
    int flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1) return -1;
    flags |= O_NONBLOCK;
    return fcntl(sfd, F_SETFL, flags);
}

// tiny helper: round-trip to data server, return parsed JSON or an error object
static json ds_call(const json& req) {
    send_message(datafd, req.dump());
    std::string r = recv_message(datafd);
    if (r.empty() || r == "Disconnected") {
        return json{{"response","failed"},{"reason","data server error"}};
    }
    json j;
    try { j = json::parse(r); }
    catch (...) { return json{{"response","failed"},{"reason","invalid JSON from data server"}}; }
    return j;
}

// find a room by name using data server `search`
static json find_room_by_name(const std::string& roomName) {
    json sreq = {{"action","search"},{"type","room"}};
    json sres = ds_call(sreq);
    if (sres["response"] != "success") return json{};
    if (!sres.contains("data")) return json{};

    for (auto& room : sres["data"]) {
        if (room.value("name","") == roomName) return room;
    }
    return json{};
}

// safe array push unique
template<class T>
static void push_unique(json& arr, const T& v) {
    for (auto& e : arr) if (e == v) return;
    arr.push_back(v);
}


int logining(int fd, const std::string& action, const std::string& name, const std::string& password) {
    json request = {
        {"action", "query"},
        {"type", "user"},
        {"name", name}
    };
    send_message(datafd, request.dump());

    std::string reply = recv_message(datafd);
    if (reply.empty() || reply == "Disconnected") {
        send_message(fd, json{{"response","failed"},{"reason","data server error"}}.dump());
        return -1;
    }

    json response;
    try { response = json::parse(reply); }
    catch (...) {
        send_message(fd, json{{"response","failed"},{"reason","invalid JSON from data server"}}.dump());
        return -1;
    }

    std::string resp = response.value("response", "failed");

    // === CASE 1: user exists ===
    if (resp == "success") {
        json user = response["data"];
        if (action == "login") {
            if (user["name"] == name &&
                user["password"] == password &&
                user["status"] == "offline") {

                user["last_login"] = now_time_str();
                user["status"] = "idle";

                json update = {
                    {"action", "update"},
                    {"type", "user"},
                    {"data", user}
                };
                send_message(datafd, update.dump());
                send_message(fd, json{{"response","success"}}.dump());

                cout << "[login] User '" << name << "' logged in successfully (id=" << user["id"] << ")\n";
                return user["id"].get<int>();
            } else {
                send_message(fd, json{{"response","failed"},{"reason","wrong password or already online"}}.dump());
                return -1;
            }
        } else {
            send_message(fd, json{{"response","failed"},{"reason","user already exists"}}.dump());
            return -1;
        }
    }

    // === CASE 2: user not found ===
    if (action == "register") {
        json user = {
            {"name", name},
            {"password", password},
            {"last_login", now_time_str()},
            {"status", "idle"},
            {"roomName", "-1"}
        };
        json update = {
            {"action", "create"},
            {"type", "user"},
            {"data", user}
        };
        send_message(datafd, update.dump());
        std::string crep = recv_message(datafd);
        send_message(fd, json{{"response","success"}}.dump());
        cout << "[register] New user registered: " << name << "\n";

        // Fetch it back to get its id
        send_message(datafd, request.dump());
        std::string r2 = recv_message(datafd);
        json j2 = json::parse(r2);
        if (j2["response"] == "success") return j2["data"]["id"].get<int>();
        return -1;
    }

    send_message(fd, json{{"response","failed"},{"reason","no such user"}}.dump());
    return -1;
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
/*
int client_request(int fd, string msg){
    json j=json::parse(msg.c_str());
    if(logineds.find(fd)!=logineds.end()){//logined
        string action=j.at("action").get<string>();
        json togetuserinfo={{"action","query"},{"type","user"},{"name",j.at("name").get<string>()}};
        send_message(datafd,togetuserinfo.dump());
        json user=json::parse(recv_message(datafd).c_str());
        if(action=="create"){
            json createmsg={
                {"action","create"},
                {"type","room"},
                {"data",{
                    {"name",j.at("room").get<string>()},
                    {"hostuser",user["name"]},
                    {"visibility",j["visibility"]},
                    {"invitelist",json::array()},
                    {"status","idle"}
                }}
            };
            send_message(datafd,createmsg.dump());
            json result=json::parse(recv_message(datafd));
            if(result["response"]=="success"){
                //move the player into room
                user["roomname"]=j["room"];
                user["status"]="playing";
                json update={{"action","update"},{"type","user"},{"data",user}};
                send_message(datafd,update.dump());
                json returnmsg={{"response","success"}};
                send_message(fd,returnmsg.dump());
            }
        }
        else if(action=="join"){

        }
        else if(action=="curroom"||action=="curinvite"){

        }
        else if(action=="join"){//look up either invited or public

        }
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
*/
int client_request(int fd, const std::string& msg) {
    json j;
    try {
        j = json::parse(msg);
    } catch (...) {
        send_message(fd, json{{"response", "failed"}, {"reason", "invalid JSON"}}.dump());
        return 0;
    }

    // ---------- LOGIN / REGISTER ----------
    if (logineds.find(fd) == logineds.end()) {
        int user_id = logining(fd,
                               j.value("action", ""),
                               j.value("name", ""),
                               j.value("password", ""));
        if (user_id >= 0) {
            logineds[fd] = user_id;
            return 1;
        }
        return 0;
    }

    // ---------- Already logged in ----------
    int user_id = logineds[fd];
    const std::string action = j.value("action", "");

    // Fetch current user info from data server by id
    json qreq = {{"action", "query"}, {"type", "user"}, {"name", j.value("name", "")}};
    send_message(datafd, qreq.dump());
    std::string rep = recv_message(datafd);
    if (rep.empty() || rep == "Disconnected") {
        send_message(fd, json{{"response", "failed"}, {"reason", "data server error"}}.dump());
        return 0;
    }
    json qres = json::parse(rep);
    if (qres["response"] != "success") {
        send_message(fd, json{{"response", "failed"}, {"reason", "user not found"}}.dump());
        return 0;
    }
    json me = qres["data"];
    me["id"] = user_id; // ensure consistency

    // ---------- CREATE ROOM ----------
    if (action == "create") {
        const std::string roomName = j.value("room", "");
        const std::string vis = j.value("visibility", "public");

        // Check duplicate room
        json sreq = {{"action", "search"}, {"type", "room"}};
        send_message(datafd, sreq.dump());
        json sres = json::parse(recv_message(datafd));
        if (sres["response"] == "success") {
            for (auto &r : sres["data"]) {
                if (r.value("name", "") == roomName) {
                    send_message(fd, json{{"response", "failed"}, {"reason", "dulplicate room"}}.dump());
                    return 0;
                }
            }
        }

        // Create new room
        json room = {
            {"name", roomName},
            {"hostUser", me.value("name", "")},
            {"visibility", vis},
            {"inviteList", json::array()},
            {"status", "idle"}
        };
        json creq = {{"action", "create"}, {"type", "room"}, {"data", room}};
        send_message(datafd, creq.dump());
        json cres = json::parse(recv_message(datafd));
        if (cres["response"] != "success") {
            send_message(fd, json{{"response", "failed"}, {"reason", "room creation failed"}}.dump());
            return 0;
        }

        // Update user to mark playing
        me["roomName"] = roomName;
        me["status"] = "playing";
        json ureq = {{"action", "update"}, {"type", "user"}, {"data", me}};
        send_message(datafd, ureq.dump());
        recv_message(datafd);

        send_message(fd, json{{"response", "success"}}.dump());
        return 1;
    }

    // ---------- JOIN ROOM ----------
    else if (action == "join") {
        const std::string roomName = j.value("room", "");

        // Search room list
        json sreq = {{"action", "search"}, {"type", "room"}};
        send_message(datafd, sreq.dump());
        json sres = json::parse(recv_message(datafd));
        json targetRoom;
        if (sres["response"] == "success") {
            for (auto &r : sres["data"])
                if (r.value("name", "") == roomName) targetRoom = r;
        }

        if (targetRoom.empty()) {
            send_message(fd, json{{"response", "failed"}, {"reason", "no such room"}}.dump());
            return 0;
        }

        const std::string vis = targetRoom.value("visibility", "public");
        bool invited = false;
        if (vis == "private" && targetRoom.contains("inviteList")) {
            for (auto &uid : targetRoom["inviteList"])
                if (uid == user_id) invited = true;
        }

        if (vis == "private" && !invited) {
            send_message(fd, json{{"response", "failed"}, {"reason", "not invited"}}.dump());
            return 0;
        }
        if (targetRoom.value("status", "") == "playing") {
            send_message(fd, json{{"response", "failed"}, {"reason", "room already playing"}}.dump());
            return 0;
        }

        me["roomName"] = roomName;
        me["status"] = "playing";
        json ureq = {{"action", "update"}, {"type", "user"}, {"data", me}};
        send_message(datafd, ureq.dump());
        recv_message(datafd);

        send_message(fd, json{{"response", "success"}}.dump());
        return 1;
    }

    // ---------- CURROOM ----------
    else if (action == "curroom") {
        const std::string roomName = me.value("roomName", "-1");
        if (roomName == "-1" || roomName.empty()) {
            send_message(fd, json{{"response", "failed"}, {"reason", "not in any room"}}.dump());
            return 0;
        }

        json qreq = {{"action", "query"}, {"type", "room"}, {"name", roomName}};
        send_message(datafd, qreq.dump());
        json qres = json::parse(recv_message(datafd));
        if (qres["response"] != "success") {
            send_message(fd, json{{"response", "failed"}, {"reason", "room not found"}}.dump());
            return 0;
        }
        json out = {{"response", "success"}, {"data", json::array({qres["data"]})}};
        send_message(fd, out.dump());
        return 1;
    }

    // ---------- CURINVITE ----------
    else if (action == "curinvite") {
        json sreq = {{"action", "search"}, {"type", "room"}};
        send_message(datafd, sreq.dump());
        json sres = json::parse(recv_message(datafd));
        json invited = json::array();

        if (sres["response"] == "success") {
            for (auto &room : sres["data"]) {
                if (room.value("visibility", "") == "private" && room.contains("inviteList")) {
                    for (auto &uid : room["inviteList"])
                        if (uid == user_id) invited.push_back(room);
                }
            }
        }

        if (invited.empty())
            send_message(fd, json{{"response", "failed"}, {"reason", "no invites"}}.dump());
        else
            send_message(fd, json{{"response", "success"}, {"data", invited}}.dump());
        return 1;
    }

    // ---------- INVITE ----------
    else if (action == "invite") {
        const std::string targetName = j.value("user", "");
        const std::string roomName = j.value("room", me.value("roomName", ""));

        if (roomName.empty() || roomName == "-1") {
            send_message(fd, json{{"response", "failed"}, {"reason", "not in a room"}}.dump());
            return 0;
        }

        // Query target user
        json qreq = {{"action", "query"}, {"type", "user"}, {"name", targetName}};
        send_message(datafd, qreq.dump());
        json qres = json::parse(recv_message(datafd));
        if (qres["response"] != "success") {
            send_message(fd, json{{"response", "failed"}, {"reason", "no such user"}}.dump());
            return 0;
        }
        json targetUser = qres["data"];
        int targetId = targetUser["id"];

        // Query room
        json rreq = {{"action", "query"}, {"type", "room"}, {"name", roomName}};
        send_message(datafd, rreq.dump());
        json rres = json::parse(recv_message(datafd));
        if (rres["response"] != "success") {
            send_message(fd, json{{"response", "failed"}, {"reason", "room not found"}}.dump());
            return 0;
        }
        json room = rres["data"];

        if (room.value("hostUser", "") != me.value("name", "")) {
            send_message(fd, json{{"response", "failed"}, {"reason", "only host can invite"}}.dump());
            return 0;
        }

        // Add invite
        if (!room.contains("inviteList") || !room["inviteList"].is_array())
            room["inviteList"] = json::array();
        bool exists = false;
        for (auto &x : room["inviteList"])
            if (x == targetId) exists = true;
        if (!exists) room["inviteList"].push_back(targetId);

        json ureq = {{"action", "update"}, {"type", "room"}, {"data", room}};
        send_message(datafd, ureq.dump());
        json ures = json::parse(recv_message(datafd));
        if (ures["response"] != "success") {
            send_message(fd, json{{"response", "failed"}, {"reason", "invite failed"}}.dump());
            return 0;
        }

        send_message(fd, json{{"response", "success"}}.dump());
        return 1;
    }

    // ---------- UNKNOWN ----------
    else {
        send_message(fd, json{{"response", "failed"}, {"reason", "unknown action"}}.dump());
        return 0;
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