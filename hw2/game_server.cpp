#include <iostream>
#include <sys/types.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <string.h>
#include <string>
#include <set>
#include <unordered_map>
#include "utility.h"
#include "nlohmann/json.hpp"
#include <cassert>

using json=nlohmann::json; using namespace std;

const int GAME_SERVER_PORT=45632, DATA_SERVER_PORT=45631;
const char *IP="127.0.0.1"; 
const int MAX_EVENTS=10;
int datafd; 
unordered_map<int,int> logineds; // fd -> user id if not logined -> -1

int make_socket_non_blocking(int s){int f=fcntl(s,F_GETFL,0);return fcntl(s,F_SETFL,f|O_NONBLOCK);}

int logining(int fd, const std::string &action, const std::string &name, const std::string &password) {
    // --- Step 1. Ask data server for this user ---
    json query = {
        {"action", "query"},
        {"type", "user"},
        {"name", name}
    };
    send_message(datafd, query.dump());
    string reply = recv_message(datafd);

    if (reply.empty() || reply == "Disconnected") {
        cerr << "[GameServer] Data server unavailable or returned empty reply.\n";
        send_message(fd, json{{"response", "failed"}, {"reason", "data server unavailable"}}.dump());
        return -1;
    }
    // cerr<<"reply of logining "<<reply<<endl;
    json resp;
    try {
        resp = json::parse(reply);
    } catch (const std::exception &e) {
        cerr << "[GameServer] JSON parse error from data server: " << e.what() << "\nRaw: " << reply << endl;
        send_message(fd, json{{"response", "failed"}, {"reason", "invalid JSON from data server"}}.dump());
        return -1;
    }
     
    std::string status = resp.value("response", "failed");

    // --- Step 2. Handle found user ---
    if (status == "success") {
        json user = resp["data"];

        // ensure id exists
        if (!user.contains("id") || user["id"].is_null()) {
            cerr << "[GameServer] Warning: queried user missing 'id', inserting placeholder.\n";
            static int fallback_id = 9999; // fallback only
            user["id"] = fallback_id++;
        }

        if (action == "login") {
            // existing user trying to login
            std::string stored_pw = user.value("password", "");
            std::string state = user.value("status", "offline");

            if (stored_pw == password && state == "offline") {
                user["last_login"] = now_time_str();
                user["status"] = "idle";

                json update = {
                    {"action", "update"},
                    {"type", "user"},
                    {"data", user}
                };
                send_message(datafd, update.dump());
                string update_reply = recv_message(datafd);  // Consume the update response

                json ok = {{"response", "success"}};
                send_message(fd, ok.dump());
                cout << "[GameServer] User '" << name << "' logged in successfully (id=" << user["id"] << ")\n";
                return user["id"];
            } else {
                cerr << "[GameServer] Login failed for user '" << name << "': wrong password or already online.\n";
                send_message(fd, json{
                    {"response", "failed"},
                    {"reason", "wrong password or already online"}
                }.dump());
                return -1;
            }
        } else {
            // trying to register but user already exists
            send_message(fd, json{
                {"response", "failed"},
                {"reason", "user already exists"}
            }.dump());
            return -1;
        }
    }

    // --- Step 3. Handle missing user (create new) ---
    if (status == "failed" && action == "register") {
        json new_user = {
            {"id", -1}, // data server will overwrite
            {"name", name},
            {"password", password},
            {"last_login", now_time_str()},
            {"status", "idle"},
            {"roomName", "-1"}
        };
        json create = {
            {"action", "create"},
            {"type", "user"},
            {"data", new_user}
        };
        send_message(datafd, create.dump());

        std::string reply2 = recv_message(datafd);
        json resp2;
        try {
            resp2 = json::parse(reply2);
        } catch (...) {
            cerr << "[GameServer] Invalid JSON from data server during create.\n";
            send_message(fd, json{{"response", "failed"}, {"reason", "invalid JSON on create"}}.dump());
            return -1;
        }

        if (resp2.value("response", "failed") == "success") {
            send_message(fd, json{{"response", "success"}}.dump());
            cout << "[GameServer] Registered new user: " << name << endl;
            return resp2["id"];
        } else {
            send_message(fd, json{
                {"response", "failed"},
                {"reason", "data server create failed"}
            }.dump());
            return -1;
        }
    }

    // --- Step 4. Login failed because user doesn’t exist ---
    if (status == "failed" && action == "login") {
        send_message(fd, json{
            {"response", "failed"},
            {"reason", "user does not exist"}
        }.dump());
        cerr << "[GameServer] Login failed: no such user '" << name << "'\n";
        return -1;
    }

    // --- fallback ---
    send_message(fd, json{
        {"response", "failed"},
        {"reason", "unexpected error"}
    }.dump());
    cerr << "[GameServer] Unexpected condition in logining() for user '" << name << "'\n";
    return -1;
}

