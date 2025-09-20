#include<iostream>
#include<sys/types.h>
#include<unistd.h>
#include<sys/socket.h>
#include<netdb.h>
#include<arpa/inet.h>
#include<string.h>
#include<string>

using namespace std;

int main(){
    //create socket
    int listening = socket(AF_INET,SOCK_STREAM, 0);
    if(listening==-1){
        cerr<<"socket creation failed";
        return -1;
    }
    
    //bind to ip
    sockaddr_in hint;
    hint.sin_family=AF_INET;
    hint.sin_port=htons(56432);

    inet_pton(AF_INET, "0.0.0.0", &hint.sin_addr);

    if(bind(listening, (sockaddr*)&hint, sizeof(hint))==-1){
        cerr<<"cannot bind";
        return -2;
    }
    
    //listening
    if(listen(listening,SOMAXCONN)==-1){
        cerr<<"cannot listen";
        return -3;
    }
    //accept call
    sockaddr_in client;
    socklen_t clientSize = sizeof(client);
    char host[NI_MAXHOST];
    char svc[NI_MAXSERV];
    int clientSocket=accept(listening,(sockaddr*)&client, &clientSize);
    if(clientSocket==-1){
        cerr<<"acceptance failed";
        return -4;
    }

    //closing socket
    close(listening);
    memset(host,0,sizeof(host));
    memset(svc, 0, sizeof(svc));

    int result= getnameinfo((sockaddr*)&client, clientSize, host, NI_MAXHOST, svc, NI_MAXSERV, 0);
    if(result){
        cerr<<host<<" connect on "<< svc<<endl;
    }
    else{
        inet_ntop(AF_INET, &client.sin_addr, host, NI_MAXHOST);
        cerr<<host<<" connect on "<<ntohs(client.sin_port)<<endl;
    }
    //receiving and display and echo message
    char buf[4096];
    while(1){
        memset(buf, 0, 4096);
        int byteRecv=recv(clientSocket, buf, 4096, 0);
        if(byteRecv==-1){
            cerr<<"connection error";
            return -5;
        }
        if(byteRecv==0){
            cout<<"disconnected"<<endl;
            break;
        }
        cout<< "received: "<<string(buf, 0, byteRecv)<<endl;
        send(clientSocket, buf, byteRecv+1, 0);

    }
    close(clientSocket);
    return 0;
}