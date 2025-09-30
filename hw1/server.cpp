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
#include <sstream>
#include <ctime>
#include <set>
using namespace std;

const int PORT=45632;
// const char *IP="140.113.235.151";
const char *IP="127.0.0.1";
const string PLAYERFILE="players.txt";
const string HISTORYFILE="game_history.txt";


//create vector of live player
map<int, string> logined_players;//fd, playerInfo
set<string> logined_players_set;
map<int,int> alive_players;//fd, state(-1:not loggined, 0:loggined)

string getCurrentTime() {
    time_t now = time(0);
    tm *ltm = localtime(&now);
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H:%M:%S", ltm);
    return string(buffer);
}

void write_history(string messages){
    ofstream outFile(HISTORYFILE, ios::app); // append to file
    stringstream ss(messages);
    string winner, loser;
    ss>>winner>>loser;
    //TODO if need validate the players
    if (!outFile) {
        cerr << "Error opening file for writing!" << endl;
        return;
    }
    outFile << winner <<" "<<loser <<" "<<getCurrentTime()<<"\n";
    outFile.close();
    return;
}
void browse_logined(int fd){
    stringstream ss;
    for (auto  t :logined_players) {
        ss << t.second <<" with fd= " <<t.first <<endl;
    }
    string msg = ss.str();  
    send(fd, msg.c_str(), msg.size(), 0);
}

int update_login_count(string name){
    std::ifstream infile(PLAYERFILE);
    std::ofstream tempfile("temp.txt");

    std::string word, passwd;
    int value;

    while (infile >> word >>passwd>> value) {
        if (word == name) {
            value ++; // modify here
        }
        tempfile << word << " " <<passwd<<" "<< value << "\n";
    }

    infile.close();
    tempfile.close();

    // Replace original file with temp file
    remove(PLAYERFILE.c_str());
    rename("temp.txt", PLAYERFILE.c_str());
    return 1;
}

