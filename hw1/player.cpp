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
#include "game.h"

const int PORT=45632;
const char *IP="127.0.0.1";

using namespace std;

char buf[4096];
string username,passwd,_,opponent_username;

struct player{
    string name;
    int port;
};
//create vector of live player
map<player, int> logined_players;//username, port

int request_logined_players(int serverfd){
    logined_players.clear();
    send(serverfd, "r", 2, 0);
    int byteRecv=recv(serverfd, buf, sizeof(buf) ,0);
    buf[byteRecv]='\0';
    string recved=string(buf,0,byteRecv), user;
    int port;
    stringstream ss(recved);
    while(ss>>user>>port){
        player p;
        p.name=user;
        p.port=port;
        logined_players.insert(p);
    }
    return 0;
}

int main(){
    //init something
    //create socket
    int lobbyfd=socket(AF_INET,SOCK_STREAM,0);
    if(lobbyfd==-1){
        cerr<<"failed to create socket"<<endl;
        return -1;
    }
    //create ip
    sockaddr_in server;
    server.sin_family=AF_INET;
    server.sin_port=htons(PORT);
    inet_pton(AF_INET,IP,&server.sin_addr);
    //connect to server
    if(connect(lobbyfd,(sockaddr*)&server, sizeof(server))==-1){
        cerr<<"connection failed"<<endl;
        return -2;
    }

    //create a udp port

    int udpsockfd,udpPort;
    sockaddr_in servaddr;

    udpsockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpsockfd < 0) {
        perror("socket creation failed");
        return -3;
    }

    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(0);
    inet_pton(AF_INET,IP,&servaddr);

    if (bind(udpsockfd, (const struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind failed");
        close(udpsockfd);
        return -4;
    }
    udpPort=ntohs((servaddr.sin_port));
    //cout << "UDP Server started on port " << udpPort << endl;

    //choose login or register with username and passwd
    while(1){
        string input;
        cout<<"type something to login\nsyntax: \'[lr] {username} {passwd}\'"<<endl;
        getline(cin,input);

        stringstream ss(input);
        ss >> _ >> username >> passwd;

        input.append(" "+udpPort);
        send(lobbyfd,input.c_str(),input.size(),0);
        //syntax: "[lr] {username} {passwd} {port}"
        //wait for confirm from server
        int byteRecv=recv(lobbyfd,buf,sizeof(buf),0);
        if(buf[0]=='y')break;
        cout<<"failed to login, try again";
    }

    cout<<"login success, now you r in lobby"<<endl;
    cout<<"To browse online players, type \'r\'; To start a game, type \'i {username}\'"<<endl;

    fd_set master, readfd;
    FD_ZERO(&master);
    FD_SET(0,&master);
    FD_SET(udpsockfd,&master);
    sockaddr_in opponent;
    int opponent_fd;
    int maxfd=max(udpsockfd,lobbyfd);
    int ingame=0;//0: idle, 1:hosting game, 2:client game

    while(1){
        if(ingame){
            //in game
            //who ever do send_invitation&TCP_info and listen&accept become the host.
            //handles game. when done, return result to the server side.
            if(ingame==1){
                //create socket
                int listening=socket(AF_INET,SOCK_STREAM, 0);
                if(listening==-1){
                    cerr<<"fail to create socket for game"<<endl;
                    return -6;
                }
                //create ip
                sockaddr_in server;
                server.sin_family=AF_INET;
                server.sin_port=htons(0);
                inet_pton(AF_INET,IP,&server.sin_addr);
                //bind
                if(bind(listening,(sockaddr*)&server,sizeof(server))==-1){
                    cerr<<"bind failed for game"<<endl;
                    return -7;
                }
                cout<<"TCP server done, wait for connection"<<endl;

                int opponent_fd=accept(listening,(sockaddr*)&opponent, sizeof(opponent));
                if(opponent_fd==-1){
                    cerr<<"acceptance failed";
                    return -8;
                }
                close(listening);
            }
            else{
                int opponent_fd = socket(AF_INET, SOCK_STREAM, 0);
                if (opponent_fd == -1) {
                    cerr << "Failed to create socket\n";
                    return -1;
                }
                
                if (inet_pton(AF_INET, opponent.c_str(), &opponent.sin_addr) <= 0) {
                    cerr << "Invalid IP address\n";
                    return -2;
                }
                // 3. Connect
                if (connect(opponent_fd, (sockaddr*)&opponent, sizeof(opponent)) == -1) {
                    cerr << "Connection failed\n";
                    close(opponent_fd);
                    return -3;
                }
            }
            int result;
            string sendResult;
            if(ingame==1){
                result=host_game();
            }
            else{
                result=client_game();
            }
            sendResult=(result?username:opponent_username)+" "+(result?opponent_username:username)+"\n";
            send(lobbyfd,sendResult.c_str,sendResult.size(),0);
            ingame=0;
        }
        //state 1:idle
        //either send request or wait for request or quit
        else{
            readfd=master;
            if(select(maxfd+1,&readfd,NULL,NULL,NULL)==-1){
                cerr<<"failed to select"<<endl;
                return -5;
            }
            for(int i=0;i<=maxfd;i++){
                if(FD_ISSET(i,&readfd)){
                    if(i==0){//player cin I/O
                        string input;
                        if (getline(cin,input)) {
                            cout << "EOF on stdin, closing\n";
                            break;
                        }
                        if(input=="r"){
                            request_logined_players();
                            //TODO print all the player except myself
                            for(auto t:logined_players){

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
                            opponent.port=logined_players[input].port;
                        }
                        else{
                            cout<<"wrong syntax, error"<<endl;
                            continue;
                        }
                        send(udpsockfd,)
                        ingame=1;
                    }
                    else if(i==udpsockfd){//invitation from another player
                        //update opponent
                        ingame=2;
                    }
                }
                if(ingame)break;//in case of send and accept corrupt
            }
        }
    }
}