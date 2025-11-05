#include <iostream>
#include <fstream>
#include <unordered_map>
#include <string>
#include <csignal>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "utility.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;
using namespace std;

const int DATA_SERVER_PORT = 45631;
const char *IP = "127.0.0.1";

unordered_map<int, json> users;
unordered_map<int, json> rooms;
int user_cnt = 0;
int room_cnt = 0;
int sockfd;

// --- Save and Load ---
void saveUsers() {
    json data = json::array();
    for (auto &[id, user] : users) {
        data.push_back(user);
    }
    ofstream out("data/users.json");
    if (!out.is_open()) {
        cerr << "[DataServer] Failed to open users.json for write\n";
        return;
    }
    out << data.dump(4);
    out.close();
    cout << "[DataServer] Users saved.\n";
}

unordered_map<int, json> loadUsers(const string &filename) {
    unordered_map<int, json> res;
    ifstream in(filename);
    if (!in.is_open()) {
        cerr << "[DataServer] No user file found, starting fresh.\n";
        return res;
    }

    json data;
    try {
        in >> data;
        for (auto &u : data) {
            int id = u.value("id", user_cnt);
            json user = {
                {"id", id},
                {"name", u.value("name", "")},
                {"password", u.value("password", "")},
                {"last_login", u.value("last_login", now_time_str())},
                {"status", "offline"},
                {"roomName", u.value("roomName", "-1")}
            };
            res[id] = user;
            user_cnt = max(user_cnt, id + 1);
        }
    } catch (...) {
        cerr << "[DataServer] Failed to parse user file.\n";
    }
    return res;
}

// --- Core Operations ---
json normalize_user(json data) {
    return {
        {"id", data.value("id", -1)},
        {"name", data.value("name", "")},
        {"password", data.value("password", "")},
        {"last_login", data.value("last_login", now_time_str())},
        {"status", data.value("status", "idle")},
        {"roomName", data.value("roomName", "-1")}
    };
}

json normalize_room(json data) {
    return {
        {"id", data.value("id", -1)},
        {"name", data.value("name", "")},
        {"hostUser", data.value("hostUser", "")},
        {"oppoUser", data.value("oppoUser", "")},
        {"visibility", data.value("visibility", "public")},
        {"inviteList", data.value("inviteList", json::array())},
        {"status", data.value("status", "idle")}
    };
}

int op_create(const string &type, json data) {
    if (type == "user") {
        json u = normalize_user(data);
        u["id"] = user_cnt++;
        users[u["id"]] = u;
        return u["id"];
    } else if (type == "room") {
        json r = normalize_room(data);
        r["id"] = room_cnt++;
        rooms[r["id"]] = r;
        cerr << "[DataServer] Created room: " << r.value("name", "(unnamed)")
             << " (id=" << r["id"] << ", host=" << r.value("hostUser", "(unknown)")
             << ", visibility=" << r.value("visibility", "public") << ")" << endl;
        return r["id"];
    } else if (type == "gamelog") {
        ofstream out("data/gamelog.json", ios::app);
        if (!out.is_open()) return -1;
        out << data.dump() << endl;
        return 1;
    }
    return -1;
}

json op_query(const string &type, const json &request) {
    json res;
    if (!(type == "user" || type=="room")) {
        res["response"] = "failed";
        res["reason"] = "unsupported type";
        return res;
    }

    // query by name or id
    string name = request.value("name", "");
    int id = request.value("id", -1);
    // cerr<<"querying "<<type<<" with name="<<name<<" id="<<id<<endl;

    if (type == "user") {
        if (id >= 0 && users.count(id)) {
            res["response"] = "success";
            res["data"] = users[id];
            // cerr<<"response with id, data:"<<res.dump()<<endl;
            return res;
        }

        if (!name.empty()) {
            for (auto &[uid, user] : users) {
                if (user.value("name", "") == name) {
                    res["response"] = "success";
                    res["data"] = user;
                    return res;
                }
            }
        }

        res["response"] = "failed";
        res["reason"] = "no such user";
        return res;
    } else if (type == "room") {
        if (id >= 0 && rooms.count(id)) {
            res["response"] = "success";
            res["data"] = rooms[id];
            cerr << "[DataServer] Queried room by id=" << id << ": " << rooms[id].value("name", "(unnamed)") << endl;
            return res;
        }

        if (!name.empty()) {
            for (auto &[rid, room] : rooms) {
                if (room.value("name", "") == name) {
                    res["response"] = "success";
                    res["data"] = room;
                    cerr << "[DataServer] Queried room by name='" << name << "' (id=" << room.value("id", -1) << ")" << endl;
                    return res;
                }
            }
        }

        res["response"] = "failed";
        res["reason"] = "no such room";
        cerr << "[DataServer] Query room failed: " << (id >= 0 ? "id=" + to_string(id) : "name='" + name + "'") << " not found" << endl;
        return res;
    }

    res["response"] = "failed";
    res["reason"] = "unexpected error";
    return res;
}

