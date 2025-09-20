// echo_client_skeleton.cpp
// Practice skeleton for a simple TCP echo client

#include <iostream>
#include <string>
#include<string.h>
#include <unistd.h>         // for close()
#include <sys/types.h>      // for socket types
#include <sys/socket.h>     // for socket(), connect(), send(), recv()
#include <netinet/in.h>     // for sockaddr_in
#include <arpa/inet.h>      // for htons(), inet_pton()

using namespace std;

const int PORT=56432;
const string server_ip="127.0.0.1";

int main() {
    // 1. Create a socket (IPv4, TCP)
    int sockfd=socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd==-1){
        cerr<<"failed to create socket"<<endl;
        return -1;
    }

    // 2. Fill in the server address (family, port, IP)
    //    - port should match your server’s listening port
    //    - use "127.0.0.1" for localhost
    sockaddr_in server;

    server.sin_family=AF_INET;
    server.sin_port=htons(PORT);
    inet_pton(AF_INET, server_ip.c_str(), &server.sin_addr);

    // 3. Connect to the server
    //    - check for errors if connection fails
    if(connect(sockfd,(sockaddr*)&server, sizeof(server))==-1){
        cerr<<"connection failed"<<endl;
        return -2;
    }

    // 4. Main loop:
    //    - read input from user (cin / getline)
    //    - if input == "quit", break
    //    - send the input to the server
    //    - recv response from server
    //    - print server’s reply
    cout << "Connected to server. Type messages and press Enter.\n";
    fd_set master, readfd;
    FD_ZERO(&master);
    FD_SET(sockfd, &master);
    FD_SET(STDIN_FILENO, &master);
    int fdmax = max(STDIN_FILENO, sockfd);
    char buf[4096];
    string input;

    while(1){
        readfd=master;
        if(select(fdmax+1,&readfd,NULL,NULL,NULL)==-1){
            cerr<<"select failed"<<endl;
            continue;
        }
        if(FD_ISSET(STDIN_FILENO,&readfd)){
            if (!getline(cin, input)) {
                cout << "EOF on stdin, closing\n";
                break;
            }
            input+='\n';
            if(send(sockfd,input.c_str(),input.size(),0)==-1){
                cerr<<"send failed"<<endl;
                continue;
            }
        }
        if(FD_ISSET(sockfd,&readfd)){
            int recvBytes=recv(sockfd,buf,sizeof(buf),0);
            if(recvBytes<=0){
                cout<<"server side disconnected"<<endl;
                break;
            }
            cout << string(buf, 0, recvBytes);
            //cout.flush();
        }
        
    }

    // 5. Close the socket and exit
    close(sockfd);
    return 0;
}
