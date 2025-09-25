
#ifndef GAME_H   // Guards against multiple inclusions
#define GAME_H
#include <string>
extern std::string username,opponent_username;
// Function declarations (prototypes)
int host_game(int oppofd, std::string opponame);  // Declaring host_game function
int client_game(int oppofd, std::string opponame); // Declaring client_game function

#endif // GAME_H