int logout_user(int fd) {
    int uid = logineds[fd];
    if (uid < 0) return -1;

    // Step 1. Query the user
    json query = {
        {"action", "query"},
        {"type", "user"},
        {"id", uid}
    };
    send_message(datafd, query.dump());
    string reply = recv_message(datafd);
    // cerr<<reply<<endl;
    if (reply.empty() || reply == "Disconnected") {
        cerr << "[GameServer] Data server not responding during logout.\n";
        return -1;
    }

    json resp;
    try {
        resp = json::parse(reply);
    } catch (const std::exception &e) {
        cerr << "[GameServer] JSON parse error from data server: " << e.what()
             << "\nRaw: " << reply << endl;
        return -1;
    }

    if (resp.value("response", "failed") != "success" || !resp.contains("data") || !resp["data"].is_object()) {
        cerr<<resp.dump();
        cerr << "[GameServer] logout_user(): user not found or invalid JSON\n";
        return -1;
    }

    // Step 2. Update the user to offline
    json user = resp["data"];
    string uname = user.value("name", "(unknown)");
    cout << "[GameServer] Logging out user: " << uname << " (id=" << uid << ")\n";

    user["status"] = "offline";

    json update = {
        {"action", "update"},
        {"type", "user"},
        {"data", user}
    };
    send_message(datafd, update.dump());

    // Step 3. Verify update succeeded
    string reply2 = recv_message(datafd);
    if (reply2.empty() || reply2 == "Disconnected") {
        cerr << "[GameServer] Data server disconnected during logout.\n";
        return -1;
    }

    json resp2;
    try {
        resp2 = json::parse(reply2);
    } catch (...) {
        cerr << "[GameServer] Invalid JSON from data server during logout update: " << reply2 << endl;
        return -1;
    }

    if (resp2.value("response", "failed") == "success") {
        cout << "[GameServer] User " << uname << " successfully logged out.\n";
        return 1;
    } else {
        cerr << "[GameServer] Data server failed to update user during logout: "
             << resp2.dump() << endl;
        return -1;
    }
}



