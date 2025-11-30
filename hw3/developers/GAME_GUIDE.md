## Building a lobby-compatible game

Games live under `developers/games/<game_id>/` and must include:
- `server_entry.py`: game server started by the lobby.
- `client_entry.py`: game client launched on each player's machine.
- `game_info.json`: metadata (id, name, description, version, min_players, max_players).

### Server contract
- The lobby starts your server with `python server_entry.py --host <HOST> --port <PORT> --room <ROOM_ID> --players <COUNT>`.
- Listen on the provided host/port and accept exactly `--players` connections (enforce min/max yourself if needed).
- Use simple, newline-terminated text or JSON. Every message should end with `\n`.
- Start the match once all players are connected; avoid selecting a new port.
- Exit the process when the match ends so the lobby can return users to rating.

### Client contract
- The lobby launches `python client_entry.py --host <HOST> --port <PORT>`; read only these flags.
- Connect to the given server, handle disconnects gracefully, and terminate the process when the match finishes.
- Keep dependencies light (pygame is available); avoid extra installs or long-lived background threads on exit.

### Metadata and versions
- `min_players`/`max_players` in `game_info.json` drive lobby validation before starting a room.
- `version` is compared during the ready check; bump it when you upload updates so players auto-download the latest build.
- Include a clear `description` so players know what changed.

### Example flow (used by RPS)
1) Server accepts all players and sends a welcome line identifying their index and total.
2) Server broadcasts a `start ...` line, then prompts each client (e.g., `choose`).
3) Clients send their action, server computes the outcome, and replies with a single `result ...` line.
This pattern keeps the lobby happy and keeps the game lifecycle short and predictable.
