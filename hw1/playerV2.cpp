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
#include "game.h"
#include <chrono>


const int LOBBYPORT=45632;
// const char *IP="140.113.235.151";
const char *IP="127.0.0.1";
const int MINUDPPORT=45700;
const int MAXUDPPORT=45799;

using namespace std;

char buf[4096];
string username,passwd,_,opponent_username;
map<string,int> logined_players;//name, port

//return socket fd
int bind_udp_in_range(int minPort, int maxPort) {
    int sockfd;
    sockaddr_in addr{};

    for (int port = minPort; port <= maxPort; port++) {
        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) {
            cerr<<"failed to create socket for udp"<<endl;
            return -1;
        }

        addr.sin_family = AF_INET;
        inet_pton(AF_INET, IP, &addr.sin_addr);
        addr.sin_port = htons(port);

        if (bind(sockfd, (sockaddr*)&addr, sizeof(addr)) == 0) {
            cout << "Bound UDP socket on port " << port << "\n";
            return sockfd; // success
        }
        close(sockfd); // try next port
    }

    cerr << "No available port in range " << minPort << "-" << maxPort << "\n";
    return -1;
}

int scan_for_player(int udpsock){
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
        dest.sin_port = htons(p);
        ssize_t sent = sendto(udpsock, msg, strlen(msg), 0, (sockaddr*)&dest, sizeof(dest));
        if (sent < 0) {
            // cerr<<"failed to send, but no worries"<<endl;
        }
    }

    auto start = chrono::steady_clock::now();
    int wait_ms=1000;    
    int remaining_ms = 1000;

    while (remaining_ms > 0) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(udpsock, &readfds);

        timeval tv;
        tv.tv_sec = remaining_ms / 1000;
        tv.tv_usec = (remaining_ms % 1000) * 1000;

        int rv = select(sock + 1, &readfds, nullptr, nullptr, &tv);
        if (rv < 0) {
            perror("select");
            break;
        } else if (rv == 0) {
            // timeout: no more data
            break;
        }

        if (FD_ISSET(sock, &readfds)) {
            char buf[2048];
            sockaddr_in from{};
            socklen_t fromlen = sizeof(from);
            ssize_t n = recvfrom(sock, buf, sizeof(buf)-1, 0, (sockaddr*)&from, &fromlen);
            if (n > 0) {
                buf[n] = '\0';
                int fromPort = ntohs(from.sin_port);
                replies.emplace_back(fromPort, string(buf, n));
            }
        }

        auto now = chrono::steady_clock::now();
        remaining_ms = wait_ms - static_cast<int>(chrono::duration_cast<chrono::milliseconds>(now - start).count());
    }


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
        cerr<<"connection failed"<<endl;
        return -2;
    }

    //choose login or register with username and passwd
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
        if(buf[0]=='y')break;
        cout<<"failed to login, try again";
    }
    cout<<"login success, now you r in lobby"<<endl;
    
    cout<<"creating UDP port for be scanned"<<endl;
    int updSock = bind_udp_in_range(MAXUDPPORT, MINUDPPORT);
    if (updSock == -1) {
        return 1;
    }

    fd_set master, readfd;
    FD_ZERO(&master);
    FD_SET(0,&master);
    FD_SET(updSock,&master);
    int state=0, maxfd=max(updSock,0);
    cout<<"start waiting for the game.\n you can either type \'s\' to scan for available players\n and type \'i {name}\' for inviting the player for a game";

    /*
        inf loop for the following states
        case 0:
            scan cin, udpServer for valid command
        case 1:
            hosting the game
        case 2:
            joining someone's game
    */
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
                            scan_for_player(udpSock);
                            //TODO print all the player except myself
                            cout<<"here's the available players"<<endl;
                            for(auto t:logined_players){
                                if(t.first==username)continue;
                                cout<<t.first<<endl;
                            }
                        }
                        else if(input[0]=='i'){
                            input=input.substr(2);
                            request_logined_players();
                            if(logined_players.size()<2){
                                cout<<"you are the only one online, wait"<<endl;
                                continue;
                            }
                            if(logined_players.find(input)==logined_players.end()){
                                cout<<"player not found"<<endl;
                                continue;
                            }
                            //found player to play
                            opponent.sin_family = AF_INET;
                            opponent.sin_port = htons(logined_players[input].port);
                            inet_pton(AF_INET, IP, &opponent.sin_addr);

                            string invitation = "I " + to_string(udpPort)+" " + username;
                            sendto(udpsockfd,invitation.c_str(),invitation.size(),0,(sockaddr*)&opponent,sizeof(opponent));
                            //send invitation through udp

                            //wait for acceptence
                            int byteRecv=recv(udpsockfd,buf,sizeof(buf),0);
                            buf[byteRecv]='\0';
                            if(buf[0]!='Y')continue;//invitation rejected, discarded

                            string tcpinfo = to_string(ntohs(server.sin_port()));
                            sendto(udpsockfd,tcpinfo.c_str(),tcpinfo.size(),0,(sockaddr*)&opponent,sizeof(opponent));
                        }
                        else{
                            cout<<"wrong syntax, error"<<endl;
                            continue;
                        }
                        ingame=1;
                    }
                    else if(i==udpsockfd){//invitation from another player
                        //update opponent
                        int byteRecv=recv(udpsockfd,buf,sizeof(buf),0);
                        string msg(buf,0,byteRecv);
                        stringstream ss(msg);
                        ss>>_>>_>>opponent_username;
                        opponent.sin_family=AF_INET;
                        opponent.sin_port=htons(stoi(_));
                        inet_pton(AF_INET, IP, opponent.sin_addr);
                        cout<<"invitation from player "<<opponent_username<<", do you accept? [y|n]";
                        cin>>_;
                        if(_!="y"){
                            sendto(udpsockfd,"N", 2, 0, (sockaddr*)&opponent, sizeof(opponent));
                            cout<<"rejecting invitation"<<endl;
                            continue;
                        }
                        sendto(udpsockfd,"Y", 2, 0, (sockaddr*)&opponent, sizeof(opponent));

                        int byteRecv=recv(udpsockfd,buf,sizeof(buf),0);
                        string msg(buf,0,byteRecv);
                        opponent.sin_port=htons(stoi(msg));

                        ingame=2;
                    }
                }

        }

    }

}