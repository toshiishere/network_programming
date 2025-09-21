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
#include <string.h>
#include <map>

using namespace std;

const int PORT=45632;
const char *IP="127.0.0.1";

struct player{
    string name;
    int port;
};

void write_history(char *buf){
    return;
}

void look_up_history(){

}

int main(){
    //init something
    //create socket
    int listening=socket(AF_INET,SOCK_STREAM, 0);
    if(listening==-1){
        cerr<<"fail to create socket"<<endl;
        return -1;
    }
    //create ip
    sockaddr_in server;
    server.sin_family=AF_INET;
    server.sin_port=htons(45632);
    inet_pton(AF_INET,IP,&server.sin_addr);
    //bind
    if(bind(listening,(sockaddr*)&server,sizeof(server))==-1){
        cerr<<"bind failed"<<endl;
        return -2;
    }

    //create vector of live player
    map<int, player> logined_players;//fd, playerInfo
    map<int,int> alive_players;
    //deal with fds, check response from cin and player's game result
    fd_set master, readfd;
    FD_ZERO(&master);
    FD_SET(listening,&master);
    FD_SET(0,&master);
    int maxfd=listening;
    while(1){
        readfd=master;
        if(select(maxfd+1,&readfd,NULL,NULL,NULL)==-1){
            cerr<<"select failed";
            continue;
        }
        for(int i=0;i<=listening;i++){
            if(FD_ISSET(i,&readfd)){
                if(i==0){//deal with I/O of server
                    string input;
                    getline(cin,input);
                    if(input=="quit"){
                        //close all port TODO
                        return 0;
                    }
                    else if(input=="query"){
                        look_up_history();
                    }
                    else{
                        cout<<"IDK what u talking, say again"<<endl;
                    }
                }
                else if(i==listening){//accept new client, update not_logined
                    sockaddr_in client;
                    socklen_t clientSize=sizeof(client);
                    int newfd=accept(i,(sockaddr*)&client, &clientSize);
                    if(newfd==-1){
                        cerr<<"acceptance failed"<<endl;
                        continue;
                    }
                    string welcomeMsg="welcome to game lobby\n complete the login/register\nformat: [l/r] [playername] [password]";
                    send(newfd,welcomeMsg.c_str(),welcomeMsg.size(),0);
                    alive_players.insert({newfd,-1});
                    FD_SET(newfd, &master);
                    if(maxfd<newfd) maxfd=newfd;
                    cout<<"new client with fd="<<newfd<<endl;
                }
                else{
                    char buf[4096];
                    int byteRecv=recv(i,buf,sizeof(buf),0);
                    if(byteRecv==-1){
                        cerr<<"recv failed"<<endl;
                        continue;
                    }
                    //if >0 can be either
                    else if(byteRecv==0){//disconnect and update alive_player and possibly loggined
                        if(logined_players.find(i)!=logined_players.end()){
                            player toexit=logined_players[alive_players[i]];
                            cout<<"player "<<toexit.name<<" with fd="<<i<< "has log out"<<endl;
                            logined_players.erase(i);
                        }
                        else{ 
                            cout<<"player un-registered with fd="<<i<< "has exited"<<endl;
                        }
                        alive_players.erase(i);
                        FD_CLR(i,&master);
                    }
                    else{
                        if(logined_players.find(i)!=logined_players.end()){//logined player
                            //request of alive player
                            //endgame update game history
                        }
                        else{// login or register
                            buf[byteRecv] = '\0';
                            vector<string> messages;
                            char *p = strtok(buf, " ");
                            while (p != NULL) {
                                messages.emplace_back(p);
                                p = strtok(NULL, " ");
                            }
                            if(messages.size()>3){
                                send(i,"your syntax is incorrect, not space in username and passwd",59,0);
                                continue;
                            }
                            if(messages[0]=="l"){//loggin
                                
                            }
                            else if(messages[0]=="r"){//register new account

                            }
                            else{
                                send(i,"your syntax is incorrect, not space in username and passwd",59,0);
                                continue;
                            }
                        }
                    }
                }
            }
        }
    }
    
}