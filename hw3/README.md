# HW3 Game Platform

A simple lobby-driven platform with player, developer, and server components.

## Components
- **Server** (`server/server.py`): Manages users, rooms, games, and launches game servers.
- **Player CLI** (`user/user.py`): Connects to the lobby, browses games/rooms, joins, readies, downloads games, launches the provided game client, and rates games.
- **Developer CLI** (`developers/dev.py`): Manages game uploads/updates, lists games, and admin utilities.

## Prerequisites
- Python 3.11+ recommended
- `pip install -r requirements.txt` for pygame/GUI deps used by game clients.

## Running the server
```bash
python server/server.py
```
- Listens on 127.0.0.1:5555 by default.
- Uses `server/database/` for users/devs/games and uploaded game folders.
- Spawns game servers by executing `server/database/games/<game_id>/server_entry.py` with host/port/room/players.

## Developer workflow
```bash
python developers/dev.py
```
1) Register/login as dev.
2) `List my games` to see your uploads and `All games` to view everything.
3) `Upload / Update game` picks a folder under `developers/games/<game_id>/` and uploads a zip to the server.
   - Upload is blocked if the same game_id belongs to another dev.
4) `Delete game` removes a game you authored.

## Player workflow (CLI)
```bash
python user/user.py
```
1) Register/login as player.
2) Lobby: list games/rooms/online players; create or join a room.
3) Ready: auto-checks version, downloads latest game if needed, waits for all players, then launches the game client.
4) Play: the game client connects to the provided host/port from the lobby.
5) Rate: after the game process ends, you’re prompted to rate the game.

## Bundled games
- `rps` (Rock Paper Scissors) — 2-10 players.
- `connect4` — 2 players.
- `battleship` — 2 players, CLI client.
- `chinese_checkers` — 3 players, pygame client.

## Game packaging expectations
Each game lives under `developers/games/<id>/` with:
- `server_entry.py` (accepts `--host`, `--port`, `--room`, `--players`)
- `client_entry.py` (accepts `--host`, `--port`)
- `game_info.json` (id, name, description, version, min_players, max_players)

When uploaded, the server stores files in `server/database/games/<id>/` and uses `game_info.json` to enforce player counts and versioning.

## Notes
- Server enforces single login per username.
- Player `ready` fails if local game version is outdated; the CLI can auto-download.
- Game IDs are unique per developer; cannot overwrite another developer’s game_id.
- Server auto-spawns game servers on ports starting at 6000.
- if ip port needed to be modified, change ```SERVER_HOST``` and ```SERVER_PORT``` in 3 python files
