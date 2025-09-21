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
#include "game.h"

const int PORT=45632;
const char *IP="127.0.0.1";

using namespace std;

char buf[4096];
int main(){
    //init something
    //create socket
    int sockfd=socket(AF_INET,SOCK_STREAM,0);
    if(sockfd==-1){
        cerr<<"failed to create socket"<<endl;
        return -1;
    }
    //create ip
    sockaddr_in server;
    server.sin_family=AF_INET;
    server.sin_port=htons(PORT);
    inet_pton(AF_INET,IP,&server.sin_addr);
    //connect to server
    if(connect(sockfd,(sockaddr*)&server, sizeof(server))==-1){
        cerr<<"connection failed"<<endl;
        return -2;
    }

    //create a udp port

    int udpsockfd,udpPort;
    sockaddr_in servaddr;

    udpsockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return -3;
    }

    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(0);
    inet_pton(AF_INET,IP,&servaddr);

    if (bind(sockfd, (const struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind failed");
        close(sockfd);
        return -4;
    }
    udpPort=ntohs((servaddr.sin_port));
    //cout << "UDP Server started on port " << udpPort << endl;

    //choose login or register with username and passwd
    while(1){
        string input;
        cout<<"type something to login\nsyntax: \'[lr] {username} {passwd}\'"<<endl;
        getline(cin,input);
        input.append(" "+udpPort);
        send(sockfd,input.c_str(),input.size(),0);
        //syntax: "[lr] {username} {passwd} {port}"
        //wait for confirm from server
        int byteRecv=recv(sockfd,buf,sizeof(buf),0);
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
    int maxfd=udpsockfd;
    int ingame=0;//0: idle, 1:hosting game, 2:client game

    while(1){
        if(ingame){
            //state 2:in game
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
                sendto();
                cout<<"TCP server done, wait for connection"<<endl;
            }
            else{

            }
            
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
                        
                        ingame=1;
                    }
                    else if(i==udpsockfd){//invitation from another player

                        ingame=2;
                    }
                }
            }
        }
    }
}