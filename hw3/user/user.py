import socket
import json
import os
import base64
import zipfile
import io
import subprocess
import sys
import time
from typing import Optional

SERVER_HOST = "140.113.17.11"
SERVER_PORT = 5555

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
GAMES_DIR = os.path.join(BASE_DIR, "games")


def send_json(sock, obj):
    data = json.dumps(obj) + "\n"
    sock.sendall(data.encode("utf-8"))


def recv_json(sock):
    buf = []
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            raise ConnectionError("socket closed")
        text = chunk.decode("utf-8")
        buf.append(text)
        if "\n" in text:
            break
    data = "".join(buf)
    line, *_ = data.split("\n", 1)
    return json.loads(line)


class UserClient:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.connect_error = None
        try:
            self.sock.connect((SERVER_HOST, SERVER_PORT))
        except Exception as exc:
            self.connect_error = exc
            self.sock = None
        self.username = None
        self.current_room_id = None

    def send(self, action, data=None, expect=None):
        """
        Send an action and optionally wait for a specific action in response.
        If expect is provided (iterable of action strings), loop until a matching
        response is found or a max number of attempts is reached, ignoring
        unexpected async messages (e.g., game_started push).
        """
        if not self.sock:
            return {"action": "error", "reason": "connection_failed", "detail": str(self.connect_error)}
        try:
            send_json(self.sock, {"action": action, "data": data or {}})
            attempts = 0
            while True:
                resp = recv_json(self.sock)
                attempts += 1
                if expect and resp.get("action") not in expect:
                    print(f"[info] received async message: {resp}")
                    if attempts > 10:
                        return resp
                    continue
                return resp
        except Exception as exc:
            return {"action": "error", "reason": "connection_failed", "detail": str(exc)}

    # ---- auth ----

    def register(self, username, password):
        return self.send("register", {
            "username": username,
            "password": password,
            "role": "player"
        }, expect={"ok", "error"})

    def login(self, username, password):
        resp = self.send("login", {
            "username": username,
            "password": password,
            "role": "player"
        }, expect={"ok", "error"})
        if resp.get("action") != "error":
            self.username = username
        return resp

    # ---- lobby ----

    def list_games(self):
        return self.send("list_games", expect={"list_games", "error"})

    def list_rooms(self):
        return self.send("list_rooms", expect={"list_rooms", "error"})

    def list_players(self):
        return self.send("list_players", expect={"list_players", "error"})

    def create_room(self, game_id, max_players=2):
        resp = self.send("create_room", {
            "game_id": game_id,
            "max_players": max_players
        }, expect={"ok", "error"})
        if resp.get("action") != "error":
            self.current_room_id = resp["data"]["room_id"]
        return resp

    def join_room(self, room_id):
        resp = self.send("join_room", {"room_id": room_id}, expect={"ok", "error"})
        if resp.get("action") != "error":
            self.current_room_id = room_id
        return resp

    def leave_room(self):
        if self.current_room_id is None:
            return
        self.send("leave_room", {"room_id": self.current_room_id}, expect={"ok", "error"})
        self.current_room_id = None

    def get_room(self, room_id):
        return self.send("get_room", {"room_id": room_id}, expect={"get_room", "error"})

    # ---- game version + download ----

    def get_local_version(self, game_id: str) -> str:
        path = os.path.join(GAMES_DIR, game_id, "version.txt")
        if not os.path.exists(path):
            return ""
        with open(path, "r", encoding="utf-8") as f:
            return f.read().strip()

    def set_local_version(self, game_id: str, version: str):
        folder = os.path.join(GAMES_DIR, game_id)
        os.makedirs(folder, exist_ok=True)
        path = os.path.join(folder, "version.txt")
        with open(path, "w", encoding="utf-8") as f:
            f.write(version)

    def download_game(self, game_id: str):
        resp = self.send("download_game", {"game_id": game_id}, expect={"download_game", "error"})
        if resp.get("action") != "download_game":
            return resp
        data = resp["data"]
        if data["status"] != "ok":
            return resp
        zip_b64 = data["zip_b64"]
        version = data["version"]
        zip_bytes = base64.b64decode(zip_b64)
        folder = os.path.join(GAMES_DIR, game_id)
        if os.path.isdir(folder):
            for root, dirs, files in os.walk(folder, topdown=False):
                for name in files:
                    os.remove(os.path.join(root, name))
                for name in dirs:
                    os.rmdir(os.path.join(root, name))
        os.makedirs(folder, exist_ok=True)
        with zipfile.ZipFile(io.BytesIO(zip_bytes), "r") as zf:
            zf.extractall(folder)
        self.set_local_version(game_id, version)
        return resp

    def ready(self, game_id: str):
        local_ver = self.get_local_version(game_id)
        return self.send("ready", {
            "room_id": self.current_room_id,
            "client_version": local_ver
        }, expect={"ready", "game_started", "error"})

    def rate_game(self, game_id: str, rating: int, comment: str):
        return self.send("rate_game", {
            "game_id": game_id,
            "rating": rating,
            "comment": comment
        }, expect={"ok", "error"})

    def close(self):
        try:
            self.send("quit")
        except Exception:
            pass
        try:
            self.sock.close()
        except Exception:
            pass


