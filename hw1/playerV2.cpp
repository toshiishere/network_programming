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
#include<netdb.h>


const int LOBBYPORT=45632;
// const char *IP="140.113.235.151";
const char *IP="127.0.0.1";
const int MINUDPPORT=45700;
const int MAXUDPPORT=45799;

using namespace std;

char buf[4096];
string username,passwd,_,__,opponent_username;
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
        if(p==udpsock)continue;
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

        int rv = select(udpsock + 1, &readfds, nullptr, nullptr, &tv);
        if (rv < 0) {
            perror("select");
            break;
        } else if (rv == 0) {
            // timeout: no more data
            break;
        }
        if (FD_ISSET(udpsock, &readfds)) {
            char buf[2048];
            sockaddr_in from{};
            socklen_t fromlen = sizeof(from);
            ssize_t n = recvfrom(udpsock, buf, sizeof(buf)-1, 0, (sockaddr*)&from, &fromlen);
            logined_players.clear();
            if (n > 0) {
                buf[n] = '\0';
                int fromPort = ntohs(from.sin_port);
                string name(strchr(buf,' '));
                logined_players[name]=fromPort;
            }
        }
        auto now = chrono::steady_clock::now();
        remaining_ms = wait_ms - static_cast<int>(chrono::duration_cast<chrono::milliseconds>(now - start).count());
    }
    return 0;
}

int send_invite_and_setup_server(int udpsock, int oppo_port){
    sockaddr_in opponent;
    socklen_t opposize=sizeof(opponent);
    opponent.sin_family=AF_INET;
    opponent.sin_port=htons(oppo_port);
    inet_pton(AF_INET,IP,&opponent.sin_addr);
    //send invitation to opponent
    char msg[128]="i ";
    strcpy(msg,opponent_username.c_str());
    sendto(udpsock, msg, (2+opponent_username.size()), 0, (sockaddr*)&opponent.sin_addr, opposize);

    // Wait for response for 10 seconds
    auto start = chrono::steady_clock::now();
    int wait_ms = 10000;  // 10 seconds timeout
    int remaining_ms = wait_ms;

    while (1) {
        if(remaining_ms <= 0){
            std::cout<<"timed out"<<endl;
            return -1;
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
            return -2;
        }
        
        if (FD_ISSET(udpsock, &readfds)) {
            char buf[2048];
            sockaddr_in from{};
            socklen_t fromlen = sizeof(from);
            ssize_t n = recvfrom(udpsock, buf, sizeof(buf)-1, 0, (sockaddr*)&from, &fromlen);

            if (n > 0) {
                buf[n] = '\0';  // Null terminate the received message
                //int fromPort = ntohs(from.sin_port);
                if(buf[0]=='a'){//fromPort==oppo_port &&   IDK if to add this
                    std::cout<<"invitation accepted, procede to sending TCP info"<<endl;
                    break;
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
    game_server.sin_port=htons(0);
    inet_pton(AF_INET, IP, &game_server.sin_addr);
    if(bind(listening, (sockaddr*)&game_server, sizeof(game_server))==-1){
        cerr<<"fail to bind";
        return -2;
    }
    if(listen(listening, SOMAXCONN)==-1){
        cerr<<"fail to listen";
        return -3;
    }
    char TCPinfo[20];
    sprintf(TCPinfo, "%d", ntohs(game_server.sin_port));

    sendto(udpsock, msg, (5), 0, (sockaddr*)&opponent.sin_addr, opposize);

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
        return -2;
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

        cout << "Client connected from " << inet_ntoa(client_addr.sin_addr)
             << ":" << ntohs(client_addr.sin_port) <<" connected successfully"<< endl;
    }
    close(listening);
    return client_fd;
}

int got_invite(int udpsock){// retunr 0 if rejected, -1 if error, fd number if success
    sockaddr_in from{};
    socklen_t fromlen = sizeof(from);
    ssize_t n = recvfrom(udpsock, buf, sizeof(buf)-1, 0, (sockaddr*)&from, &fromlen);//'from' is updated

    string msg(buf,0,n);
    stringstream ss(msg);
    ss>>_>>opponent_username;

    cout<<"invitation from player "<<opponent_username<<", do you accept? [y|n]";
    cin>>_;
    if (_ != "y") {
        sendto(udpsock, "N", 2, 0, (sockaddr*)&from, fromlen);  // Send rejection
        cout << "Rejecting invitation..." << endl;
        return 0;
    }
    sendto(udpsock, "Y", 2, 0, (sockaddr*)&from, fromlen);  // Send acceptance
    cout << "Accepted invitation from " << opponent_username << "!" << endl;

    // Receive the TCP port number for the game
    int byteRecv = recv(udpsock, buf, sizeof(buf), 0);
    string game_port_msg(buf, 0, byteRecv);
    int game_port = stoi(game_port_msg);

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

    // 3. Connect to the server
    //    - check for errors if connection fails
    if(connect(sockfd,(sockaddr*)&opponentTCP, sizeof(opponentTCP))==-1){
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
    return sockfd;  // Indicating successful invitation acceptance
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
    int udpSock = bind_udp_in_range(MAXUDPPORT, MINUDPPORT);
    if (udpSock == -1) {
        return 1;
    }

    fd_set master, readfd;
    FD_ZERO(&master);
    FD_SET(0,&master);
    FD_SET(udpSock,&master);
    int state=0, maxfd=max(udpSock,0);
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
                            if(scan_for_player(udpSock)<0){
                                cerr<<"scan failed somehow"<<endl;
                                return -10;
                            }
                            if(logined_players.size()<1){
                                cout<<"you are the only one online, wait"<<endl;
                                continue;
                            }
                            cout<<"here's the available players"<<endl;
                            for(auto t:logined_players){
                                if(t.first==username)continue;
                                cout<<t.first<<endl;
                            }
                        }
                        else if(input[0]=='i'){
                            input=input.substr(2);
                            scan_for_player(udpSock);
                            if(logined_players.size()<1){
                                cout<<"you are the only one online, wait"<<endl;
                                continue;
                            }
                            if(logined_players.find(input)==logined_players.end()){
                                cout<<"player not found"<<endl;
                                continue;
                            }
                            opponent_username=input;
                            if(send_invite_and_setup_server(udpSock, logined_players[input])<0){
                                cout<<"you are either rejected or time out-ed"<<endl;
                                continue;
                            }
                            cout<<"process to game"<<endl;
                            state=1;
                        }
                    }
                    else if(i==udpSock){//invitation from another player
                        //update opponent
                        got_invite();
                        /*
                        

                        */
                        
                        ingame=2;
                    }
                }
            }

        }

    }

}
