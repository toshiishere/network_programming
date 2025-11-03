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

using json=nlohmann::json; using namespace std;

const int GAME_SERVER_PORT=45632, DATA_SERVER_PORT=45631;
const char *IP="127.0.0.1"; const int MAX_EVENTS=10;
int datafd; vector<int> clients;
unordered_map<int,int> logineds; // fd -> user id

int make_socket_non_blocking(int s){int f=fcntl(s,F_GETFL,0);return fcntl(s,F_SETFL,f|O_NONBLOCK);}

int logining(int fd, const std::string &action, const std::string &name, const std::string &password) {
    // --- Step 1. Ask data server for this user ---
    json query = {
        {"action", "query"},
        {"type", "user"},
        {"name", name}
    };
    send_message(datafd, query.dump());
    std::string reply = recv_message(datafd);

    if (reply.empty() || reply == "Disconnected") {
        cerr << "[GameServer] Data server unavailable or returned empty reply.\n";
        send_message(fd, json{{"response", "failed"}, {"reason", "data server unavailable"}}.dump());
        return 0;
    }

    json resp;
    try {
        resp = json::parse(reply);
    } catch (const std::exception &e) {
        cerr << "[GameServer] JSON parse error from data server: " << e.what() << "\nRaw: " << reply << endl;
        send_message(fd, json{{"response", "failed"}, {"reason", "invalid JSON from data server"}}.dump());
        return 0;
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
                return 0;
            }
        } else {
            // trying to register but user already exists
            send_message(fd, json{
                {"response", "failed"},
                {"reason", "user already exists"}
            }.dump());
            return 0;
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
            {"roomname", "-1"}
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
            return 0;
        }

        if (resp2.value("response", "failed") == "success") {
            send_message(fd, json{{"response", "success"}}.dump());
            cout << "[GameServer] Registered new user: " << name << endl;
            return 1;
        } else {
            send_message(fd, json{
                {"response", "failed"},
                {"reason", "data server create failed"}
            }.dump());
            return 0;
        }
    }

    // --- Step 4. Login failed because user doesn’t exist ---
    if (status == "failed" && action == "login") {
        send_message(fd, json{
            {"response", "failed"},
            {"reason", "user does not exist"}
        }.dump());
        cerr << "[GameServer] Login failed: no such user '" << name << "'\n";
        return 0;
    }

    // --- fallback ---
    send_message(fd, json{
        {"response", "failed"},
        {"reason", "unexpected error"}
    }.dump());
    cerr << "[GameServer] Unexpected condition in logining() for user '" << name << "'\n";
    return 0;
}


