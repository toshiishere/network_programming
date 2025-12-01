# server/server.py
import socket
import threading
import json
import os
import base64
import zipfile
import io
import subprocess
import sys
from typing import Dict, Any
import signal

# ---------- paths ----------

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DB_DIR = os.path.join(BASE_DIR, "database")
GAMES_DIR = os.path.join(DB_DIR, "games")
DEVS_JSON = os.path.join(DB_DIR, "devs.json")
PLAYERS_JSON = os.path.join(DB_DIR, "players.json")
GAMES_JSON = os.path.join(DB_DIR, "games.json")

HOST = "140.113.17.11"
PORT = 55455

# ---------- in-memory state ----------

lock = threading.Lock()
rooms: Dict[int, dict] = {}  # room_id -> {id, game_id, host, players, ready, status, port}
# Track who is online to block duplicate logins and push events.
online_users = {
    "player": {},
    "dev": {}
}
shutdown_event = threading.Event()
server_socket: socket.socket | None = None
STATE_JSON = os.path.join(DB_DIR, "state.json")


# ---------- utility ----------

def ensure_dirs():
    os.makedirs(DB_DIR, exist_ok=True)
    os.makedirs(GAMES_DIR, exist_ok=True)
    for path in [DEVS_JSON, PLAYERS_JSON, GAMES_JSON]:
        if not os.path.exists(path):
            with open(path, "w", encoding="utf-8") as f:
                json.dump({}, f)


def load_json(path: str) -> dict:
    if not os.path.exists(path):
        return {}
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def save_json(path: str, data: dict):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)


def send_json(sock: socket.socket, obj: Dict[str, Any]):
    data = json.dumps(obj) + "\n"
    sock.sendall(data.encode("utf-8"))


def recv_json(sock: socket.socket) -> Dict[str, Any]:
    """
    Receive a single JSON line. Tolerate decode errors by replacing invalid
    bytes; raise ConnectionError on close.
    """
    buf_bytes = bytearray()
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            raise ConnectionError("socket closed")
        buf_bytes.extend(chunk)
        if b"\n" in chunk:
            break
    try:
        text = buf_bytes.decode("utf-8", errors="replace")
        line, *_ = text.split("\n", 1)
        return json.loads(line)
    except json.JSONDecodeError as exc:
        raise ConnectionError(f"bad json: {exc}") from exc
    except UnicodeDecodeError as exc:
        raise ConnectionError(f"decode error: {exc}") from exc


def next_room_id() -> int:
    with lock:
        return (max(rooms.keys()) + 1) if rooms else 1


def load_games() -> dict:
    return load_json(GAMES_JSON)


def save_games(g: dict):
    save_json(GAMES_JSON, g)


def load_devs() -> dict:
    return load_json(DEVS_JSON)


def save_devs(d: dict):
    save_json(DEVS_JSON, d)


def load_players() -> dict:
    return load_json(PLAYERS_JSON)


def save_players(p: dict):
    save_json(PLAYERS_JSON, p)


def bump_version(current: str) -> str:
    parts = (current.split(".") + ["0", "0", "0"])[:3]
    try:
        major, minor, patch = (int(p) for p in parts)
    except ValueError:
        return "1.0.0"
    patch += 1
    return f"{major}.{minor}.{patch}"


