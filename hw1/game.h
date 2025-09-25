#include <string>
#ifndef GAME_H   // Guards against multiple inclusions
#define GAME_H

extern string username,opponent_username;
// Function declarations (prototypes)
int host_game(int oppofd, string opponame);  // Declaring host_game function
int client_game(int oppofd, string opponame); // Declaring client_game function

#endif // GAME_H