int client_request(int fd,const string&msg){
    json j; try{j=json::parse(msg);}catch(...){send_message(fd,json{{"response","failed"},{"reason","invalid JSON"}}.dump());return 0;}
    if(!logineds.count(fd)){int uid=logining(fd,j["action"],j["name"],j["password"]);if(uid>=0)logineds[fd]=uid;return uid>=0;}
    int uid=logineds[fd];string act=j["action"];
    // query self
    send_message(datafd,json{{"action","query"},{"type","user"},{"name",j["name"]}}.dump());
    json me=json::parse(recv_message(datafd))["data"];me["id"]=uid;

    if(act=="create"){ // room
        string room=j["room"],vis=j.value("visibility","public");
        json check={{"action","search"},{"type","room"}};send_message(datafd,check.dump());
        json r=json::parse(recv_message(datafd));for(auto &x:r["data"])
            if(x["name"]==room){send_message(fd,json{{"response","failed"},{"reason","duplicate room"}}.dump());return 0;}
        json newroom={{"name",room},{"hostUser",me["name"]},{"visibility",vis},{"inviteList",json::array()},{"status","idle"}};
        send_message(datafd,json{{"action","create"},{"type","room"},{"data",newroom}}.dump());
        send_message(datafd,json{{"action","query"},{"type","room"},{"name",room}}.dump());
        json qr=json::parse(recv_message(datafd));
        me["roomId"]=qr["data"]["id"];me["status"]="playing";
        send_message(datafd,json{{"action","update"},{"type","user"},{"data",me}}.dump());
        send_message(fd,json{{"response","success"}}.dump());return 1;
    }
    else if(act=="join"){
        string room=j["room"];json s={{"action","search"},{"type","room"}};send_message(datafd,s.dump());
        json sres=json::parse(recv_message(datafd));json target;
        for(auto&r:sres["data"])if(r["name"]==room)target=r;
        if(target.empty()){send_message(fd,json{{"response","failed"},{"reason","no such room"}}.dump());return 0;}
        if(target["status"]=="playing"){send_message(fd,json{{"response","failed"},{"reason","busy"}}.dump());return 0;}
        me["roomId"]=target["id"];me["status"]="playing";
        send_message(datafd,json{{"action","update"},{"type","user"},{"data",me}}.dump());
        send_message(fd,json{{"response","success"}}.dump());return 1;
    }
    else if(act=="curroom"){
        if(me["roomId"]==-1){send_message(fd,json{{"response","failed"},{"reason","not in room"}}.dump());return 0;}
        send_message(datafd,json{{"action","query"},{"type","room"},{"name",j.value("room","")}}.dump());
        json q=json::parse(recv_message(datafd));
        send_message(fd,json{{"response","success"},{"data",json::array({q["data"]})}}.dump());return 1;
    }
    else if(act=="curinvite"){
        json s={{"action","search"},{"type","room"}};send_message(datafd,s.dump());
        json res=json::parse(recv_message(datafd));json arr=json::array();
        for(auto&r:res["data"])if(r["visibility"]=="private")
            for(auto&x:r["inviteList"])if(x==uid)arr.push_back(r);
        send_message(fd,json{{"response",arr.empty()?"failed":"success"},{"data",arr.empty()?"no invites":arr}}.dump());return 1;
    }
    else if(act=="invite"){
        string uname=j["user"],room=j["room"];
        send_message(datafd,json{{"action","query"},{"type","user"},{"name",uname}}.dump());
        json u=json::parse(recv_message(datafd));
        if(u["response"]!="success"){send_message(fd,json{{"response","failed"},{"reason","no such user"}}.dump());return 0;}
        int tid=u["data"]["id"];
        send_message(datafd,json{{"action","query"},{"type","room"},{"name",room}}.dump());
        json rr=json::parse(recv_message(datafd));json roomj=rr["data"];
        if(roomj["hostUser"]!=me["name"]){send_message(fd,json{{"response","failed"},{"reason","not host"}}.dump());return 0;}
        auto&inv=roomj["inviteList"];bool ex=false;for(auto&x:inv)if(x==tid)ex=true;
        if(!ex)inv.push_back(tid);
        send_message(datafd,json{{"action","update"},{"type","room"},{"data",roomj}}.dump());
        send_message(fd,json{{"response","success"}}.dump());return 1;
    }
    send_message(fd,json{{"response","failed"},{"reason","unknown action"}}.dump());
    return 0;
}

int main(){
    int listen_sock=socket(AF_INET,SOCK_STREAM,0);
    sockaddr_in addr{};addr.sin_family=AF_INET;inet_pton(AF_INET,IP,&addr.sin_addr);
    addr.sin_port=htons(GAME_SERVER_PORT);bind(listen_sock,(sockaddr*)&addr,sizeof(addr));listen(listen_sock,SOMAXCONN);

    datafd=socket(AF_INET,SOCK_STREAM,0);
    sockaddr_in ds{};ds.sin_family=AF_INET;ds.sin_port=htons(DATA_SERVER_PORT);
    inet_pton(AF_INET,IP,&ds.sin_addr);
    connect(datafd,(sockaddr*)&ds,sizeof(ds));

    int epfd=epoll_create1(0);make_socket_non_blocking(listen_sock);
    epoll_event ev{.events=EPOLLIN,.data={.fd=listen_sock}};epoll_ctl(epfd,EPOLL_CTL_ADD,listen_sock,&ev);
    epoll_event evs[MAX_EVENTS];cout<<"[Game] server ready\n";
    while(true){
        int n=epoll_wait(epfd,evs,MAX_EVENTS,-1);
        for(int i=0;i<n;++i){int fd=evs[i].data.fd;
            if(fd==listen_sock){
                sockaddr_in c; socklen_t cl=sizeof(c); int cs=accept(listen_sock,(sockaddr*)&c,&cl);
                make_socket_non_blocking(cs);
                epoll_event ce{.events=EPOLLIN|EPOLLET,.data={.fd=cs}};epoll_ctl(epfd,EPOLL_CTL_ADD,cs,&ce);
                clients.push_back(cs);
            }else{
                string m=recv_message(fd);
                if(m=="Disconnected"){close(fd);clients.erase(remove(clients.begin(),clients.end(),fd),clients.end());continue;}
                client_request(fd,m);
            }
        }
    }
}