def write_game_info_file(game_id: str, meta: dict):
    """Write metadata into server/database/games/<game_id>/game_info.json."""
    game_dir = os.path.join(GAMES_DIR, game_id)
    os.makedirs(game_dir, exist_ok=True)
    info = {
        "id": game_id,
        "name": meta.get("name", game_id),
        "description": meta.get("description", ""),
        "max_players": meta.get("max_players", 2),
        "min_players": meta.get("min_players", 2),
        "version": meta.get("version", ""),
    }
    path = os.path.join(game_dir, "game_info.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(info, f, indent=2, ensure_ascii=False)


def migrate_games_metadata():
    """Ensure games have max_players metadata and info files."""
    games = load_games()
    changed = False
    for game_id, meta in games.items():
        if "max_players" not in meta:
            meta["max_players"] = 2
            changed = True
        if "min_players" not in meta:
            meta["min_players"] = 2
            changed = True
        # sanity: min <= max and at least 2
        try:
            meta["max_players"] = int(meta.get("max_players", 2))
        except (TypeError, ValueError):
            meta["max_players"] = 2
        try:
            meta["min_players"] = int(meta.get("min_players", 2))
        except (TypeError, ValueError):
            meta["min_players"] = 2
        if meta["min_players"] < 2:
            meta["min_players"] = 2
        if meta["min_players"] > meta["max_players"]:
            meta["min_players"] = meta["max_players"]
        write_game_info_file(game_id, meta)
    if changed:
        save_games(games)


def save_state_snapshot():
    """Persist snapshot of registered users and games (no runtime rooms/online info)."""
    snapshot = {
        "players": list(load_players().keys()),
        "devs": list(load_devs().keys()),
        "games": list(load_games().values()),
    }
    save_json(STATE_JSON, snapshot)
    print(f"[INFO] state saved to {STATE_JSON}")


def request_shutdown():
    print("[INFO] shutdown requested")
    shutdown_event.set()
    try:
        if server_socket:
            server_socket.close()
    except Exception:
        pass


# ---------- game file handling ----------

def store_uploaded_game(game_id: str, zip_bytes: bytes):
    """Unzip uploaded game into server/database/games/<game_id>/"""
    game_dir = os.path.join(GAMES_DIR, game_id)
    if os.path.exists(game_dir):
        # overwrite / update
        for root, dirs, files in os.walk(game_dir, topdown=False):
            for name in files:
                os.remove(os.path.join(root, name))
            for name in dirs:
                os.rmdir(os.path.join(root, name))
    os.makedirs(game_dir, exist_ok=True)

    with zipfile.ZipFile(io.BytesIO(zip_bytes), "r") as zf:
        zf.extractall(game_dir)


def zip_game_folder(game_id: str) -> bytes:
    """Zip server/database/games/<game_id>/ and return bytes."""
    game_dir = os.path.join(GAMES_DIR, game_id)
    if not os.path.isdir(game_dir):
        raise FileNotFoundError("game folder not found")
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        for root, _, files in os.walk(game_dir):
            for name in files:
                full = os.path.join(root, name)
                rel = os.path.relpath(full, game_dir)
                zf.write(full, arcname=rel)
    return buf.getvalue()


def clean_games_data():
    """Delete all games from database and clear game folders and rooms."""
    games = load_games()
    if games:
        print(f"[INFO] cleaning {len(games)} games from database")
    save_games({})
    # remove game folders
    if os.path.isdir(GAMES_DIR):
        for entry in os.listdir(GAMES_DIR):
            path = os.path.join(GAMES_DIR, entry)
            if os.path.isdir(path):
                for root, dirs, files in os.walk(path, topdown=False):
                    for name in files:
                        os.remove(os.path.join(root, name))
                    for name in dirs:
                        os.rmdir(os.path.join(root, name))
                os.rmdir(path)
    # clear rooms
    with lock:
        rooms.clear()


def start_game_server(game_id: str, room_id: int, player_count: int = 2) -> int:
    """
    Spawn game server process:
      python server/database/games/<game_id>/server_entry.py --host HOST --port <port> --room <room_id>
    Return assigned port.
    """
    base_port = 6000
    used_ports = {r["port"] for r in rooms.values() if r.get("port")}
    port = base_port
    while port in used_ports:
        port += 1

    game_dir = os.path.join(GAMES_DIR, game_id)
    server_entry = os.path.join(game_dir, "server_entry.py")
    if not os.path.exists(server_entry):
        print(f"[WARN] server_entry.py not found for game {game_id}")
        return port

    print(f"[INFO] starting game server {game_id} for room {room_id} on {HOST}:{port} players={player_count}")
    subprocess.Popen([
        sys.executable,
        server_entry,
        "--host", HOST,
        "--port", str(port),
        "--room", str(room_id),
        "--players", str(player_count),
    ])
    return port


# ---------- per-connection handler ----------

class ClientThread(threading.Thread):
    def __init__(self, conn: socket.socket, addr):
        super().__init__(daemon=True)
        self.conn = conn
        self.addr = addr
        self.username = None
        self.role = None  # "player" or "dev"
        self.send_lock = threading.Lock()
        print(f"[INFO] connection from {addr}")

    # --- helpers ---

    def send(self, action: str, data: dict):
        with self.send_lock:
            send_json(self.conn, {"action": action, "data": data})

    def require_login(self, role=None) -> bool:
        if not self.username:
            self.send("error", {"reason": "not_logged_in"})
            return False
        if role and self.role != role:
            self.send("error", {"reason": "wrong_role"})
            return False
        return True

    # --- main loop ---

    def run(self):
        try:
            while True:
                try:
                    msg = recv_json(self.conn)
                except ConnectionError:
                    break
                except Exception as exc:
                    print(f"[WARN] failed to parse message from {self.addr}: {exc}")
                    break
                action = msg.get("action")
                data = msg.get("data", {})

                if action == "register":
                    self.handle_register(data)
                elif action == "login":
                    self.handle_login(data)
                elif action == "list_games":
                    self.handle_list_games()
                elif action == "list_rooms":
                    self.handle_list_rooms()
                elif action == "list_players":
                    self.handle_list_players()
                elif action == "create_room":
                    self.handle_create_room(data)
                elif action == "join_room":
                    self.handle_join_room(data)
                elif action == "leave_room":
                    self.handle_leave_room(data)
                elif action == "get_room":
                    self.handle_get_room(data)
                elif action == "ready":
                    self.handle_ready(data)
                elif action == "download_game":
                    self.handle_download_game(data)
                elif action == "rate_game":
                    self.handle_rate_game(data)
                elif action == "dev_list_games":
                    self.handle_dev_list_games()
                elif action == "dev_upload_game":
                    self.handle_dev_upload_game(data)
                elif action == "dev_delete_game":
                    self.handle_dev_delete_game(data)
                elif action == "admin_clean":
                    self.handle_admin_clean()
                elif action == "admin_shutdown":
                    self.handle_admin_shutdown()
                elif action == "quit":
                    self.send("ok", {"msg": "bye"})
                    break
                else:
                    self.send("error", {"reason": "unknown_action", "action": action})
        finally:
            if self.username and self.role in online_users:
                with lock:
                    online_users[self.role].pop(self.username, None)
            if self.username:
                print(f"[INFO] {self.role} '{self.username}' logged out")
            print(f"[INFO] connection closed {self.addr}")
            self.conn.close()

    # ---------- auth ----------

    def handle_register(self, data: dict):
        username = data.get("username")
        password = data.get("password")
        role = data.get("role")  # "player" or "dev"
        if not username or not password or role not in ("player", "dev"):
            self.send("error", {"reason": "bad_request"})
            return

        if role == "dev":
            db = load_devs()
            if username in db:
                self.send("error", {"reason": "user_exists"})
                return
            db[username] = {"password": password}
            save_devs(db)
        else:
            db = load_players()
            if username in db:
                self.send("error", {"reason": "user_exists"})
                return
            db[username] = {"password": password}
            save_players(db)

        self.send("ok", {"msg": "registered"})

    def handle_login(self, data: dict):
        username = data.get("username")
        password = data.get("password")
        role = data.get("role")
        if not username or not password or role not in ("player", "dev"):
            self.send("error", {"reason": "bad_request"})
            return

        if role == "dev":
            db = load_devs()
        else:
            db = load_players()

        user = db.get(username)
        if not user or user["password"] != password:
            self.send("error", {"reason": "auth_failed"})
            return

        # block duplicate logins
        with lock:
            already_online = username in online_users[role]
            if already_online:
                self.send("error", {"reason": "already_online"})
                return
            online_users[role][username] = self

        self.username = username
        self.role = role
        print(f"[INFO] {role} '{username}' logged in from {self.addr}")
        self.send("ok", {"role": role})

    # ---------- player lobby ----------

    def handle_list_games(self):
        if not self.require_login():
            return
        games = load_games()
        self.send("list_games", {"games": list(games.values())})

    def handle_list_rooms(self):
        if not self.require_login():
            return
        with lock:
            rlist = list(rooms.values())
        self.send("list_rooms", {"rooms": rlist})

    def handle_list_players(self):
        if not self.require_login():
            return
        with lock:
            online = list(online_users["player"].keys())
        plist = [{"username": u} for u in online]
        self.send("list_players", {"players": plist})

    def handle_create_room(self, data: dict):
        if not self.require_login(role="player"):
            return
        game_id = data.get("game_id")
        games = load_games()
        game = games.get(game_id)
        if not game:
            self.send("error", {"reason": "game_not_found"})
            return
        try:
            game_max = int(game.get("max_players", 2))
        except (TypeError, ValueError):
            game_max = 2
        try:
            game_min = int(game.get("min_players", 2))
        except (TypeError, ValueError):
            game_min = 2
        if game_max < 2:
            game_max = 2
        if game_min < 2:
            game_min = 2
        if game_min > game_max:
            game_min = game_max
        max_players = game_max
        min_players = game_min
        room_id = next_room_id()
        with lock:
            rooms[room_id] = {
                "id": room_id,
                "game_id": game_id,
                "game_name": game.get("name", game_id),
                "game_description": game.get("description", ""),
                "host": self.username,
                "players": [self.username],
                "ready": {self.username: False},
                "status": "waiting",
                "port": None,
                "max_players": max_players,
                "min_players": min_players,
            }
        self.send("ok", {"room_id": room_id})

    def handle_join_room(self, data: dict):
        if not self.require_login(role="player"):
            return
        room_id = int(data.get("room_id", 0))
        with lock:
            room = rooms.get(room_id)
            if not room:
                self.send("error", {"reason": "room_not_found"})
                return
            if self.username in room["players"]:
                pass
            elif len(room["players"]) >= room.get("max_players", 8):
                self.send("error", {"reason": "room_full"})
                return
            else:
                room["players"].append(self.username)
                room["ready"][self.username] = False
        self.send("ok", {"room_id": room_id})

    def handle_leave_room(self, data: dict):
        if not self.require_login(role="player"):
            return
        room_id = int(data.get("room_id", 0))
        with lock:
            room = rooms.get(room_id)
            if room and self.username in room["players"]:
                room["players"].remove(self.username)
                room["ready"].pop(self.username, None)
                if not room["players"]:
                    rooms.pop(room_id, None)
        self.send("ok", {"room_id": room_id})

    def handle_get_room(self, data: dict):
        if not self.require_login():
            return
        room_id = int(data.get("room_id", 0))
        with lock:
            room = rooms.get(room_id)
        if not room:
            self.send("error", {"reason": "room_not_found"})
        else:
            self.send("get_room", {"room": room})

    def handle_ready(self, data: dict):
        if not self.require_login(role="player"):
            return
        room_id = int(data.get("room_id", 0))
        client_version = data.get("client_version", "")
        with lock:
            room = rooms.get(room_id)
        if not room:
            self.send("error", {"reason": "room_not_found"})
            return

        game_id = room["game_id"]
        games = load_games()
        game = games.get(game_id)
        if not game:
            self.send("error", {"reason": "game_not_found"})
            return

        latest_version = game["version"]
        if client_version != latest_version:
            self.send("ready", {
                "status": "need_update",
                "game_id": game_id,
                "latest_version": latest_version,
                "description": game.get("description", "")
            })
            return

        # mark ready
        with lock:
            room = rooms.get(room_id)
            if not room:
                self.send("error", {"reason": "room_not_found"})
                return
            room["ready"][self.username] = True
            ready_count = sum(1 for v in room["ready"].values() if v)
            print(f"[INFO] {self.username} is ready in room {room_id} ({ready_count}/{len(room['players'])})")
            all_ready = all(room["ready"].get(u, False) for u in room["players"])
            enough_players = len(room["players"]) >= room.get("min_players", 2)
            starting_allowed = all_ready and room["status"] == "waiting" and enough_players
            existing_port = room.get("port")

        # start server outside lock to avoid blocking other threads
        if starting_allowed:
            player_count = len(room.get("players", []))
            port = start_game_server(game_id, room_id, player_count)
            with lock:
                room = rooms.get(room_id)
                if room:
                    room["port"] = port
                    room["status"] = "in_game"
            print(f"[INFO] room {room_id} started game {game_id} on port {port}")
        else:
            port = existing_port

        # respond based on current state
        with lock:
            room = rooms.get(room_id)
            in_game = room and room.get("status") == "in_game" and room.get("port")
            players = room["players"] if room else []

        if in_game:
            payload = {
                "game_id": game_id,
                "room_id": room_id,
                "host": HOST,
                "port": port
            }
            self.send("game_started", payload)
            # notify everyone else who is online
            for username in players:
                if username == self.username:
                    continue
                with lock:
                    peer = online_users["player"].get(username)
                if not peer:
                    continue
                try:
                    peer.send("game_started", payload)
                except Exception as exc:
                    print(f"[WARN] failed to push game start to {username}: {exc}")
        else:
            self.send("ready", {"status": "ready_set"})

    def handle_download_game(self, data: dict):
        if not self.require_login(role="player"):
            return
        game_id = data.get("game_id")
        games = load_games()
        if game_id not in games:
            self.send("download_game", {"status": "error", "reason": "game_not_found"})
            return
        try:
            zip_bytes = zip_game_folder(game_id)
        except FileNotFoundError:
            self.send("download_game", {"status": "error", "reason": "files_missing"})
            return
        b64 = base64.b64encode(zip_bytes).decode("ascii")
        self.send("download_game", {
            "status": "ok",
            "game_id": game_id,
            "version": games[game_id]["version"],
            "zip_b64": b64
        })

    def handle_rate_game(self, data: dict):
        if not self.require_login(role="player"):
            return
        game_id = data.get("game_id")
        rating = int(data.get("rating", 0))
        comment = data.get("comment", "")
        if not (1 <= rating <= 5):
            self.send("error", {"reason": "bad_rating"})
            return
        games = load_games()
        game = games.get(game_id)
        if not game:
            self.send("error", {"reason": "game_not_found"})
            return
        reviews = game.setdefault("reviews", [])
        reviews.append({"user": self.username, "rating": rating, "comment": comment})
        avg = sum(r["rating"] for r in reviews) / len(reviews)
        game["avg_rating"] = avg
        save_games(games)
        self.send("ok", {"msg": "rated"})

    # ---------- dev ops ----------

    def handle_dev_list_games(self):
        if not self.require_login(role="dev"):
            return
        games = load_games()
        my_games = [g for g in games.values() if g.get("author") == self.username]
        all_games = list(games.values())
        self.send("dev_list_games", {"my_games": my_games, "all_games": all_games})

    def handle_dev_upload_game(self, data: dict):
        if not self.require_login(role="dev"):
            return
        game_id = data.get("game_id")
        name = data.get("name")
        description = data.get("description", "")
        version = data.get("version")
        zip_b64 = data.get("zip_b64")

        if not (game_id and zip_b64):
            self.send("error", {"reason": "bad_request"})
            return

        zip_bytes = base64.b64decode(zip_b64)

        # Try to read game_info.json from uploaded zip for metadata defaults
        info = {}
        try:
            with zipfile.ZipFile(io.BytesIO(zip_bytes), "r") as zf:
                if "game_info.json" in zf.namelist():
                    info = json.loads(zf.read("game_info.json").decode("utf-8"))
        except Exception as exc:
            print(f"[WARN] failed to read game_info.json for {game_id}: {exc}")

        games = load_games()
        existing = games.get(game_id)
        if existing and existing.get("author") != self.username:
            self.send("error", {"reason": "game_id_taken", "detail": "game id already used by another developer"})
            return
        # validate required fields if game_info was present
        required_fields = {"id", "name", "description", "version", "min_players", "max_players"}
        if info:
            missing = [f for f in required_fields if f not in info]
            if missing:
                self.send("error", {"reason": "bad_game_info", "detail": f"missing fields: {','.join(missing)}"})
                return
        try:
            max_players = int(info.get("max_players", 2))
        except (TypeError, ValueError):
            max_players = 2
        try:
            min_players = int(info.get("min_players", 2))
        except (TypeError, ValueError):
            min_players = 2
        if max_players < 2:
            max_players = 2
        if min_players < 2:
            min_players = 2
        if min_players > max_players:
            min_players = max_players
        name = info.get("name") or name
        description = info.get("description", description)
        if not name:
            self.send("error", {"reason": "bad_request", "detail": "name missing"})
            return
        latest_version = (existing or {}).get("version", "1.0.0")
        if not version or str(version).lower() == "auto":
            version = bump_version(latest_version)
        # allow explicit version override from info file if provided
        if str(version).lower() == "use_info" and info.get("version"):
            version = str(info.get("version"))

        # create or update
        games[game_id] = {
            "id": game_id,
            "name": name,
            "description": description,
            "version": version,
            "max_players": max_players,
            "min_players": min_players,
            "author": self.username,
            "reviews": games.get(game_id, {}).get("reviews", []),
            "avg_rating": games.get(game_id, {}).get("avg_rating", None),
        }
        save_games(games)

        store_uploaded_game(game_id, zip_bytes)
        write_game_info_file(game_id, games[game_id])
        self.send("ok", {"msg": "game_uploaded_or_updated"})

    def handle_dev_delete_game(self, data: dict):
        if not self.require_login(role="dev"):
            return
        game_id = data.get("game_id")
        if not game_id:
            self.send("error", {"reason": "bad_request"})
            return
        games = load_games()
        game = games.get(game_id)
        if not game:
            self.send("error", {"reason": "game_not_found"})
            return
        if game.get("author") != self.username:
            self.send("error", {"reason": "not_author"})
            return
        # Check if any room is using this game
        with lock:
            active_rooms = [r for r in rooms.values() if r.get("game_id") == game_id]
        if active_rooms:
            self.send("error", {"reason": "game_in_use", "detail": "rooms exist for this game"})
            return

        games.pop(game_id)
        save_games(games)

        game_dir = os.path.join(GAMES_DIR, game_id)
        if os.path.isdir(game_dir):
            for root, dirs, files in os.walk(game_dir, topdown=False):
                for name in files:
                    os.remove(os.path.join(root, name))
                for name in dirs:
                    os.rmdir(os.path.join(root, name))
            os.rmdir(game_dir)

        self.send("ok", {"msg": "game_deleted"})

    # ---------- admin ops ----------

    def handle_admin_clean(self):
        if not self.require_login(role="dev"):
            return
        clean_games_data()
        self.send("ok", {"msg": "games_cleared"})

    def handle_admin_shutdown(self):
        if not self.require_login(role="dev"):
            return
        self.send("ok", {"msg": "server_shutting_down"})
        request_shutdown()


# ---------- server main ----------

def main():
    ensure_dirs()
    migrate_games_metadata()
    print(f"[INFO] server listening on {HOST}:{PORT}")
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        global server_socket
        server_socket = s
        s.bind((HOST, PORT))
        s.listen()

        def handle_signal(signum, frame):
            print(f"[INFO] received signal {signum}, shutting down...")
            shutdown_event.set()
            save_state_snapshot()
            try:
                s.close()
            except Exception:
                pass

        signal.signal(signal.SIGINT, handle_signal)
        signal.signal(signal.SIGTERM, handle_signal)

        while not shutdown_event.is_set():
            try:
                conn, addr = s.accept()
            except OSError:
                break
            ClientThread(conn, addr).start()

    save_state_snapshot()


if __name__ == "__main__":
    main()
