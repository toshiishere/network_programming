#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>          // for memset
#include <unistd.h>         // for close()
#include <sys/types.h>      // for socket types
#include <sys/socket.h>     // for socket(), bind(), listen(), accept()
#include <netinet/in.h>     // for sockaddr_in
#include <arpa/inet.h>      // for htons(), inet_ntop()
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <future>
#include "game.h"
std::string username;
std::string opponent_username;
#include <chrono>
// #include<netdb.h/>


const int LOBBYPORT=45632;
// const char *IP="140.113.235.151";
const char *IP="127.0.0.1";
const int MINUDPPORT=45700;
const int MAXUDPPORT=45799;

using namespace std;

string passwd,_,__;
map<string,int> logined_players;//name, port

//return socket fd
int bind_udp_in_range(int minPort, int maxPort, int &port) {
    int sockfd;
    sockaddr_in addr{};

    for (port = minPort; port <= maxPort; port++) {
        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) {
            cerr<<"failed to create socket for udp"<<endl;
            return -1;
        }

        addr.sin_family = AF_INET;
        inet_pton(AF_INET, IP, &addr.sin_addr);
        addr.sin_port = htons(port);

        if (bind(sockfd, (sockaddr*)&addr, sizeof(addr)) == 0) {
            cerr << "Bound UDP socket on port " << port << "\n";
            return sockfd; // success
        }
        close(sockfd); // try next port
    }
    cerr << "No available port in range " << minPort << "-" << maxPort << "\n";
    return -1;
}

