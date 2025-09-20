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

void write_history(char *buf){
    return;
}

int main(){
    //init something
    //create socket
    //create ip
    //bind

    //create vector of live player
    //deal with fds, check response from cin and player's game result
    //update to game_histroy
}