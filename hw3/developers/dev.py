# developers/dev.py
import socket
import json
import os
import base64
import zipfile
import io

SERVER_HOST = "127.0.0.1"
SERVER_PORT = 5555

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
GAMES_DIR = os.path.join(BASE_DIR, "games")


# -------------------- JSON Helpers --------------------

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
    line = "".join(buf).split("\n", 1)[0]
    return json.loads(line)


# -------------------- Folder Zipping --------------------

def zip_game_folder(game_id: str) -> bytes:
    """
    Zip the folder developers/games/<game_id> and return raw bytes.
    """
    folder = os.path.join(GAMES_DIR, game_id)
    if not os.path.isdir(folder):
        raise FileNotFoundError(f"Game folder '{folder}' not found")

    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        for root, dirs, files in os.walk(folder):
            for name in files:
                full = os.path.join(root, name)
                rel = os.path.relpath(full, folder)
                zf.write(full, arcname=rel)
    return buf.getvalue()


# -------------------- Developer Client --------------------

class DevClient:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((SERVER_HOST, SERVER_PORT))
        self.username = None
        self.logged_in = False

    def send(self, action: str, data=None):
        send_json(self.sock, {"action": action, "data": data or {}})
        return recv_json(self.sock)

    # ------------ Authentication ------------

    def register(self):
        print("\n=== Register Developer ===")
        username = input("New developer username: ").strip()
        password = input("Password: ").strip()

        resp = self.send("register", {
            "username": username,
            "password": password,
            "role": "dev"
        })
        print(resp)

    def login(self):
        print("\n=== Developer Login ===")
        username = input("Developer username: ").strip()
        password = input("Password: ").strip()

        resp = self.send("login", {
            "username": username,
            "password": password,
            "role": "dev"
        })
        print(resp)

        if resp.get("action") != "error":
            self.username = username
            self.logged_in = True
            print(f"\nLogged in as developer '{username}'\n")
        else:
            print("\nLogin failed.\n")

    # ------------- Developer Operations -------------

    def ensure_login(self) -> bool:
        """
        Prevent dev operations until logged in.
        """
        if not self.logged_in:
            print("\n❌ You must login first.\n")
            return False
        return True

    def list_my_games(self):
        if not self.ensure_login():
            return
        resp = self.send("dev_list_games")
        print("\n=== Your Games ===")
        for g in resp["data"]["games"]:
            print(f"- {g['id']} [{g['version']}] {g['name']}: {g['description']}")
        print()

    def upload_game(self):
        if not self.ensure_login():
            return

        print("\n=== Upload Game ===")
        print("(Your local game folders under developers/games/):")
        for entry in os.listdir(GAMES_DIR):
            print(" -", entry)
        print()

        game_id = input("Game folder name (game_id): ").strip()
        name = input("Game display name: ").strip()
        description = input("Description: ").strip()
        version = input("Version (blank to auto bump): ").strip()
        try:
            max_players = int(input("Max players (default 2): ").strip() or 2)
        except ValueError:
            max_players = 2

        # Zip folder
        try:
            zip_bytes = zip_game_folder(game_id)
        except FileNotFoundError as e:
            print("Error:", e)
            return

        zip_b64 = base64.b64encode(zip_bytes).decode("ascii")

        resp = self.send("dev_upload_game", {
            "game_id": game_id,
            "name": name,
            "description": description,
            "version": version,
            "max_players": max_players,
            "zip_b64": zip_b64
        })

        print("\nServer Response:", resp, "\n")

    def delete_game(self):
        if not self.ensure_login():
            return

        game_id = input("Game ID to delete: ").strip()
        resp = self.send("dev_delete_game", {"game_id": game_id})
        print(resp)

    # ------------- Main Loop -------------

    def run(self):
        print("=== Developer Client ===")

        while True:
            if not self.logged_in:
                print("""
1) Register
2) Login
3) Quit
""")
                choice = input("> ").strip()

                if choice == "1":
                    self.register()
                elif choice == "2":
                    self.login()
                elif choice == "3":
                    self.send("quit")
                    print("Goodbye.")
                    break
                else:
                    print("Invalid option.\n")

            else:
                print(f"=== Developer Menu (Logged in as {self.username}) ===")
                print("""
1) List my games
2) Upload / Update game
3) Delete game
4) Logout
5) Quit
""")
                choice = input("> ").strip()

                if choice == "1":
                    self.list_my_games()
                elif choice == "2":
                    self.upload_game()
                elif choice == "3":
                    self.delete_game()
                elif choice == "4":
                    print(f"Logged out from {self.username}.\n")
                    self.logged_in = False
                    self.username = None
                elif choice == "5":
                    self.send("quit")
                    print("Goodbye.")
                    break
                else:
                    print("Invalid option.\n")

        self.sock.close()


if __name__ == "__main__":
    os.makedirs(GAMES_DIR, exist_ok=True)
    DevClient().run()
