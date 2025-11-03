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

    //handle response
    string resp = response.value("response", "failed");

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
                {"status", "idle"},
                {"roomname","-1"}
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
    // 1) parse client request
    json j;
    try { j = json::parse(msg); }
    catch (...) {
        std::cerr << "[client] invalid JSON from fd=" << fd << ": " << msg << "\n";
        send_message(fd, json({{"response","failed"},{"reason","invalid JSON"}}).dump());
        return 0;
    }

    // 2) If not logged in yet → do login/register path
    if (logineds.find(fd) == logineds.end()) {
        int ok = logining(
            fd,
            j.value("action",""),      // "login" | "register"
            j.value("name",""),
            j.value("password","")
        );
        if (ok) logineds.insert(fd);
        return ok;
    }

    // 3) Already logged in → handle lobby ops
    const std::string action = j.value("action", "");
    const std::string meName = j.value("name", "");

    // Fetch my user object first (data server supports query user by name)
    json getMeReq = {{"action","query"},{"type","user"},{"name",meName}};
    json meRes = ds_call(getMeReq);
    if (meRes["response"] != "success") {
        send_message(fd, json({{"response","failed"},{"reason","user not found"}}).dump());
        return 0;
    }
    json me = meRes["data"];

    // -------- action handlers --------

    if (action == "create") {
        // Create a room (host = me.name)
        const std::string roomName = j.value("room", "");
        const std::string vis = j.value("visibility", "public"); // public|private

        // ensure no duplicate room name
        json exist = find_room_by_name(roomName);
        if (!exist.empty()) {
            send_message(fd, json({{"response","failed"},{"reason","dulplicate room"}}).dump());
            return 0;
        }

        json createRoom = {
            {"action","create"},
            {"type","room"},
            {"data",{
                {"name", roomName},
                {"hostUser", me.value("name","")},
                {"visibility", vis},
                {"inviteList", json::array()},
                {"status","idle"}
            }}
        };
        json cres = ds_call(createRoom);
        if (cres["response"] != "success") {
            send_message(fd, json({{"response","failed"},{"reason","room creation failed"}}).dump());
            return 0;
        }

        // Put me into that room; spec says client lobby create success or failed.
        me["roomName"] = roomName;
        me["status"]   = "playing";
        json upd = {{"action","update"},{"type","user"},{"data",me}};
        (void)ds_call(upd);

        send_message(fd, json({{"response","success"}}).dump());
        return 1;
    }

    else if (action == "join") {
        // Join a room by name: public OR private + invited
        const std::string roomName = j.value("room", "");
        json room = find_room_by_name(roomName);
        if (room.empty()) {
            send_message(fd, json({{"response","failed"},{"reason","no such room"}}).dump());
            return 0;
        }
        const std::string visibility = room.value("visibility","public");
        const std::string status     = room.value("status","idle");

        // allow only if: public, and idle; or private and invited
        bool invited = false;
        if (visibility == "private" && room.contains("inviteList")) {
            for (auto& uid : room["inviteList"]) {
                if (uid == me["id"]) { invited = true; break; }
            }
        }
        bool allowed = (visibility == "public") || invited;

        if (!allowed) {
            send_message(fd, json({{"response","failed"},{"reason","not invited"}}).dump());
            return 0;
        }
        if (status == "playing") {
            send_message(fd, json({{"response","failed"},{"reason","room already playing"}}).dump());
            return 0;
        }

        me["roomName"] = roomName;
        me["status"]   = "playing";
        json upd = {{"action","update"},{"type","user"},{"data",me}};
        (void)ds_call(upd);

        send_message(fd, json({{"response","success"}}).dump());
        return 1;
    }

    else if (action == "curroom") {
        // Return info about my current room
        const std::string roomName = me.value("roomName","-1");
        if (roomName == "-1" || roomName.empty()) {
            send_message(fd, json({{"response","failed"},{"reason","not in any room"}}).dump());
            return 0;
        }
        json room = find_room_by_name(roomName);
        if (room.empty()) {
            send_message(fd, json({{"response","failed"},{"reason","room not found"}}).dump());
            return 0;
        }
        // spec: curroom returns {"response":"success","data":[rooms]}
        json out = {{"response","success"},{"data", json::array({room})}};
        send_message(fd, out.dump());
        return 1;
    }

    else if (action == "curinvite") {
        // List all rooms that invited me (private rooms with my id in inviteList)
        json sreq = {{"action","search"},{"type","room"}};
        json sres = ds_call(sreq);
        if (sres["response"] != "success" || !sres.contains("data")) {
            send_message(fd, json({{"response","failed"},{"reason","no invites"}}).dump());
            return 0;
        }
        json invited = json::array();
        for (auto& room : sres["data"]) {
            if (room.value("visibility","public") == "private" && room.contains("inviteList")) {
                for (auto& uid : room["inviteList"]) {
                    if (uid == me["id"]) { invited.push_back(room); break; }
                }
            }
        }
        if (invited.empty()) {
            send_message(fd, json({{"response","failed"},{"reason","no invites"}}).dump());
        } else {
            send_message(fd, json({{"response","success"},{"data",invited}}).dump());
        }
        return 1;
    }

    else if (action == "invite") {
        // invite another user into my current room
        const std::string targetName = j.value("user", "");
        const std::string roomName   = j.value("room", me.value("roomName",""));

        if (roomName.empty() || roomName == "-1") {
            send_message(fd, json({{"response","failed"},{"reason","not in a room"}}).dump());
            return 0;
        }

        // 1) find target user
        json tgtReq = {{"action","query"},{"type","user"},{"name",targetName}};
        json tgtRes = ds_call(tgtReq);
        if (tgtRes["response"] != "success") {
            send_message(fd, json({{"response","failed"},{"reason","no such user"}}).dump());
            return 0;
        }
        json target = tgtRes["data"];

        // 2) find room by name
        json room = find_room_by_name(roomName);
        if (room.empty()) {
            send_message(fd, json({{"response","failed"},{"reason","room not found"}}).dump());
            return 0;
        }

        // 3) only host can invite (optional policy)
        if (room.value("hostUser","") != me.value("name","")) {
            send_message(fd, json({{"response","failed"},{"reason","only host can invite"}}).dump());
            return 0;
        }

        // 4) update inviteList (by id) and send update (room updates are by id in your note)
        if (!room.contains("inviteList") || !room["inviteList"].is_array()) {
            room["inviteList"] = json::array();
        }
        push_unique(room["inviteList"], target["id"]);

        json upd = {{"action","update"},{"type","room"},{"data",room}};
        json ures = ds_call(upd);
        if (ures["response"] != "success") {
            send_message(fd, json({{"response","failed"},{"reason","invite update failed"}}).dump());
            return 0;
        }
        send_message(fd, json({{"response","success"}}).dump());
        return 1;
    }

    else {
        send_message(fd, json({{"response","failed"},{"reason","unknown action"}}).dump());
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