json op_search(const string &type) {
    json arr = json::array(), res;
    if (type == "user") {
        for (auto &[id, user] : users)
            if (user.value("status", "") == "idle")
                arr.push_back(user);
        res["response"] = arr.empty() ? "failed" : "success";
        if (arr.empty()) {
            res["reason"] = "no user online";
        } else {
            res["data"] = arr;
        }
    } else if (type == "room") {
        for (auto &[id, room] : rooms)
            if (room.value("visibility", "") == "public")
                arr.push_back(room);
        res["response"] = arr.empty() ? "failed" : "success";
        if (arr.empty()) {
            res["reason"] = "no available room";
            cerr << "[DataServer] Search rooms: no public rooms available" << endl;
        } else {
            res["data"] = arr;
            cerr << "[DataServer] Search rooms: found " << arr.size() << " public room(s)" << endl;
        }
    } else {
        res["response"] = "failed";
        res["reason"] = "unsupported type";
    }
    return res;
}

int op_update(const string &type, json data) {
    if (type != "user" && type != "room") return -1;
    if (!data.contains("id") || data["id"].is_null()) return -1;

    int id;
    try {
        if (data["id"].is_string())
            id = stoi(data["id"].get<string>());
        else
            id = data["id"].get<int>();
    } catch (...) {
        cerr << "[DataServer] Invalid id type in update\n";
        return -1;
    }

    if (type == "user" && users.count(id)) {
        json merged = users[id];
        for (auto &[k, v] : data.items()) merged[k] = v;
        users[id] = normalize_user(merged);
        cout << "[DataServer] Updated user id=" << id << " status=" << users[id]["status"] << endl;
        return 1;
    } else if (type == "room" && rooms.count(id)) {
        json merged = rooms[id];
        for (auto &[k, v] : data.items()) merged[k] = v;
        rooms[id] = normalize_room(merged);
        cerr << "[DataServer] Updated room id=" << id << " (" << rooms[id].value("name", "(unnamed)")
             << ", status=" << rooms[id].value("status", "idle") << ")" << endl;
        return 1;
    }

    cerr << "[DataServer] Update failed: " << type << " id=" << id << " not found\n";
    return -1;
}


int op_delete(const string &type, const string &name) {
    if (type == "room") {
        for (auto it = rooms.begin(); it != rooms.end(); ++it) {
            if (it->second.value("name", "") == name) {
                cerr << "[DataServer] Deleted room: " << name << " (id=" << it->first << ")" << endl;
                rooms.erase(it);
                return 1;
            }
        }
        cerr << "[DataServer] Delete room failed: '" << name << "' not found" << endl;
    }
    return -1;
}

// --- Signal handler ---
void signal_handler(int) {
    cout << "\n[DataServer] Caught SIGINT, saving users and exiting.\n";
    saveUsers();
    close(sockfd);
    exit(0);
}

// --- Main server loop ---
int main() {
    signal(SIGINT, signal_handler);
    users = loadUsers("data/users.json");

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DATA_SERVER_PORT);
    inet_pton(AF_INET, IP, &addr.sin_addr);

    if (bind(listen_fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    listen(listen_fd, SOMAXCONN);
    cout << "[DataServer] Listening on " << IP << ":" << DATA_SERVER_PORT << " ...\n";

    sockaddr_in client{};
    socklen_t len = sizeof(client);
    sockfd = accept(listen_fd, (sockaddr *)&client, &len);
    if (sockfd < 0) {
        perror("accept");
        return 1;
    }
    cout << "[DataServer] Connected to game server.\n";
    close(listen_fd);

    while (true) {
        string msg = recv_message(sockfd);
        if (msg.empty() || msg == "Disconnected") {
            cout << "[DataServer] Game server disconnected.\n";
            break;
        }
        // cerr<<msg<<endl;
        json request;
        try {
            request = json::parse(msg);
        } catch (const exception &e) {
            cerr << "[DataServer] Invalid JSON: " << e.what() << "\nRaw: " << msg << endl;
            continue;
        }

        string action = request.value("action", "");
        string type = request.value("type", "");
        json response;

        try {
            if (action == "create") {
                json data = request["data"].is_string() ? json::parse(request["data"].get<string>()) : request["data"];
                int result = op_create(type, data);
                response["response"] = result >= 0 ? "success" : "failed";
                if (result >= 0) response["id"] = result;
                if (result < 0) response["reason"] = "create failed";
            }
            else if (action == "query") {
                response = op_query(type, request);
            }
            else if (action == "search") {
                response = op_search(type);
            }
            else if (action == "update") {
                json data = request["data"].is_string() ? json::parse(request["data"].get<string>()) : request["data"];
                int result = op_update(type, data);
                response["response"] = result > 0 ? "success" : "failed";
                if (result <= 0) response["reason"] = "update failed";
            }
            else if (action == "delete") {
                string name = request["data"].is_string() ? request["data"].get<string>() : "";
                int result = op_delete(type, name);
                response["response"] = result > 0 ? "success" : "failed";
                if (result <= 0) response["reason"] = "delete failed";
            }
            else {
                response["response"] = "failed";
                response["reason"] = "unknown action";
            }
        } catch (const exception &e) {
            cerr << "[DataServer] Exception in action handler: " << e.what() << endl;
            response = {{"response", "failed"}, {"reason", e.what()}};
        }

        send_message(sockfd, response.dump());
    }

    close(sockfd);
    saveUsers();
    return 0;
}
