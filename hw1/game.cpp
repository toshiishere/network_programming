#include "game.h"
#include <sys/select.h>
#include <stdlib.h>
#include <iostream>
#include <sys/socket.h>
#include <string>
#include <string.h>
#include <unistd.h>

#include <fstream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <algorithm>

#include <streambuf>
#include <cerrno>
#include <csignal>

using namespace std;
char buf[1024];

pair<int, int> computeScore(const string &secret, const string &guess) {
    int bulls = 0, cows = 0;
    int len = secret.size();

    // track letters
    vector<int> secretCount(256, 0), guessCount(256, 0);

    for (int i = 0; i < len; i++) {
        if (guess[i] == secret[i]) {
            bulls++;
        } else {
            secretCount[(unsigned char)secret[i]]++;
            guessCount[(unsigned char)guess[i]]++;
        }
    }

    for (int i = 0; i < 256; i++) {
        cows += min(secretCount[i], guessCount[i]);
    }

    return {bulls, cows};
}

class BroadcastBuf : public std::streambuf {
    int sockfd;
    std::streambuf* coutBuf;

public:
    BroadcastBuf(int fd) : sockfd(fd), coutBuf(std::cout.rdbuf()) {}

protected:
    // write one char to cout + socket
    virtual int overflow(int ch) override {
        if (ch == EOF) return !EOF;

        // write to cout first
        if (coutBuf->sputc(ch) == EOF) return EOF;

        // write to socket
        char c = static_cast<char>(ch);
        if (::send(sockfd, &c, 1, MSG_NOSIGNAL) == -1) {
            if (errno == EPIPE || errno == ECONNRESET) {
                std::cerr << "Broadcast: peer disconnected" << std::endl;
            } else {
                perror("Broadcast send");
            }
            return EOF; // signals failure → ostream sets badbit
        }
        return ch;
    }

    // flush underlying cout
    virtual int sync() override {
        if (coutBuf->pubsync() == -1) return -1;
        return 0;
    }
};

// Wrapper ostream that uses BroadcastBuf
class BroadcastStream : public std::ostream {
    BroadcastBuf buf;

public:
    BroadcastStream(int fd) : std::ostream(&buf), buf(fd) {}
};


// =============================
// SendBuf
// =============================
class SendBuf : public std::streambuf {
    int sockfd;

public:
    SendBuf(int fd) : sockfd(fd) {}

protected:
    virtual int overflow(int ch) override {
        if (ch == EOF) return !EOF;

        char c = static_cast<char>(ch);
        if (::send(sockfd, &c, 1, MSG_NOSIGNAL) == -1) {
            if (errno == EPIPE || errno == ECONNRESET) {
                std::cerr << "Send: peer disconnected" << std::endl;
            } else {
                perror("Send");
            }
            return EOF; // signals failure → ostream sets badbit
        }
        return ch;
    }

    virtual int sync() override {
        return 0; // nothing extra to flush
    }
};

// Ostream wrapper using SendBuf
class SendStream : public std::ostream {
    SendBuf buf;

public:
    SendStream(int fd) : std::ostream(&buf), buf(fd) {}
};


int client_game(int oppofd, string opponame){
    cout<<"game starting"<<endl;
    fd_set master, readfd;
    int maxfd=oppofd;
    FD_ZERO(&master);
    FD_SET(oppofd,&master);
    FD_SET(0,&master);
    while(1){
        readfd=master;
        if(select(oppofd+1,&readfd,NULL,NULL,NULL)==-1){
            cerr<<"select failed"<<endl;
            continue;
        }
        if(FD_ISSET(0, &readfd)){
            string line;
            getline(cin, line);
            send(oppofd,line.c_str(),line.size(),0);
        }
        if(FD_ISSET(oppofd, &readfd)){
            int byteRecv=recv(oppofd,buf,sizeof(buf),0);
            if(byteRecv==-1){
                cerr<<"recv failed"<<endl;
                continue;
            }
            //if >0 can be either
            else if(byteRecv==0){//disconnected
                cout<<"your opponent has exited, no one won"<<endl;
                break;
            }
            else{
                buf[byteRecv] = '\0';
                string st(buf);
                cout<<st;
                if(st.size()>=17 && st.compare(0,17,"Congratulations!")==0){
                    return 1;
                }
            }
        }
    }
}


int host_game(int oppofd, string opponame){
    //pick a random word from the file
    BroadcastStream broadcast(oppofd);
    SendStream sendout(oppofd);
    srand(time(nullptr));
    ifstream fin("game_lib.txt");
    if (!fin) {
        cerr << "Failed to open game_lib.txt\n";
        return -1;
    }
    vector<string> words;
    string line;
    while (getline(fin, line)) {
        if (!line.empty())
            words.push_back(line);
    }
    fin.close();
    if (words.empty()) {
        cerr << "No words in game_lib.txt\n";
        return -1;
    }
    // 2. Pick a random word
    string secret = words[rand() % words.size()];
    int wordLen = secret.size();
    broadcast  << "------------------------------------------"<<endl;
    broadcast  << "Welcome to the XaXb game!"<<endl;
    broadcast  << "I picked a word of length " << wordLen << ". Try to guess it!"<<endl;

    // Gameplay loop
    int turn,result;
    for(turn=1; ;turn++){
        if(turn % 2){//host's turn
            string guess;
            cout << username<<"'s guess: ";
            cin >> guess;

            if (guess.size() != wordLen) {
                cout << "Guess must be length " << wordLen << "!\n";
                turn--;
                continue;
            }

            auto [a, b] = computeScore(secret, guess);
            cout <<"you guessed "<<guess <<" and got "<< a << "a" << b << "b\n";
            sendout << username << " guessed " << guess << " and got "<< a << "a" << b << "b\n";

            if (a == wordLen) {
                broadcast << "Congratulations! "<<username<<" guessed the word: " << secret << "\n";
                return 1;
            }
        }
        else{//client's turn
            sendout << opponame <<"'s guess: "<<endl;

            int byteRecv=recv(oppofd,buf,sizeof(buf),0);
            if(byteRecv==-1){
                cerr<<"recv failed"<<endl;
                turn--;
                continue;
            }
            else if(byteRecv==0){//disconnected
                cout<<"your opponent has exited, no one won"<<endl;
                break;
            }

            string guess(buf,0,byteRecv);

            if (guess.size() != wordLen) {
                sendout << "Guess must be length " << wordLen << "!\n";
                turn--;
                continue;
            }

            auto [a, b] = computeScore(secret, guess);
            sendout <<"you guessed "<<guess <<" and got "<< a << "a" << b << "b\n";
            cout << opponent_username << " guessed " << guess << " and got "<< a << "a" << b << "b\n";

            if (a == wordLen) {
                broadcast << "Congratulations! "<<opponent_username <<" guessed the word: " << secret << "\n";
                return 2;
            }
        }
    }
    return -1;
}