# ---------- CLI helpers ----------

def prompt_int(prompt: str, allow_empty: bool = False) -> Optional[int]:
    while True:
        val = input(prompt).strip()
        if allow_empty and val == "":
            return None
        try:
            return int(val)
        except ValueError:
            print("Please enter a number.")


def prompt_yes_no(prompt: str, default: bool = False) -> bool:
    suffix = "[Y/n]" if default else "[y/N]"
    ans = input(f"{prompt} {suffix} ").strip().lower()
    if ans == "" and default:
        return True
    if ans == "y":
        return True
    return False


class CLIApp:
    def __init__(self):
        os.makedirs(GAMES_DIR, exist_ok=True)
        self.client = UserClient()
        self.game_process: Optional[subprocess.Popen] = None
        self.current_game_id: Optional[str] = None

    def run(self):
        if not self.client.sock:
            print(f"Cannot connect to server: {self.client.connect_error}")
            return
        try:
            self.main_menu()
        finally:
            self.cleanup_game_process()
            self.client.close()

    # ---------- Menus ----------

    def main_menu(self):
        while True:
            print("\n=== Game Platform ===")
            print("1) Login")
            print("2) Register")
            print("3) Quit")
            choice = input("> ").strip()
            if choice == "1":
                self.handle_login()
            elif choice == "2":
                self.handle_register()
            elif choice == "3":
                print("Goodbye.")
                return
            else:
                print("Invalid choice.")

    def lobby_menu(self):
        while True:
            print("\n=== Lobby ===")
            print("1) List games")
            print("2) List rooms")
            print("3) List online players")
            print("4) Create room")
            print("5) Join room")
            print("6) Logout")
            choice = input("> ").strip()
            if choice == "1":
                self.show_games()
            elif choice == "2":
                self.show_rooms()
            elif choice == "3":
                self.show_players()
            elif choice == "4":
                self.create_room()
            elif choice == "5":
                self.join_room()
            elif choice == "6":
                self.client.send("quit", expect={"ok", "error"})
                self.client = UserClient()
                print("Logged out.")
                return
            else:
                print("Invalid choice.")

    def room_menu(self):
        while True:
            if self.client.current_room_id is None:
                print("Not in a room. Returning to lobby.")
                return
            print("\n=== Room ===")
            print("1) Show room info")
            print("2) Ready (auto update game)")
            print("3) Leave room")
            print("4) Back to lobby")
            choice = input("> ").strip()
            if choice == "1":
                self.show_room_info()
            elif choice == "2":
                self.ready_and_start()
            elif choice == "3":
                self.client.leave_room()
                print("Left room.")
            elif choice == "4":
                self.client.leave_room()
                return
            else:
                print("Invalid choice.")

    # ---------- Actions ----------

    def handle_login(self):
        u = input("Username: ").strip()
        p = input("Password: ").strip()
        resp = self.client.login(u, p)
        if resp.get("action") == "error":
            print("Login failed:", resp)
        else:
            print("Logged in as", u)
            self.lobby_menu()

    def handle_register(self):
        u = input("New username: ").strip()
        p = input("Password: ").strip()
        resp = self.client.register(u, p)
        print(resp)

    def show_games(self):
        resp = self.client.list_games()
        if resp.get("action") != "list_games":
            print("Failed to fetch games:", resp)
            return
        games = resp.get("data", {}).get("games", [])
        if not games:
            print("No games available.")
            return
        print("\nGames:")
        for g in games:
            avg = g.get("avg_rating")
            avg_txt = f"{avg:.2f}" if avg is not None else "N/A"
            print(f"- {g.get('id')} v{g.get('version')} (min {g.get('min_players')} / max {g.get('max_players')}), rating {avg_txt}\n  {g.get('description', '')}")

    def show_rooms(self):
        resp = self.client.list_rooms()
        if resp.get("action") != "list_rooms":
            print("Failed to fetch rooms:", resp)
            return
        rooms = resp.get("data", {}).get("rooms", [])
        if not rooms:
            print("No rooms.")
            return
        print("\nRooms:")
        for r in rooms:
            players = ",".join(r.get("players", []))
            print(f"- {r['id']} | game={r.get('game_id')} status={r.get('status')} players=[{players}] port={r.get('port')}")

    def show_players(self):
        resp = self.client.list_players()
        if resp.get("action") != "list_players":
            print("Failed to fetch players:", resp)
            return
        players = resp.get("data", {}).get("players", [])
        if not players:
            print("No players online.")
            return
        print("Online players:")
        for p in players:
            print("-", p.get("username"))

    def create_room(self):
        # List available games with numbers
        resp_list = self.client.list_games()
        games = resp_list.get("data", {}).get("games", []) if resp_list.get("action") == "list_games" else []
        if not games:
            print("No games available or failed to fetch games:", resp_list)
            return
        print("Select a game to create a room:")
        for idx, g in enumerate(games, start=1):
            print(f"{idx}) {g.get('id')} - {g.get('name')} (v{g.get('version')})")
        choice = prompt_int("Number: ")
        if choice is None or choice < 1 or choice > len(games):
            print("Invalid selection.")
            return
        gid = games[choice - 1].get("id")
        resp = self.client.create_room(gid)
        if resp.get("action") == "error":
            print("Create room failed:", resp)
            return
        print("Room created with id", resp["data"]["room_id"])
        self.room_menu()

    def join_room(self):
        rid = prompt_int("Room ID: ")
        if rid is None:
            return
        resp = self.client.join_room(rid)
        if resp.get("action") == "error":
            print("Join failed:", resp)
            return
        room_info = self.client.get_room(rid)
        if room_info.get("action") == "get_room":
            room = room_info.get("data", {}).get("room", {})
            players = room.get("players", [])
            maxp = room.get("max_players", "N/A")
            game_id = room.get("game_id", "?")
            print(f"Joined room {rid} | game={game_id} players={len(players)}/{maxp}")
        else:
            print("Joined room", rid)
        self.room_menu()

    def show_room_info(self):
        rid = self.client.current_room_id
        if not rid:
            print("Not in a room.")
            return
        resp = self.client.get_room(rid)
        if resp.get("action") != "get_room":
            print("Room fetch failed:", resp)
            return
        room = resp.get("data", {}).get("room")
        if not room:
            print("Room missing.")
            return
        print(f"Room {room['id']} | game={room['game_id']} status={room['status']} port={room.get('port')}")
        print(f"Host: {room['host']}")
        print("Players:")
        for p in room.get("players", []):
            ready = room.get("ready", {}).get(p, False)
            print(f" - {p}: {'READY' if ready else 'NOT READY'}")

        if room.get("status") == "in_game" and room.get("port"):
            print(f"Game running on {SERVER_HOST}:{room['port']}")

    def ready_and_start(self):
        rid = self.client.current_room_id
        if not rid:
            print("Not in a room.")
            return
        room_resp = self.client.get_room(rid)
        room = room_resp.get("data", {}).get("room") if room_resp.get("action") == "get_room" else None
        if not room:
            print("Room not found.")
            return
        game_id = room["game_id"]
        self.current_game_id = game_id

        result = self.client.ready(game_id)
        if result.get("action") == "ready":
            data = result.get("data", {})
            if data.get("status") == "need_update":
                if prompt_yes_no(f"Need to download latest version of {game_id}. Download now?", default=True):
                    dresp = self.client.download_game(game_id)
                    if dresp.get("data", {}).get("status") != "ok":
                        print("Download failed:", dresp)
                        return
                    result = self.client.ready(game_id)
                else:
                    return

        if result.get("action") == "game_started":
            self.handle_game_start(result["data"], game_id)
        elif result.get("action") == "ready":
            print("Ready. Waiting for others... (input blocked until game starts)")
            self.poll_until_game_starts(rid, game_id)
        else:
            print("Ready failed:", result)

    def poll_until_game_starts(self, room_id: int, game_id: str):
        while True:
            time.sleep(1)
            resp = self.client.get_room(room_id)
            if resp.get("action") == "game_started":
                self.handle_game_start(resp["data"], game_id)
                return
            if resp.get("action") != "get_room":
                print("Room fetch failed:", resp)
                return
            room = resp.get("data", {}).get("room")
            if not room:
                print("Room disappeared.")
                return
            if room.get("status") == "in_game" and room.get("port"):
                payload = {
                    "game_id": game_id,
                    "host": SERVER_HOST,
                    "port": room["port"],
                    "room_id": room_id,
                }
                self.handle_game_start(payload, game_id)
                return

    def handle_game_start(self, data: dict, game_id: str):
        host = data.get("host", SERVER_HOST)
        port = data.get("port")
        if port is None:
            print("No port provided for game start.")
            return
        print(f"Starting game {game_id} at {host}:{port} ...")
        self.launch_game_client(game_id, host, port)
        self.wait_for_game_end()
        self.client.leave_room()
        self.prompt_rating(game_id)

    def launch_game_client(self, game_id: str, host: str, port: int):
        game_folder = os.path.join(GAMES_DIR, game_id)
        client_entry = os.path.join(game_folder, "client_entry.py")
        if not os.path.exists(client_entry):
            print(f"client_entry.py not found for game '{game_id}'")
            return
        try:
            self.game_process = subprocess.Popen([
                sys.executable,
                client_entry,
                "--host", host,
                "--port", str(port)
            ])
            print("Game launched. Close the game window to return.")
        except Exception as exc:
            print(f"Failed to launch game: {exc}")
            self.game_process = None

    def wait_for_game_end(self):
        if not self.game_process:
            return
        try:
            self.game_process.wait()
        except KeyboardInterrupt:
            print("Game interrupted. Sending terminate...")
            try:
                self.game_process.terminate()
            except Exception:
                pass
            self.game_process.wait()
        finally:
            self.game_process = None

    def prompt_rating(self, game_id: str):
        print("\nPlease rate the game you just played.")
        rating = prompt_int("Rating (1-5, empty to skip): ", allow_empty=True)
        if rating is None:
            return
        comment = input("Comment (optional): ").strip()
        resp = self.client.rate_game(game_id, rating, comment)
        if resp.get("action") == "ok":
            print("Thanks for rating!")
        else:
            print("Rating failed:", resp)

    def cleanup_game_process(self):
        if self.game_process and self.game_process.poll() is None:
            try:
                self.game_process.terminate()
            except Exception:
                pass
            try:
                self.game_process.wait(timeout=5)
            except Exception:
                pass
            self.game_process = None


def main():
    app = CLIApp()
    app.run()


if __name__ == "__main__":
    main()