int client_request(int fd,const string&msg){
    json j;
    try{j=json::parse(msg);}
    catch(...){
        send_message(fd,json{{"response","failed"},{"reason","invalid JSON"}}.dump());
        return 0;
    }
    if(logineds[fd]==-1){
        int uid=logining(fd,j["action"],j["name"],j["password"]);
        //cerr<<"does go to logining\n";
        if(uid>=0)logineds[fd]=uid;
        return uid;
    }
    int uid=logineds[fd];
    string act=j["action"];
    // query user itself
    send_message(datafd,json{{"action","query"},{"type","user"},{"id",uid}}.dump());
    string self_reply = recv_message(datafd);
    json self_resp = json::parse(self_reply);
    if(self_resp.value("response", "failed") != "success" || !self_resp.contains("data")) {
        send_message(fd,json{{"response","failed"},{"reason","failed to query user"}}.dump());
        return 0;
    }
    json me = self_resp["data"];
    me["id"]=uid;

    if(act=="create"){ // room
        string room=j["roomname"];
        string vis=j.value("visibility","public");
        cerr << "[GameServer] User '" << me["name"] << "' attempting to create room '" << room << "' (visibility=" << vis << ")" << endl;
        json check={{"action","search"},{"type","room"}};
        send_message(datafd,check.dump());
        json r=json::parse(recv_message(datafd));
        if(r.contains("data") && r["data"].is_array()) {
            for(auto &x:r["data"])
                if(x["name"]==room){
                    cerr << "[GameServer] Room creation failed: room '" << room << "' already exists" << endl;
                    send_message(fd,json{{"response","failed"},{"reason","duplicate room"}}.dump());
                    return 0;
                }
        }
        json newroom={{"name",room},{"hostUser",me["name"]},{"oppoUser",""},{"visibility",vis},{"inviteList",json::array()},{"status","idle"}};
        send_message(datafd,json{{"action","create"},{"type","room"},{"data",newroom}}.dump());
        string create_reply = recv_message(datafd);
        json create_resp = json::parse(create_reply);
        if(create_resp.value("response", "failed") != "success") {
            cerr << "[GameServer] Room creation failed: data server error" << endl;
            send_message(fd,json{{"response","failed"},{"reason","failed to create room"}}.dump());
            return 0;
        }
        send_message(datafd,json{{"action","query"},{"type","room"},{"name",room}}.dump());
        string query_reply = recv_message(datafd);
        json qr=json::parse(query_reply);
        if(qr.value("response", "failed") != "success" || !qr.contains("data")) {
            send_message(fd,json{{"response","failed"},{"reason","room query failed"}}.dump());
            return 0;
        }
        me["roomName"]=room;
        me["status"]="room";
        send_message(datafd,json{{"action","update"},{"type","user"},{"data",me}}.dump());
        recv_message(datafd);  // Consume update response
        cout << "[GameServer] User '" << me["name"] << "' successfully created and joined room '" << room << "'" << endl;
        send_message(fd,json{{"response","success"}}.dump());return 1;
    }
    else if(act=="join"){
        string room=j["roomname"];
        cerr << "[GameServer] User '" << me["name"] << "' attempting to join room '" << room << "'" << endl;
        json s={{"action","search"},{"type","room"}};send_message(datafd,s.dump());
        json sres=json::parse(recv_message(datafd));json target;
        if(sres.contains("data") && sres["data"].is_array()) {
            for(auto&r:sres["data"])if(r["name"]==room)target=r;
        }
        if(target.empty()){
            cerr << "[GameServer] Join room failed: room '" << room << "' not found" << endl;
            send_message(fd,json{{"response","failed"},{"reason","no such room"}}.dump());
            return 0;
        }
        if(target["status"]=="playing"){
            cerr << "[GameServer] Join room failed: room '" << room << "' is busy" << endl;
            send_message(fd,json{{"response","failed"},{"reason","busy"}}.dump());
            return 0;
        }
        me["roomName"]=room;
        me["status"]="room";
        send_message(datafd,json{{"action","update"},{"type","user"},{"data",me}}.dump());
        recv_message(datafd);  // Consume update response
        cout << "[GameServer] User '" << me["name"] << "' successfully joined room '" << room << "'" << endl;
        send_message(fd,json{{"response","success"}}.dump());return 1;
    }
    else if(act=="curroom"){
        string current_room = me.value("roomName", "-1");
        cerr << "[GameServer] User '" << me["name"] << "' querying current room" << endl;
        if(current_room == "-1"){
            // User not in a room - show all public rooms
            cerr << "[GameServer] User not in room, listing all public rooms" << endl;
            send_message(datafd,json{{"action","search"},{"type","room"}}.dump());
            string search_reply = recv_message(datafd);
            json search_res = json::parse(search_reply);
            if(search_res.value("response", "failed") == "success" && search_res.contains("data")) {
                cout << "[GameServer] Retrieved " << search_res["data"].size() << " public rooms for user '" << me["name"] << "'" << endl;
                send_message(fd,json{{"response","success"},{"data",search_res["data"]}}.dump());
            } else {
                cerr << "[GameServer] Room search failed or no public rooms available" << endl;
                send_message(fd,json{{"response","failed"},{"reason",search_res.value("reason", "no available room")}}.dump());
            }
            return 1;
        }
        send_message(datafd,json{{"action","query"},{"type","room"},{"name",current_room}}.dump());
        string room_reply = recv_message(datafd);
        json q=json::parse(room_reply);
        if(q.value("response", "failed") == "success" && q.contains("data")) {
            cout << "[GameServer] User '" << me["name"] << "' retrieved current room: '" << current_room << "'" << endl;
            send_message(fd,json{{"response","success"},{"data",json::array({q["data"]})}}.dump());
        } else {
            cerr << "[GameServer] Current room query failed: room '" << current_room << "' not found" << endl;
            send_message(fd,json{{"response","failed"},{"reason","room not found"}}.dump());
        }
        return 1;
    }
    else if (act == "curinvite") {
        cerr << "[GameServer] User '" << me["name"] << "' querying current invites" << endl;
        // 1. Ask Data Server for all rooms
        json req = {
            {"action", "search"},
            {"type", "room"}
        };
        send_message(datafd, req.dump());

        // 2. Receive reply
        string reply = recv_message(datafd);
        json res;
        try {
            res = json::parse(reply);
        } catch (const exception &e) {
            cerr << "[GameServer] Failed to parse data server response in curinvite: " << e.what() << endl;
            send_message(fd, json{{"response", "failed"}, {"reason", "invalid JSON from data server"}}.dump());
            return 0;
        }

        // 3. Collect rooms that contain this user's id in inviteList
        json arr = json::array();

        if (res.contains("data") && res["data"].is_array()) {
            for (auto &r : res["data"]) {
                if (!r.contains("inviteList") || !r["inviteList"].is_array())
                    continue;

                for (auto &x : r["inviteList"]) {
                    if (x.is_number_integer() && x.get<int>() == uid) {
                        arr.push_back(r);
                        break;  // no need to check more invites for this room
                    }
                }
            }
        } else {
            cerr << "[GameServer] Invalid room data from data server: " << res.dump() << endl;
        }

        // 4. Send back filtered result
        json reply_to_client;
        if (arr.empty()) {
            cerr << "[GameServer] User '" << me["name"] << "' has no pending invites" << endl;
            reply_to_client = {
                {"response", "failed"},
                {"reason", "no invites"}
            };
        } else {
            cout << "[GameServer] User '" << me["name"] << "' has " << arr.size() << " pending invite(s)" << endl;
            reply_to_client = {
                {"response", "success"},
                {"data", arr}
            };
        }

        send_message(fd, reply_to_client.dump());
        return 1;
    }
    else if(act=="invite"){
        // Get the user to invite (client sends "name" field)
        string uname = j.value("name", j.value("user", ""));
        if(uname.empty()) {
            cerr << "[GameServer] Invite failed: no user specified" << endl;
            send_message(fd,json{{"response","failed"},{"reason","no user specified"}}.dump());
            return 0;
        }

        // Get current room from user's status
        string room = me.value("roomName", "-1");
        if(room == "-1") {
            cerr << "[GameServer] Invite failed: user '" << me["name"] << "' is not in a room" << endl;
            send_message(fd,json{{"response","failed"},{"reason","not in a room"}}.dump());
            return 0;
        }

        cerr << "[GameServer] User '" << me["name"] << "' attempting to invite '" << uname << "' to room '" << room << "'" << endl;

        // Query the user to invite
        send_message(datafd,json{{"action","query"},{"type","user"},{"name",uname}}.dump());
        json u=json::parse(recv_message(datafd));
        if(u.value("response","failed") != "success" || !u.contains("data")){
            cerr << "[GameServer] Invite failed: user '" << uname << "' not found" << endl;
            send_message(fd,json{{"response","failed"},{"reason","no such user"}}.dump());
            return 0;
        }
        int tid=u["data"]["id"];

        // Query the room
        send_message(datafd,json{{"action","query"},{"type","room"},{"name",room}}.dump());
        string room_reply = recv_message(datafd);
        json rr=json::parse(room_reply);
        if(rr.value("response","failed") != "success" || !rr.contains("data")) {
            cerr << "[GameServer] Invite failed: room '" << room << "' not found" << endl;
            send_message(fd,json{{"response","failed"},{"reason","room not found"}}.dump());
            return 0;
        }

        json roomj=rr["data"];
        if(roomj["hostUser"]!=me["name"]){
            cerr << "[GameServer] Invite failed: user '" << me["name"] << "' is not host of room '" << room << "'" << endl;
            send_message(fd,json{{"response","failed"},{"reason","not host"}}.dump());
            return 0;
        }
        auto&inv=roomj["inviteList"];
        bool ex=false;
        for(auto&x:inv)if(x==tid)ex=true;
        if(!ex)inv.push_back(tid);
        send_message(datafd,json{{"action","update"},{"type","room"},{"data",roomj}}.dump());
        recv_message(datafd);
        cout << "[GameServer] User '" << me["name"] << "' successfully invited '" << uname << "' (id=" << tid << ") to room '" << room << "'" << endl;
        send_message(fd,json{{"response","success"}}.dump());
        return 1;
    }
    else if(act=="start"){
        //check if there are 2 player in the room
        send_message(datafd,json{{"action","query"},{"type","room"},{"name",me["roomName"]}}.dump());
        string query_reply = recv_message(datafd);
        json query_res = json::parse(query_reply);
        if(query_res.value("response", "failed") == "success") {
            if(!query_res.contains("data")){
                send_message(fd,json{{"response","failed"},{"reason","data_server side:"+query_res.value("reason", "no data of room")}}.dump());
                return -1;
            }
            json room=query_res["data"];
            if(room.value("oppoUser","").size() || (room.value("hostUser","").size())){//start the game
                send_message(fd,json{{"response","success"}}.dump());
                

                // Find and notify the opponent user
                string oppo_name = (room["hostUser"] == me["name"]) ? room.value("oppoUser", "") : room.value("hostUser", "");
                if (!oppo_name.empty()) {
                    // Query opponent user to get their id
                    send_message(datafd, json{{"action","query"},{"type","user"},{"name",oppo_name}}.dump());
                    string oppo_reply = recv_message(datafd);
                    json oppo_res = json::parse(oppo_reply);
                    if (oppo_res.value("response", "failed") == "success" && oppo_res.contains("data")) {
                        int oppo_id = oppo_res["data"].value("id", -1);
                        // Find opponent's fd in logineds map
                        for (auto& [oppo_fd, oppo_uid] : logineds) {
                            if (oppo_uid == oppo_id) {
                                send_message(oppo_fd, json{{"action","start"}}.dump());
                                break;
                            }
                        }
                    }
                }
                // Send start message to the current user as well
                send_message(fd, json{{"action","start"}}.dump());

                return 1;
            }
            else{
                send_message(fd,json{{"response","failed"},{"reason","no 2 player in the room"}}.dump());
                return -1;
            }
        } 
        else {
            cerr << "[GameServer] Query of room failed, fix it" << endl;
            send_message(fd,json{{"response","failed"},{"reason",query_res.value("reason", "no room")}}.dump());
        }
    }
    else if(act=="spectate"){
        throw(runtime_error("not implemented yet"));
    }
    send_message(fd,json{{"response","failed"},{"reason","unknown action"}}.dump());
    return 0;
}

