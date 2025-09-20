// server_skeleton.cpp
// Practice skeleton for a multi-client chat server using select()

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
#include <set>

using namespace std;

int main() {
    // 1. Create a listening socket (IPv4, TCP)
    int listening = socket(AF_INET, SOCK_STREAM, 0);
    if(listening==-1){
        cerr<<"fail to create listening socket";
        return -1;
    }

    // 2. Bind the socket to an IP and port
    sockaddr_in hint;
    hint.sin_family=AF_INET;
    hint.sin_port=htons(56432);
    inet_pton(AF_INET, "0.0.0.0", &hint.sin_addr);
    if(bind(listening, (sockaddr*)&hint, sizeof(hint))==-1){
        cerr<<"fail to bind";
        return -2;
    }

    // 3. Put the socket into listening mode
    if(listen(listening, SOMAXCONN)==-1){
        cerr<<"fail to listen";
        return -3;
    }

    // 4. Initialize fd_set "master" to track all sockets
    //    Add the listening socket to master
    //    Track fdmax = highest file descriptor
    fd_set master, readfd;
    int fdmax=listening;
    FD_ZERO(&master);
    FD_SET(listening, &master);
    set<int> clients;

    // 5. Event loop:
    //    - Copy master into read_fds before each select()
    //    - Call select(fdmax+1, &read_fds, NULL, NULL, NULL)
    while(1){
        readfd=master;
        int change=select(fdmax+1, &readfd, NULL, NULL, NULL);
        if(change==-1){
            cerr<<"select failed";
            return -4;
        }
        if(change>0){
            for(int i=0;i<=fdmax;i++){
                if(FD_ISSET(i, &readfd)){
                    if(i==listening){
                        sockaddr_in client;
                        socklen_t client_t=sizeof(client);
                        int newclient = accept(i, (sockaddr*)&client, &client_t);
                        if(newclient==-1){
                            cerr<<"fail to accept new client"<<endl;
                            continue;
                        }
                        clients.insert(newclient);
                        FD_SET(newclient, &master);
                        if(fdmax<newclient) fdmax=newclient;
                        cout<<"new client with fd = "<<newclient<<endl;
                    }
                    else{
                        char buf[4096];
                        memset(buf, 0, sizeof(buf));
                        int received=recv(i, buf, sizeof(buf), 0);
                        if(received==-1){
                            cerr << "client with fd = " << i << " is in error state" << endl;
                            continue;
                        }
                        else if(received==0){
                            cout<<"client with fd = "<<i << "is disconnected"<<endl;
                            clients.erase(i);
                            close(i);
                            FD_CLR(i,&master);
                        }
                        else{
                            string msg = string(buf, 0, received);
                            cout << "Client " << i << " says: " << msg;
                            for(int c:clients){
                                if(c==i)continue;
                                send(c,msg.c_str(),msg.size(),0);
                            }
                        }
                    }
                }
            }
        }
    }
    
    return 0;
}
