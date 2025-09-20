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

using namespace std;

struct player{
    int fd;
    string name;
    int port;
}player;


int main(){
    //init something
    //create socket
    //create ip
    //bind
    //connect
    //choose login or register
    //type in name and passwd
    //wait for confirm from server

    //setup udp listening server
    while(1){
        //state 1:idle
        //either send request or wait for request or quit
        //state 2:in game
        //who ever do listen+accept become the host.
        //handles game. when done, return result to the server side.
    }
}