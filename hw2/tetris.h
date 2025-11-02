#pragma once
#include <vector>
#include <array>
#include <random>
#include <cstdint>
#include <string>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

class Tetris {
public:
    static constexpr int kWidth  = 10;
    static constexpr int kHeight = 20;

    enum Piece : uint8_t { Empty=0, I, O, T, S, Z, J, L };

    enum class Action {
        None, Left, Right, SoftDrop, HardDrop, RotateCW, RotateCCW, Hold
    };

    struct Active {
        Piece id = Empty;
        int x = 0;
        int y = 0;
        int rot = 0;
    };

    struct State {
        bool gameOver = false;
        int score = 0;
        int lines = 0;
        int level = 1;
        int combo = -1;
        Active active{};
        Piece hold = Empty;
        bool holdLocked = false;
        std::array<Piece, 6> nextPreview{};
        int ghostY = 0;
    };

    explicit Tetris(uint32_t seed = std::random_device{}());
    void reset();
    bool step(Action a);

    const std::array<uint8_t, kWidth * kHeight>& board() const { return board_; }
    const State& state() const { return st_; }

    std::string debugString() const;

    // --- serialization ---
    json to_json() const;
    void from_json(const json& j);

private:
    std::array<uint8_t, kWidth * kHeight> board_{};
    std::mt19937 rng_;
    std::vector<Piece> bag_;
    State st_{};

    void spawn();
    bool canPlace(const Active& a) const;
    void lockPiece();
    int clearLinesAndScore();
    void newBagIfNeeded();
    Piece popNext();
    void computeGhost();
    bool testKick(Active& a, int rotDir) const;
    void addLockScore(int cleared, int softDropCells, int hardDropCells);

    static bool cell(Piece p, int rot, int dx, int dy);
    static int idx(int x, int y) { return y * kWidth + x; }
};
