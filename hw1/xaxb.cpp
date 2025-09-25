#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdlib>
#include <ctime>
#include <algorithm>

using namespace std;

// Count bulls (A) and cows (B)
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

int main() {
    srand(time(nullptr));

    // 1. Load words from file
    ifstream fin("game_lib.txt");
    if (!fin) {
        cerr << "Failed to open game_lib.txt\n";
        return 1;
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
        return 1;
    }

    // 2. Pick a random word
    string secret = words[rand() % words.size()];
    int wordLen = secret.size();

    cout << "Welcome to the XaXb game!\n";
    cout << "I picked a word of length " << wordLen << ". Try to guess it!\n";

    // 3. Gameplay loop
    while (true) {
        string guess;
        cout << "Your guess: ";
        cin >> guess;

        if (guess.size() != wordLen) {
            cout << "Guess must be length " << wordLen << "!\n";
            continue;
        }

        auto [a, b] = computeScore(secret, guess);
        cout << a << "a" << b << "b\n";

        if (a == wordLen) {
            cout << "Congratulations! You guessed the word: " << secret << "\n";
            break;
        }
    }

    return 0;
}