int main() {
    // === Create game listening socket ===
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        perror("[GameServer] socket() failed");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(GAME_SERVER_PORT);
    if (inet_pton(AF_INET, IP, &addr.sin_addr) <= 0) {
        cerr << "[GameServer] Invalid IP address: " << IP << endl;
        close(listen_sock);
        return 1;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(listen_sock, (sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[GameServer] bind() failed");
        close(listen_sock);
        return 1;
    }

    if (listen(listen_sock, SOMAXCONN) < 0) {
        perror("[GameServer] listen() failed");
        close(listen_sock);
        return 1;
    }

    // === Connect to Data Server ===
    datafd = socket(AF_INET, SOCK_STREAM, 0);
    if (datafd < 0) {
        perror("[GameServer] socket() for Data Server failed");
        close(listen_sock);
        return 1;
    }

    sockaddr_in ds{};
    ds.sin_family = AF_INET;
    ds.sin_port = htons(DATA_SERVER_PORT);
    if (inet_pton(AF_INET, IP, &ds.sin_addr) <= 0) {
        cerr << "[GameServer] Invalid Data Server IP: " << IP << endl;
        close(listen_sock);
        close(datafd);
        return 1;
    }

    if (connect(datafd, (sockaddr *)&ds, sizeof(ds)) < 0) {
        perror("[GameServer] connect() to Data Server failed");
        close(listen_sock);
        close(datafd);
        return 1;
    }

    cout << "[GameServer] Connected to Data Server at " << IP << ":" << DATA_SERVER_PORT << endl;

    // === Create epoll ===
    int epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("[GameServer] epoll_create1() failed");
        close(listen_sock);
        close(datafd);
        return 1;
    }

    // === Make sockets non-blocking ===
    if (make_socket_non_blocking(listen_sock) < 0) {
        perror("[GameServer] make_socket_non_blocking() failed for listen_sock");
        close(listen_sock);
        close(datafd);
        close(epfd);
        return 1;
    }

    epoll_event ev{.events = EPOLLIN, .data = {.fd = listen_sock}};
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_sock, &ev) < 0) {
        perror("[GameServer] epoll_ctl(ADD listen_sock) failed");
        close(listen_sock);
        close(datafd);
        close(epfd);
        return 1;
    }

    cout << "[GameServer] Listening on " << IP << ":" << GAME_SERVER_PORT << " and ready!\n";

    epoll_event evs[MAX_EVENTS];
    while(true){
        int n=epoll_wait(epfd,evs,MAX_EVENTS,-1);
        for(int i=0;i<n;++i){int fd=evs[i].data.fd;
            if(fd==listen_sock){
                sockaddr_in c; socklen_t cl=sizeof(c); int cs=accept(listen_sock,(sockaddr*)&c,&cl);
                make_socket_non_blocking(cs);
                epoll_event ce{.events=EPOLLIN|EPOLLET,.data={.fd=cs}};epoll_ctl(epfd,EPOLL_CTL_ADD,cs,&ce);
                logineds.insert({cs,-1});
                cerr<<"new client with fd="<<fd<<endl;
            }else{
                string m=recv_message(fd);
                if(m=="Disconnected"){
                    close(fd);
                    logout_user(fd);
                    logineds.erase(fd);
                    continue;
                }
                // cerr<<m<<endl;
                if(fd==datafd){
                    cerr<<"unexpected msg from data_server:"<<m<<endl;
                    continue;
                }
                client_request(fd,m);
            }
        }
    }
}