int scan_for_player(int udpsock, int udpForwardPort){
    char msg[]="r";
    // prepare destination template
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    if (inet_pton(AF_INET, IP, &dest.sin_addr) <= 0) {
        cerr << "inet_pton failed for targetIP\n";
        return -1;
    }

    // 1) blast messages to all ports
    for (int p = MINUDPPORT; p <= MAXUDPPORT; ++p) {
        if(p==udpForwardPort)continue;
        dest.sin_port = htons(p);
        ssize_t sent = sendto(udpsock, msg, strlen(msg), 0, (sockaddr*)&dest, sizeof(dest));
        if (sent < 0) {
            // cerr<<"failed to send, but no worries"<<endl;
        }
    }

    auto start = chrono::steady_clock::now();
    int wait_ms=1000;    
    int remaining_ms = 1000;
    logined_players.clear();
    while (remaining_ms > 0) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(udpsock, &readfds);

        timeval tv;
        tv.tv_sec = remaining_ms / 1000;
        tv.tv_usec = (remaining_ms % 1000) * 1000;

        int rv = select(udpsock + 1, &readfds, nullptr, nullptr, &tv);
        if (rv < 0) {
            perror("select");
            break;
        } else if (rv == 0) {
            // timeout: no more data
            break;
        }
        if (FD_ISSET(udpsock, &readfds)) {
            char buf[1024];
            sockaddr_in from{};
            socklen_t fromlen = sizeof(from);
            ssize_t n = recvfrom(udpsock, buf, sizeof(buf) - 1, 0,
                                (sockaddr*)&from, &fromlen);

            if (n > 0) {
                buf[n] = '\0';  // null-terminate

                std::istringstream iss(buf);
                std::string name;

                if (iss >> _ >> name && _=="R") {
                    logined_players[name] = ntohs(from.sin_port);
                    // std::cout << "Discovered player: " << name<< ", source port " << ntohs(from.sin_port) << ")\n";
                } else {
                    std::cerr << "Malformed UDP message: '" << buf << "'\n";
                }
            } else if (n == 0) {
                cerr << "recvfrom returned 0 (empty datagram)\n";
            } else {
                perror("recvfrom failed");
            }
        }
        auto now = chrono::steady_clock::now();
        remaining_ms = wait_ms - static_cast<int>(chrono::duration_cast<chrono::milliseconds>(now - start).count());
    }
    return 0;
}
//return client fd, -1 if error, -2 if rejected, -3 if time out
int send_invite_and_setup_server(int udpsock, int oppo_port){
    sockaddr_in opponent;
    opponent.sin_family=AF_INET;
    opponent.sin_port=htons(oppo_port);
    inet_pton(AF_INET,IP,&opponent.sin_addr);
    //send invitation to opponent
    string msg="i "+username;
    sendto(udpsock, msg.c_str(), msg.size(), 0,(sockaddr*)&opponent, sizeof(opponent));

    cout<<"invitation sent, let's see"<<endl;

    // Wait for response for 10 seconds
    auto start = chrono::steady_clock::now();
    int wait_ms = 10000;  // 10 seconds timeout
    int remaining_ms = wait_ms;

    while (1) {
        if(remaining_ms <= 0){
            cout<<"timed out"<<endl;
            return -3;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(udpsock, &readfds);

        timeval tv;
        tv.tv_sec = remaining_ms / 1000;
        tv.tv_usec = (remaining_ms % 1000) * 1000;

        int rv = select(udpsock + 1, &readfds, nullptr, nullptr, &tv);
        if (rv < 0) {
            perror("select");
            break;
        } else if (rv == 0) {
            // Timeout: no data received within the specified time
            std::cout << "Timeout: No response received within 10 seconds." << endl;
            return -3;
        }
        
        if (FD_ISSET(udpsock, &readfds)) {
            char buf[2048];
            sockaddr_in from{};
            socklen_t fromlen = sizeof(from);
            ssize_t n = recvfrom(udpsock, buf, sizeof(buf)-1, 0, (sockaddr*)&from, &fromlen);

            if (n > 0) {
                buf[n] = '\0';  // Null terminate the received message
                //int fromPort = ntohs(from.sin_port);
                if(buf[0]=='Y'){//fromPort==oppo_port &&   IDK if to add this
                    cout<<"invitation accepted, procede to sending TCP info"<<endl;
                    break;
                }
                if(buf[0]=='N'){
                    cout<<"invitation rejected..."<<endl;
                    return -2;
                }
            }
        }

        // Calculate the remaining time for the next iteration
        auto now = chrono::steady_clock::now();
        remaining_ms = wait_ms - static_cast<int>(chrono::duration_cast<chrono::milliseconds>(now - start).count());
    }
    //if success, build TCP and send info

    // 1. Create a listening socket (IPv4, TCP)
    int listening = socket(AF_INET, SOCK_STREAM, 0);
    if(listening==-1){
        cerr<<"fail to create listening socket";
        return -1;
    }

    // 2. Bind the socket to an IP and port
    sockaddr_in game_server;
    game_server.sin_family=AF_INET;
    game_server.sin_port=0;
    inet_pton(AF_INET, IP, &game_server.sin_addr);
    HANDLE:
    if(bind(listening, (sockaddr*)&game_server, sizeof(game_server))==-1){
        if(errno==EADDRINUSE)goto HANDLE;//when conflict
        cerr<<"fail to bind";
        return -1;
    }
    if(listen(listening, SOMAXCONN)==-1){
        cerr<<"fail to listen";
        return -1;
    }
    sockaddr_in actual_addr{};
    socklen_t len = sizeof(actual_addr);
    if (getsockname(listening, (sockaddr*)&actual_addr, &len) == -1) {
        cerr << "getsockname failed";
        return -1;
    }

    string TCPinfo = to_string(ntohs(actual_addr.sin_port));
    cerr << "Listening on port: " << TCPinfo << endl;
    sendto(udpsock, TCPinfo.c_str(), sizeof(TCPinfo), 0, (sockaddr*)&opponent, sizeof(opponent));

    // 5. Set up select() timeout
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(listening, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 10;  // 10 seconds timeout
    timeout.tv_usec = 0;

    // 6. Wait for a client to connect (with timeout)
    cout << "Waiting for a client to connect..." << endl;
    int activity = select(listening + 1, &readfds, NULL, NULL, &timeout),client_fd;
    
    if (activity == -1) {
        cerr << "Select failed" << endl;
        close(listening);
        return -1;
    }
    else if (activity == 0) {
        cout << "Timeout: No connection after 10 seconds" << endl;
        close(listening);
        return -3;
    }
    else {
        // A client is trying to connect, accept the connection
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        client_fd = accept(listening, (sockaddr*)&client_addr, &client_len);
        if (client_fd == -1) {
            cerr << "Failed to accept connection" << endl;
            close(listening);
            return -1;
        }

        cerr << "Client connected from " << inet_ntoa(client_addr.sin_addr)
             << ":" << ntohs(client_addr.sin_port) <<" connected successfully"<< endl;
    }
    close(listening);
    return client_fd;
}

// retunr 0 if rejecting, -1 if error, fd number if success, -4 if just be scanned, -2 time out
int got_something(int udpsock){
    
    sockaddr_in from{};
    socklen_t fromlen = sizeof(from);
    char buf[1024];
    ssize_t n = recvfrom(udpsock, buf, sizeof(buf)-1, 0, (sockaddr*)&from, &fromlen);//'from' is updated
    buf[n]='\0';
    //check if just scanning
    if(buf[0]=='r'){
        string msg="R "+username;
        sendto(udpsock, msg.c_str(), msg.size(), 0, (sockaddr*)&from, fromlen);
        return -4;
    }
    
    
    if(buf[0]=='i'){
        string st(buf);
        stringstream ss(st);
        ss>>_>>opponent_username;
        // stringstream ss(msg);
        // ss>>_>>opponent_username;

        cout<<"invitation from player "<<opponent_username<<", do you accept? [y|n]"<<endl;
        future<string> result = async(launch::async, [] {
            string input;
            getline(cin, input);
            return input;
        });
        string input;
        // Wait up to 10 seconds
        if (result.wait_for(chrono::seconds(9)) == future_status::ready) {
            input = result.get();
        } else {
            cout << "Timeout! You didn't answer in time." << endl;
            return 0;
        }
        if (input != "y") {
            sendto(udpsock, "N", 2, 0, (sockaddr*)&from, fromlen);  // Send rejection
            cout << "Rejecting invitation..." << endl;
            return 0;
        }
        sendto(udpsock, "Y", 2, 0, (sockaddr*)&from, fromlen);  // Send acceptance
        cout << "Accepted invitation from " << opponent_username << "!" << endl;

        // Receive the TCP port number for the game
        int byteRecv = recv(udpsock, buf, sizeof(buf), 0);
        buf[byteRecv]='\0';
        cerr<<string(buf)<<endl;
        int game_port=atoi(buf);

        // 1. Create a socket (IPv4, TCP)
        int sockfd=socket(AF_INET, SOCK_STREAM, 0);
        if(sockfd==-1){
            cerr<<"failed to create socket"<<endl;
            return -1;
        }
        sockaddr_in opponentTCP;

        opponentTCP.sin_family=AF_INET;
        opponentTCP.sin_port=htons(game_port);
        inet_pton(AF_INET, IP, &opponentTCP.sin_addr);

        // 3. Connect to the server check for errors if connection fails
        if(connect(sockfd,(sockaddr*)&opponentTCP, sizeof(opponentTCP))==-1){
            cerr<<"connection failed"<<endl;
            return -2;
        }
        cout << "Connected to server. Type messages and press Enter.\n";
        return sockfd;  // Indicating successful invitation acceptance
    }
    return -1;
}

int report_game_result(int result, int state, int lobbyfd){//state 1:hosting, state 2:joining, result 1:win 0 lose
    if(state==2)return 0;
    string winner, loser;
    if(result){
        winner=username;
        loser=opponent_username;
    }
    else{
        winner=opponent_username;
        loser=username;
    }
    string msg=winner+" "+loser;
    send(lobbyfd,msg.c_str(),msg.size(),0);
    return 0;
}

int main(){
    //create socket
    int lobbyfd=socket(AF_INET,SOCK_STREAM,0);
    if(lobbyfd==-1){
        cerr<<"failed to create socket"<<endl;
        return -1;
    }
    //create ip
    sockaddr_in lobbyAddr;
    lobbyAddr.sin_family=AF_INET;
    lobbyAddr.sin_port=htons(LOBBYPORT);
    inet_pton(AF_INET,IP,&lobbyAddr.sin_addr);
    //connect to lobby
    if(connect(lobbyfd,(sockaddr*)&lobbyAddr, sizeof(lobbyAddr))==-1){
        cerr<<"lobby connection failed"<<endl;
        return -2;
    }
    cout<<"welcome to game lobby. Please complete the login/register"<<endl;

    //choose login or register with username and passwd
    char buf[1024];
    while(1){
        string input;
        cout<<"type something to login\nsyntax: \'[lr] {username} {passwd}\'"<<endl;
        getline(cin,input);

        stringstream ss(input);
        ss >> _ >> username >> passwd;

        send(lobbyfd,input.c_str(),input.size(),0);
        //syntax: "[lr] {username} {passwd} {port}"
        //wait for confirm from server
        
        int byteRecv=recv(lobbyfd,buf,sizeof(buf),0);
        buf[byteRecv]='\0';
        // cout<<buf<<endl;
        if(buf[0]=='y')break;
        cout<<"failed to login, try again";
    }
    string msg(buf+2);
    // cerr<<msg<<endl;
    cout<<"you have been logined "<<msg<<" times"<<endl;
    cout<<"login success, now you r in lobby"<<endl;
    
    cerr<<"creating UDP port for be scanned"<<endl;
    int udpForwardPort;
    int udpSock = bind_udp_in_range(MINUDPPORT, MAXUDPPORT,udpForwardPort);
    if (udpSock == -1) {
        return 1;
    }

    fd_set master, readfd;
    FD_ZERO(&master);
    FD_SET(0,&master);
    FD_SET(udpSock,&master);
    FD_SET(lobbyfd,&master);
    int state=0, maxfd=max(udpSock,lobbyfd);
    cout<<"start waiting for the game.\nyou can either type \'s\' to scan for available players\nor type \'i {name}\' for inviting the player for a game"<<endl;

    /*
        inf loop for the following states
        case 0:
            scan cin, udpServer for valid command
        case 1:
            hosting the game
        case 2:
            joining someone's game
    */
   int client_fd;
    while(1){
        if(!state){
            readfd=master;
            if(select(maxfd+1,&readfd,NULL,NULL,NULL)==-1){
                cerr<<"failed to select"<<endl;
                return -5;
            }
            for(int i=0;i<=maxfd;i++){
                if(FD_ISSET(i,&readfd)){
                    if(i==0){//player cin I/O
                        string input;
                        if (!getline(cin,input)) {
                            cout << "EOF on stdin, closing\n";
                            break;
                        }
                        if(input=="s"){
                            cout<<"start scanning..."<<endl;
                            if(scan_for_player(udpSock,udpForwardPort)<0){
                                cerr<<"scan failed somehow"<<endl;
                                return -10;
                            }
                            if(logined_players.size()<1){
                                cout<<"you are the only one online, wait for another player"<<endl;
                                continue;
                            }
                            cout<<"here's the available players"<<endl;
                            for(auto t:logined_players){
                                if(t.first==username)continue;
                                cout<<t.first<<endl;
                            }
                            cout<<"-----------below are empty------------"<<endl;
                            cout<<"\'s\' to scan for available players or \'i {name}\' for inviting the player for a game"<<endl;
                        }
                        else if(input[0]=='i'){
                            input=input.substr(2);
                            scan_for_player(udpSock,udpForwardPort);
                            if(logined_players.size()<1){
                                cout<<"you are the only one online, wait for another player"<<endl;
                                continue;
                            }
                            if(logined_players.find(input)==logined_players.end()){
                                cout<<"player not found"<<endl;
                                continue;
                            }
                            opponent_username=input;
                            client_fd=send_invite_and_setup_server(udpSock, logined_players[input]);
                            if(client_fd<0){
                                if(client_fd==-3)cout<<"you are either time out-ed"<<endl;
                                continue;
                            }
                            cout<<"process to game"<<endl;
                            state=1;
                        }
                        else if(input[0]=='q'){
                            cout<<"quitting the porgram"<<endl;
                            return 0;
                        }
                    }
                    else if(i==udpSock){//invitation from another player
                        client_fd=got_something(udpSock);
                        if(client_fd==-4)continue;
                        if(client_fd<0){
                            cout<<"connection failed"<<endl;
                            continue;
                        }
                        if(client_fd==0){
                            continue;
                        }
                        cout<<"process to game"<<endl;
                        state=2;
                    }
                    else if(i==lobbyfd){
                        char buf[1024];
                        int n=recv(i,buf, sizeof(buf),0);
                        if(n==0){
                            cout<<"server is down, leaving here"<<endl;
                            return 0;
                        }
                        buf[n]='\0';
                        //DO something to respond to server, but currently nope
                    }
                }
            }

        }
        else{//in game
            int result;
            if(state==1)result=host_game(client_fd,opponent_username);
            else result=client_game(client_fd,opponent_username);
            if(result<0){
                cout<<"game not finished, nothing recorded"<<endl;
            }
            else{
                report_game_result(result, state, lobbyfd);
            }
            cout<<"you are back to lobby"<<endl;
            state=0;
            shutdown(client_fd,SHUT_RDWR);
            close(client_fd);
        }

    }

}
