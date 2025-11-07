# Tetris Multiplayer Game - Complete Communication Protocol

This document describes all communication protocols and JSON formats used in the Tetris multiplayer game system.

---

## Table of Contents

1. [System Architecture](#1-system-architecture)
2. [Game Server ↔ Data Server Communication](#2-game-server--data-server-communication)
3. [Client ↔ Game Server Communication](#3-client--game-server-communication)
4. [Game State JSON Format](#4-game-state-json-format)
5. [Data Model Schemas](#5-data-model-schemas)
6. [Example Flows](#6-example-flows)

---

## 1. System Architecture

```
┌──────────┐         ┌──────────────┐         ┌──────────────┐
│  Client  │ ◄─────► │ Game Server  │ ◄─────► │ Data Server  │
└──────────┘         └──────────────┘         └──────────────┘
     │                      │
     │                      │
     └──────────────────────┘
     (Separate game socket for Tetris gameplay)
```

### Communication Channels

1. **Client ↔ Game Server (Port 45632)**: User authentication, lobby operations, room management
2. **Client ↔ Game Server (Port 50000+room_id)**: Real-time Tetris gameplay
3. **Game Server ↔ Data Server (Port 45631)**: Persistent data storage and retrieval

### Message Format

All messages use **length-prefixed JSON**:
- **4-byte header**: Network byte order (big-endian) unsigned integer indicating payload length
- **JSON payload**: UTF-8 encoded string

**Example (Python):**
```python
import struct, json

# Send
data = json.dumps({"action": "login", "name": "john"}).encode("utf-8")
sock.sendall(struct.pack("!I", len(data)) + data)

# Receive
length = struct.unpack("!I", sock.recv(4))[0]
payload = sock.recv(length).decode("utf-8")
message = json.loads(payload)
```

---

## 2. Game Server ↔ Data Server Communication

The game server communicates with the data server to manage persistent data.

### Request Format

```json
{
  "action": "create | query | update | delete | search",
  "type": "user | room | gamelog",
  "data": { ... }  // or "name"/"id" for queries
}
```

### Actions

#### Create
Creates a new entity in the database.

**Request:**
```json
{
  "action": "create",
  "type": "user",
  "data": {
    "id": -1,
    "name": "alice",
    "password": "secret123",
    "last_login": "2025-01-07 12:34:56",
    "status": "idle",
    "roomName": "-1"
  }
}
```

**Response:**
```json
{
  "response": "success",
  "id": 42
}
```
or
```json
{
  "response": "failed",
  "reason": "user already exists"
}
```

#### Query
Retrieves a specific entity by ID or name.

**Request:**
```json
{
  "action": "query",
  "type": "user",
  "name": "alice"
}
```
or
```json
{
  "action": "query",
  "type": "user",
  "id": 42
}
```

**Response:**
```json
{
  "response": "success",
  "data": {
    "id": 42,
    "name": "alice",
    "password": "secret123",
    "last_login": "2025-01-07 12:34:56",
    "status": "idle",
    "roomName": "-1"
  }
}
```

#### Search
Lists all entities of a type.

**Request:**
```json
{
  "action": "search",
  "type": "room"
}
```

**Response:**
```json
{
  "response": "success",
  "data": [
    {
      "id": 1,
      "name": "Room A",
      "hostUser": "alice",
      "oppoUser": "",
      "visibility": "public",
      "inviteList": [],
      "status": "idle",
      "difficulty": 10
    },
    {
      "id": 2,
      "name": "Room B",
      "hostUser": "bob",
      "oppoUser": "charlie",
      "visibility": "private",
      "inviteList": [42],
      "status": "playing",
      "difficulty": 5
    }
  ]
}
```

#### Update
Updates an existing entity.

**Request:**
```json
{
  "action": "update",
  "type": "user",
  "data": {
    "id": 42,
    "name": "alice",
    "password": "secret123",
    "last_login": "2025-01-07 13:00:00",
    "status": "playing",
    "roomName": "Room A"
  }
}
```

**Response:**
```json
{
  "response": "success"
}
```

#### Delete
Deletes an entity.

**Request:**
```json
{
  "action": "delete",
  "type": "room",
  "data": "Room A"
}
```

**Response:**
```json
{
  "response": "success"
}
```

---

## 3. Client ↔ Game Server Communication

### Authentication Phase

#### Login / Register

**Request:**
```json
{
  "action": "login",
  "name": "alice",
  "password": "secret123"
}
```
or
```json
{
  "action": "register",
  "name": "alice",
  "password": "secret123"
}
```

**Response:**
```json
{
  "response": "success"
}
```
or
```json
{
  "response": "failed",
  "reason": "wrong password | user already exists | user does not exist | already online"
}
```

---

### Lobby Phase

#### Create Room

**Request:**
```json
{
  "action": "create",
  "roomname": "My Room",
  "visibility": "public",
  "difficulty": 10
}
```

**Attributes:**
- `visibility`: `"public"` or `"private"`
- `difficulty`: Integer from 2 (hardest) to 10 (easiest)
  - Controls auto-drop interval (frames between drops)
  - Default: 10

**Response:**
```json
{
  "response": "success"
}
```
or
```json
{
  "response": "failed",
  "reason": "duplicate room"
}
```

#### Join Room

**Request:**
```json
{
  "action": "join",
  "roomname": "My Room"
}
```

**Response:**
```json
{
  "response": "success"
}
```
or
```json
{
  "response": "failed",
  "reason": "no such room | busy"
}
```

#### List Public Rooms

**Request:**
```json
{
  "action": "curroom"
}
```

**Response:**
```json
{
  "response": "success",
  "data": [
    {
      "id": 1,
      "name": "Room A",
      "hostUser": "alice",
      "oppoUser": "",
      "visibility": "public",
      "status": "idle",
      "difficulty": 10
    }
  ]
}
```

#### List Invitations

**Request:**
```json
{
  "action": "curinvite"
}
```

**Response:**
```json
{
  "response": "success",
  "data": [
    {
      "id": 2,
      "name": "Private Room",
      "hostUser": "bob",
      "oppoUser": "",
      "visibility": "private",
      "inviteList": [42],
      "status": "idle",
      "difficulty": 7
    }
  ]
}
```

---

### Room Phase

#### Invite Player

**Request:**
```json
{
  "action": "invite",
  "name": "charlie"
}
```

**Response:**
```json
{
  "response": "success"
}
```
or
```json
{
  "response": "failed",
  "reason": "no such user | not host | not in a room"
}
```

#### Start Game

**Request:**
```json
{
  "action": "start"
}
```

**Response (Success):**
```json
{
  "response": "success"
}
```

Followed by (sent to both players):
```json
{
  "action": "start",
  "data": {
    "id": 1,
    "name": "My Room",
    "hostUser": "alice",
    "oppoUser": "bob",
    "difficulty": 10
  }
}
```

**Response (Failure - Missing Players):**
```json
{
  "response": "failed",
  "reason": "need both host and opponent to start (missing opponent)"
}
```

**Note:** Players connect to game port: `50000 + room_id`

---

### Game Phase

#### Player Actions

Once connected to the game server (port 50000+room_id), players send action commands:

**Request:**
```json
{
  "action": "Left | Right | SoftDrop | HardDrop | RotateCW | RotateCCW | Hold"
}
```

No response - actions are processed immediately.

#### Real-Time Game State Updates

The server sends frame updates every 100ms (10 ticks/second):

**Format (Player 1):**
```json
{
  "f": 1234,
  "p1": {
    "b": [0,0,0,0,0,0,0,0,0,0, ...],
    "h": 0,
    "s": 1200,
    "l": 8,
    "v": 1,
    "g": false
  },
  "p2": {
    "b": [0,0,0,0,0,0,0,0,0,0, ...],
    "h": 3,
    "s": 950,
    "l": 6,
    "v": 1,
    "g": false
  }
}
```

**Note:** Each player sees their own game as `"p1"` and opponent as `"p2"`.

#### Game Over Notification

When the game ends, the server sends:

**Format (Player 1):**
```json
{
  "action": "game_over",
  "won": true,
  "my_result": {
    "score": 15400,
    "lines": 42,
    "maxCombo": 8
  },
  "opponent_result": {
    "score": 12200,
    "lines": 35,
    "maxCombo": 6
  }
}
```

**Format (Player 2):**
```json
{
  "action": "game_over",
  "won": false,
  "my_result": {
    "score": 12200,
    "lines": 35,
    "maxCombo": 6
  },
  "opponent_result": {
    "score": 15400,
    "lines": 42,
    "maxCombo": 8
  }
}
```

---

## 4. Game State JSON Format

### Real-Time Game State: `to_json()`

Sent every game tick during gameplay.

```json
{
  "b": [0,0,0,...],
  "h": 0,
  "s": 1200,
  "l": 8,
  "v": 1,
  "g": false
}
```

#### Field Descriptions

| Field | Type | Description |
|-------|------|-------------|
| `"b"` | Array[200] | Board state (10×20 grid, row-major order) |
| `"h"` | Integer | Hold piece (0-7, 0=Empty) |
| `"s"` | Integer | Current score |
| `"l"` | Integer | Lines cleared |
| `"v"` | Integer | Current level (1 + lines/10) |
| `"g"` | Boolean | Game over flag |

#### Board Array (`"b"`)

A flattened 1D array (200 elements) representing the 10×20 board:

**Index Calculation:** `index = y * 10 + x`
- `x` = column (0-9)
- `y` = row (0-19, where 0 is top)

**Cell Values:**
- `0` = Empty
- `1` = I piece (cyan)
- `2` = O piece (yellow)
- `3` = T piece (purple)
- `4` = S piece (green)
- `5` = Z piece (red)
- `6` = J piece (blue)
- `7` = L piece (orange)
- `8` = Ghost piece (drop preview)
- `9` = Active piece (currently falling)

**Example:**
```
Row 0:  [0,0,0,0,0,0,0,0,0,0]  // indices 0-9
Row 1:  [0,0,0,9,9,9,0,0,0,0]  // indices 10-19 (active T piece)
Row 2:  [0,0,0,0,9,0,0,0,0,0]  // indices 20-29
...
Row 19: [1,1,3,3,3,2,2,0,0,0]  // indices 190-199 (locked pieces)
```

#### Scoring System

- **Line clears:**
  - 1 line: 100 × level
  - 2 lines: 300 × level
  - 3 lines: 500 × level
  - 4 lines (Tetris): 800 × level
- **Drop bonuses:**
  - Soft drop: +1 per cell
  - Hard drop: +2 per cell

### Game Result: `result_json()`

Sent to data server for logging when game ends.

```json
{
  "score": 15400,
  "lines": 42,
  "maxCombo": 8
}
```

---

## 5. Data Model Schemas

### User

```json
{
  "id": 42,
  "name": "alice",
  "password": "secret123",
  "last_login": "2025-01-07 12:34:56",
  "status": "idle | playing | offline",
  "roomName": "Room A"
}
```

**Notes:**
- `roomName`: `"-1"` when not in a room
- `status`: Updated as user navigates through states

### Room

```json
{
  "id": 1,
  "name": "My Room",
  "hostUser": "alice",
  "oppoUser": "bob",
  "visibility": "public | private",
  "inviteList": [42, 57],
  "status": "idle | playing",
  "difficulty": 10
}
```

**Notes:**
- `oppoUser`: Empty string `""` when no opponent
- `inviteList`: Array of user IDs invited to private room
- `difficulty`: 2 (hardest) to 10 (easiest)
  - Controls auto-drop interval (lower = faster = harder)

### GameLog

```json
{
  "room": {
    "id": 1,
    "name": "My Room",
    "difficulty": 10
  },
  "hostUser": "alice",
  "oppoUser": "bob",
  "host_result": {
    "score": 15400,
    "lines": 42,
    "maxCombo": 8
  },
  "oppo_result": {
    "score": 12200,
    "lines": 35,
    "maxCombo": 6
  }
}
```

---

## 6. Example Flows

### Complete Game Session Flow

```
1. Client → Game Server: {"action": "login", "name": "alice", "password": "secret"}
2. Game Server ← Client: {"response": "success"}

3. Client → Game Server: {"action": "create", "roomname": "Room A", "visibility": "public", "difficulty": 10}
4. Game Server ← Client: {"response": "success"}

5. Client → Game Server: {"action": "invite", "name": "bob"}
6. Game Server ← Client: {"response": "success"}

7. [Bob joins the room from another client]

8. Client → Game Server: {"action": "start"}
9. Game Server ← Client: {"response": "success"}
10. Game Server → Both Clients: {"action": "start", "data": {...room info...}}

11. [Clients connect to port 50000 + room_id]

12. Game Server → Clients (every 100ms): {"f": N, "p1": {...}, "p2": {...}}

13. Client → Game Server: {"action": "HardDrop"}

14. [Game ends when one player's board fills up]

15. Game Server → Player 1: {"action": "game_over", "won": true, ...}
16. Game Server → Player 2: {"action": "game_over", "won": false, ...}

17. [Connection closes, players return to lobby]
```

### Board Parsing Example (Python)

```python
def parse_board(board_array):
    """Convert flat array to 2D board."""
    WIDTH, HEIGHT = 10, 20
    board = []
    for y in range(HEIGHT):
        row = board_array[y * WIDTH : (y + 1) * WIDTH]
        board.append(row)
    return board

# Example usage
data = json.loads(message)
board_1d = data["p1"]["b"]
board_2d = parse_board(board_1d)

# Access cell at position (x=3, y=5)
cell_value = board_1d[5 * 10 + 3]
```

---

## 7. Technical Details

### Performance Optimizations

- **Compact keys**: Single-letter keys (`"b"`, `"h"`, `"s"`, etc.) minimize JSON size
- **Flat board array**: 1D array instead of 2D reduces nesting overhead
- **Length-prefixed messages**: Avoids delimiter scanning, faster parsing

### Frame Rate & Timing

- **Game tick rate**: 10 ticks/second (100ms interval)
- **Auto-drop interval**: Configurable via `difficulty` parameter
  - Formula: `drop_every_N_frames = difficulty`
  - Default: 10 frames = 1 second per drop
  - Minimum: 2 frames = 0.2 seconds per drop

### Connection Management

- **Lobby socket**: Persistent connection on port 45632
- **Game socket**: New connection per game on port 50000+room_id
- **Non-blocking I/O**: Edge-triggered epoll (`EPOLLET`) for efficient event handling
- **Message draining**: All queued messages processed per epoll event to prevent input lag

---

## 8. Related Files

- **Tetris Header:** [tetris.h](tetris.h#L43) - Game engine definitions
- **Tetris Implementation:** [tetris.cpp](tetris.cpp#L244) - JSON export logic
- **Game Server:** [game_server.cpp](game_server.cpp) - Main server logic
- **Data Server:** [data_server.cpp](data_server.cpp) - Database management
- **Client:** [client.py](client.py) - Python client with GUI
- **Utility Functions:** [utility.cpp](utility.cpp) - Message send/receive helpers

---

## 9. Board Coordinate System

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

- **Width:** 10 cells (x: 0-9)
- **Height:** 20 cells (y: 0-19)
- **Origin:** Top-left (0,0)
- **Array index:** `idx = y * 10 + x`

---

**Reference:** https://pai-kuan.notion.site/HW2-24ed3aba0aea80f2b7e4e0570c40bc34
