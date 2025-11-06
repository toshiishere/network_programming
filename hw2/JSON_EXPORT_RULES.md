# Tetris Game JSON Export Rules

## Overview

The game server sends JSON data to clients every game tick (100ms) containing the complete game state. There are two main JSON export methods in the Tetris class:

1. **`to_json()`** - Full game state for real-time gameplay (sent every frame)
2. **`result_json()`** - Final game results (sent to game server for recording when game ends)

---

## 1. Real-Time Game State: `to_json()`

### JSON Structure

```json
{
  "b": [0,0,0,...],  // board array (200 elements: 10 width × 20 height)
  "h": 0,            // hold piece
  "s": 1200,         // score
  "l": 8,            // lines cleared
  "v": 1,            // level
  "g": false         // gameOver flag
}
```

### Field Descriptions

#### `"b"` - Board Array (200 elements)
A flattened 1D array representing the 10×20 game board.

**Index Calculation:** `index = y * 10 + x`
- `x` = column (0-9)
- `y` = row (0-19, where 0 is top)

**Cell Values:**
- `0` = Empty cell
- `1` = I piece (cyan)
- `2` = O piece (yellow)
- `3` = T piece (purple)
- `4` = S piece (green)
- `5` = Z piece (red)
- `6` = J piece (blue)
- `7` = L piece (orange)
- `8` = **Ghost piece** (preview of where active piece will land)
- `9` = **Active piece** (currently falling piece)

**Example Board Layout:**
```
Row 0:  [0,0,0,0,0,0,0,0,0,0]  // indices 0-9
Row 1:  [0,0,0,9,9,9,0,0,0,0]  // indices 10-19 (active piece)
Row 2:  [0,0,0,0,9,0,0,0,0,0]  // indices 20-29
...
Row 19: [1,1,3,3,3,2,2,0,0,0]  // indices 190-199 (bottom, locked pieces)
```

#### `"h"` - Hold Piece
The piece currently held by the player (can be swapped once per piece).
- Values: `0-7` (same as Piece enum)
- `0` = Empty (no held piece)

#### `"s"` - Score
Current player score (integer).
- Scoring rules:
  - 1 line: 100 × level
  - 2 lines: 300 × level
  - 3 lines: 500 × level
  - 4 lines (Tetris): 800 × level
  - Soft drop: +1 per cell
  - Hard drop: +2 per cell

#### `"l"` - Lines Cleared

#### `"v"` - Level
Current difficulty level (integer).  `level = 1 + (lines_cleared / 10)`

#### `"g"` - Game Over
Boolean flag indicating if the game has ended.

---

## 2. Game Result: `result_json()`

### JSON Structure

```json
{
  "score": 15400,
  "lines": 42,
  "maxCombo": 8
}
```

---

## 3. Server Frame Protocol

Each game tick, the server sends a frame message to both players:

### Player 1's View
```json
{
  "f": 1234,         // frame number
  "p1": { ... },     // player 1's own game (to_json())
  "p2": { ... }      // player 2's game (opponent view)
}
```

### Player 2's View
```json
{
  "f": 1234,         // frame number
  "p1": { ... },     // player 2's own game (to_json())
  "p2": { ... }      // player 1's game (opponent view)
}
```

**Key Point:** Each player sees their own game as `"p1"` and the opponent as `"p2"`.

---

## 4. Board Coordinate System

```
     x →
   ┌─────────────────────┐
y  │ (0,0)       (9,0)   │  Top of board
↓  │                     │
   │                     │
   │                     │
   │                     │
   │                     │
   │                     │
   │                     │
   │                     │
   │ (0,19)      (9,19)  │  Bottom of board
   └─────────────────────┘
```

---

## 5. Example Use Cases

### Parsing the Board in Python

```python
import json

def parse_board(board_array):
    """Convert flat array to 2D board."""
    WIDTH, HEIGHT = 10, 20
    board = []
    for y in range(HEIGHT):
        row = board_array[y * WIDTH : (y + 1) * WIDTH]
        board.append(row)
    return board

# Receive from server
data = json.loads(message)
board_1d = data["p1"]["b"]
board_2d = parse_board(board_1d)

# Access cell at (x=3, y=5)
cell_value = board_1d[5 * 10 + 3]
```

---

## 6. Related Files

- **Tetris Header:** [tetris.h](tetris.h#L16) - Piece enum definitions
- **Tetris Implementation:** [tetris.cpp](tetris.cpp#L244) - JSON export logic
- **Game Server:** [game_server.cpp](game_server.cpp#L434) - Frame protocol
- **Utility Functions:** [utility.cpp](utility.cpp) - Message send/receive

---

## 7. Notes

- **Performance:** The board is sent as a flat array (not 2D) to reduce JSON size
- **Compact Keys:** Single-letter keys (`"b"`, `"h"`, `"s"`, etc.) minimize bandwidth
- **Frame Rate:** Server runs at 10 ticks/second (100ms interval)
- **Auto-Drop:** Configurable via `difficulty` parameter (default: 10 frames = 1 second)
- **Message Protocol:** Length-prefixed (4-byte header + JSON body)