void look_up_history(){
    map<string, int> wincount;
    cout<<"Here's the list of win count"<<endl;
    ifstream inFile(HISTORYFILE);
    if (!inFile) {
        cerr << "Error opening file for reading!" << endl;
        return;
    }
    string line,a,b,_;
    while (getline(inFile, line)) {
        stringstream ss(line);
        ss>>a>>b>>_;
        wincount[a]++;
    }
    inFile.close();
    for(auto t:wincount){
        cout<<t.first<<" has won "<<t.second<<" times"<<endl;
    }
}
//0:success, -1:serious error, -2:wrong passwd, -3:player not found ,-4 register dulplicate, -5:other
int login(vector<string> messages,int fd){
    if(messages[0]=="l"){
        ifstream inFile(PLAYERFILE);
        if (!inFile) {
            cerr << "Error opening file for reading!" << endl;
            return -1;
        }
        string line;
        while (getline(inFile, line)) {
            string username, passwd;
            int count;
            stringstream ss(line);
            ss >> username >> passwd>> count;
            if(messages[1]==username){
                if(messages[2]==passwd){
                    if(logined_players_set.find(messages[1])!=logined_players_set.end()){
                        cout<<"he/she has logged in, use another account"<<endl;
                        return -2;
                    }
                    logined_players.insert({fd, username});
                    logined_players_set.insert(username);
                    cout<<"player "<<username<<" log in successfully"<<endl;
                    char msg[100];
                    snprintf(msg,sizeof(msg),"y %d",count);
                    send(fd,msg,strlen(msg),0);
                    alive_players[fd]=0;
                    update_login_count(username);
                    return 0;
                }
                else{
                    cout<<"player "<<username<<" failed to login"<<endl;
                    return -2;
                }
            }
        }
        inFile.close();
        cout<<"player "<<messages[1]<<" not found"<<endl;
        return -3;
    }
    else if(messages[0]=="r"){
        ifstream inFile(PLAYERFILE);
        if (!inFile) {
            cerr << "Error opening file for reading!" << endl;
            return -1;
        }
        string line;
        while (getline(inFile, line)) {
            stringstream ss(line);
            string username, passwd;
            ss >> username >> passwd;
            if(messages[1]==username){
                cout<<"player "<<messages[1]<<" dulplicated"<<endl;
                return -3;  
            }
        }
        inFile.close();

        ofstream outFile(PLAYERFILE, ios::app); // append to file
        if (!outFile) {
            cerr << "Error opening file for writing!" << endl;
            return -1;
        }
        outFile << messages[1]<<" "<<messages[2] << " 1"<< "\n";
        outFile.close();

        logined_players.insert({fd, messages[1]});
        logined_players_set.insert(messages[1]);
        cout<<"player "<<messages[1]<<" log in successfully"<<endl;
        send(fd,"y 0",4,0);
        alive_players[fd]=0;
        return 0;
    }
    else{
        cerr<<"failed to login/register player fd="<<fd<<endl;
        return -5;
    }
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
        perror("bind failed");
        return -2;
    }
    if (listen(listening, SOMAXCONN) == -1) {
        perror("listen");
        return -3;
    }
    cout<<"listening at port "<<PORT<<endl;


    //deal with fds, check response from cin and player's game result
    fd_set master, readfd;
    FD_ZERO(&master);
    FD_SET(listening,&master);
    FD_SET(0,&master);
    int maxfd=listening;
    cout<<"To quit, type quit; To query history game, type query_history; To view online players, type query_player"<<endl;
    while(1){
        readfd=master;
        int change=select(maxfd+1,&readfd,NULL,NULL,NULL);
        if(change==-1){
            cerr<<"select failed";
            return -6;
        }
        if(!change)continue;
        for(int i=0;i<=maxfd;i++){
            if(FD_ISSET(i,&readfd)){

                if(i==0){//deal with I/O of server
                    string input;
                    getline(cin,input);
                    if(input=="quit"){
                        //close all port
                        for(auto t : alive_players){
                            close(t.first);
                        }
                        close(listening);
                        return 0;
                    }
                    else if(input=="query_history"){
                        look_up_history();
                    }
                    else if(input=="query_player"){
                        for(auto t:alive_players){
                            cout<<"username: "<<(t.second?"unknown":logined_players[t.first])<<" with fd="<<t.first<<endl;
                        }
                    }
                    else{
                        cout<<"IDK what u talking, say again"<<endl;
                    }
                }

                else if(i==listening){//accept new client, update not_logined
                    sockaddr_in client;
                    socklen_t clientSize=sizeof(client);
                    int newfd=accept(listening,(sockaddr*)&client, &clientSize);
                    if (newfd == -1) {
                        // cerr<<"fail to accept new client"<<endl;
                        perror("accept");
                        continue;
                    }
                    
                    alive_players.insert({newfd,-1});
                    FD_SET(newfd, &master);
                    if(maxfd<newfd) maxfd=newfd;
                    cout<<"new client with fd="<<newfd<<endl;
                }
                else{//active player's message
                    char buf[4096];
                    int byteRecv=recv(i,buf,sizeof(buf),0);
                    if(byteRecv==-1){
                        cerr<<"recv failed"<<endl;
                        continue;
                    }
                    //if >0 can be either
                    else if(byteRecv==0){//disconnect and update alive_player and possibly loggined
                        if(logined_players.find(i)!=logined_players.end()){
                            string toexit=logined_players[i];
                            cout<<"player "<<toexit<<" with fd="<<i<< " has log out"<<endl;
                            logined_players.erase(i);
                            logined_players_set.erase(toexit);
                        }
                        else{ 
                            cout<<"player un-registered with fd="<<i<< " has exited"<<endl;
                        }
                        alive_players.erase(i);
                        FD_CLR(i,&master);
                        close(i);
                    }
                    else{
                        buf[byteRecv] = '\0';
                        if(logined_players.find(i)!=logined_players.end()){//logined player
                            //request of alive player
                            //endgame update game history
                            string st(buf);
                            write_history(st);
                        }

                        else{// login or register
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
                            int resultLogin=login(messages,i);
                            if(resultLogin==-1){
                                return -7;
                            }
                            else if(resultLogin<-1){
                                string errorcode="the player failed to login\n here's are the error codes -2:wrong passwd, -3:player not found ,-4 register dulplicate, -5:other";
                                errorcode.append("\nyour error code:"+resultLogin);
                                send(i,errorcode.c_str(),errorcode.size(),0);
                            }
                        }
                    }
                }
            }
        }
    }
    
}