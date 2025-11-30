# user/user.py
import socket
import json
import os
import base64
import zipfile
import io
import subprocess
import sys
import tkinter as tk
from tkinter import ttk, messagebox, simpledialog

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

    def send(self, action, data=None):
        if not self.sock:
            return {"action": "error", "reason": "connection_failed", "detail": str(self.connect_error)}
        try:
            send_json(self.sock, {"action": action, "data": data or {}})
            return recv_json(self.sock)
        except Exception as exc:
            return {"action": "error", "reason": "connection_failed", "detail": str(exc)}

    # ---- auth ----

    def register(self, username, password):
        return self.send("register", {
            "username": username,
            "password": password,
            "role": "player"
        })

    def login(self, username, password):
        resp = self.send("login", {
            "username": username,
            "password": password,
            "role": "player"
        })
        if resp.get("action") != "error":
            self.username = username
        return resp

    # ---- lobby ----

    def list_games(self):
        return self.send("list_games")

    def list_rooms(self):
        return self.send("list_rooms")

    def list_players(self):
        return self.send("list_players")

    def create_room(self, game_id, max_players=2):
        resp = self.send("create_room", {
            "game_id": game_id,
            "max_players": max_players
        })
        if resp.get("action") != "error":
            self.current_room_id = resp["data"]["room_id"]
        return resp

    def join_room(self, room_id):
        resp = self.send("join_room", {"room_id": room_id})
        if resp.get("action") != "error":
            self.current_room_id = room_id
        return resp

    def leave_room(self):
        if self.current_room_id is None:
            return
        self.send("leave_room", {"room_id": self.current_room_id})
        self.current_room_id = None

    def get_room(self, room_id):
        return self.send("get_room", {"room_id": room_id})

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
        resp = self.send("download_game", {"game_id": game_id})
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
        """Perform ready with auto-version check."""
        local_ver = self.get_local_version(game_id)
        resp = self.send("ready", {
            "room_id": self.current_room_id,
            "client_version": local_ver
        })
        if resp.get("action") == "ready":
            data = resp["data"]
            if data["status"] == "need_update":
                return resp
            else:
                return resp
        elif resp.get("action") == "game_started":
            return resp
        else:
            return resp

    def rate_game(self, game_id: str, rating: int, comment: str):
        return self.send("rate_game", {
            "game_id": game_id,
            "rating": rating,
            "comment": comment
        })

    def close(self):
        try:
            self.send("quit")
        except Exception:
            pass
        self.sock.close()


# ---------- Tkinter GUI ----------

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("HW3 Game Lobby")
        self.geometry("800x600")
        os.makedirs(GAMES_DIR, exist_ok=True)

        self.client = UserClient()
        self.current_game_id = None
        self.base_title = "HW3 Game Lobby"
        self.is_gaming = False
        self.game_process = None

        topbar = ttk.Frame(self)
        topbar.pack(fill="x")
        ttk.Button(topbar, text="Quit", command=self.quit_app).pack(side="right", padx=5, pady=5)

        self.container = ttk.Frame(self)
        self.container.pack(fill="both", expand=True)

        self.frames = {}
        for F in (LoginFrame, LobbyFrame, RoomFrame, RateFrame):
            frame = F(parent=self.container, app=self)
            self.frames[F.__name__] = frame
            frame.grid(row=0, column=0, sticky="nsew")

        self.show_frame("LoginFrame")

    def show_frame(self, name: str):
        frame = self.frames[name]
        frame.tkraise()
        if hasattr(frame, "on_show"):
            frame.on_show()

    def launch_game_client(self, game_id: str, host: str, port: int):
        """Launch pygame client_entry.py inside user/games/<game_id>/"""
        game_folder = os.path.join(GAMES_DIR, game_id)
        client_entry = os.path.join(game_folder, "client_entry.py")
        if not os.path.exists(client_entry):
            messagebox.showerror("Error", f"client_entry.py not found for game '{game_id}'")
            return
        try:
            self.game_process = subprocess.Popen([
                sys.executable,
                client_entry,
                "--host", host,
                "--port", str(port)
            ])
            self.is_gaming = True
            self.set_title_user(self.client.username, gaming=True)
            self.current_game_id = game_id
            self.after(1000, self._poll_game_process)
        except Exception as exc:
            messagebox.showerror("Error", f"Failed to launch game: {exc}")

    def on_closing(self):
        self.client.close()
        self.destroy()

    def quit_app(self):
        try:
            self.client.close()
        except Exception:
            pass
        self.destroy()

    def set_title_user(self, username, gaming=False):
        title = self.base_title
        if username:
            title = f"{self.base_title} - {username}"
        if gaming:
            title += " [Gaming]"
        self.title(title)

    def _poll_game_process(self):
        if self.game_process and self.game_process.poll() is None:
            # still running
            self.after(1000, self._poll_game_process)
            return
        # finished
        self.is_gaming = False
        self.set_title_user(self.client.username)
        self.game_process = None
        try:
            self.client.leave_room()
        except Exception:
            pass
        if self.current_game_id:
            self.show_frame("RateFrame")


class LoginFrame(ttk.Frame):
    def __init__(self, parent, app: App):
        super().__init__(parent)
        self.app = app

        ttk.Label(self, text="HW3 Game Platform - Login/Register", font=("Arial", 16)).pack(pady=20)

        form = ttk.Frame(self)
        form.pack(pady=10)

        ttk.Label(form, text="Username").grid(row=0, column=0, sticky="e", padx=5, pady=5)
        ttk.Label(form, text="Password").grid(row=1, column=0, sticky="e", padx=5, pady=5)

        self.username_var = tk.StringVar()
        self.password_var = tk.StringVar()

        ttk.Entry(form, textvariable=self.username_var).grid(row=0, column=1, padx=5, pady=5)
        ttk.Entry(form, textvariable=self.password_var, show="*").grid(row=1, column=1, padx=5, pady=5)

        btns = ttk.Frame(self)
        btns.pack(pady=10)
        ttk.Button(btns, text="Register", command=self.do_register).grid(row=0, column=0, padx=5)
        ttk.Button(btns, text="Login", command=self.do_login).grid(row=0, column=1, padx=5)

    def do_register(self):
        u = self.username_var.get().strip()
        p = self.password_var.get().strip()
        if not u or not p:
            messagebox.showwarning("Warning", "Username/password cannot be empty")
            return
        resp = self.app.client.register(u, p)
        messagebox.showinfo("Register", str(resp))

    def do_login(self):
        u = self.username_var.get().strip()
        p = self.password_var.get().strip()
        if not u or not p:
            messagebox.showwarning("Warning", "Username/password cannot be empty")
            return
        resp = self.app.client.login(u, p)
        if resp.get("action") == "error":
            if resp.get("reason") == "connection_failed":
                messagebox.showerror("Login failed", f"Cannot connect to server: {resp.get('detail')}")
            else:
                messagebox.showerror("Login failed", str(resp))
        else:
            messagebox.showinfo("Login", "Logged in as player")
            self.app.set_title_user(u)
            self.app.show_frame("LobbyFrame")


class LobbyFrame(ttk.Frame):
    def __init__(self, parent, app: App):
        super().__init__(parent)
        self.app = app
        self.games_data = {}

        top = ttk.Frame(self)
        top.pack(fill="x", pady=5)
        ttk.Label(top, text="Lobby", font=("Arial", 16)).pack(side="left", padx=10)
        ttk.Button(top, text="Logout", command=self.logout).pack(side="right", padx=10)

        main = ttk.Frame(self)
        main.pack(fill="both", expand=True, padx=10, pady=10)

        # Games list
        games_frame = ttk.LabelFrame(main, text="Games")
        games_frame.grid(row=0, column=0, sticky="nsew", padx=5, pady=5)
        self.games_list = tk.Listbox(games_frame, height=10)
        self.games_list.pack(fill="both", expand=True)
        self.games_list.bind("<Double-Button-1>", self.show_game_info)
        btns_games = ttk.Frame(games_frame)
        btns_games.pack(pady=5)
        ttk.Button(btns_games, text="Refresh Games", command=self.refresh_games).grid(row=0, column=0, padx=2)
        ttk.Button(btns_games, text="View Info", command=self.show_game_info).grid(row=0, column=1, padx=2)

        # Rooms list
        rooms_frame = ttk.LabelFrame(main, text="Rooms")
        rooms_frame.grid(row=0, column=1, sticky="nsew", padx=5, pady=5)
        self.rooms_list = tk.Listbox(rooms_frame, height=10)
        self.rooms_list.pack(fill="both", expand=True)
        btns_room = ttk.Frame(rooms_frame)
        btns_room.pack(pady=5)
        ttk.Button(btns_room, text="Refresh Rooms", command=self.refresh_rooms).grid(row=0, column=0, padx=2)
        ttk.Button(btns_room, text="Create Room", command=self.create_room).grid(row=0, column=1, padx=2)
        ttk.Button(btns_room, text="Join Room", command=self.join_room).grid(row=0, column=2, padx=2)

        # Players list
        players_frame = ttk.LabelFrame(main, text="Players")
        players_frame.grid(row=0, column=2, sticky="nsew", padx=5, pady=5)
        self.players_list = tk.Listbox(players_frame, height=10)
        self.players_list.pack(fill="both", expand=True)
        ttk.Button(players_frame, text="Refresh Players", command=self.refresh_players).pack(pady=5)

        main.columnconfigure(0, weight=1)
        main.columnconfigure(1, weight=1)
        main.columnconfigure(2, weight=1)

    def on_show(self):
        self.refresh_games()
        self.refresh_rooms()
        self.refresh_players()

    def logout(self):
        try:
            self.app.client.send("quit")
        except Exception:
            pass
        try:
            self.app.client.close()
        except Exception:
            pass
        self.app.client = UserClient()
        self.app.client.username = None
        self.app.set_title_user(None)
        self.app.show_frame("LoginFrame")

    def refresh_games(self):
        self.games_list.delete(0, tk.END)
        resp = self.app.client.list_games()
        if resp.get("action") == "error" or "data" not in resp or "games" not in resp.get("data", {}):
            messagebox.showerror("Error", str(resp))
            return
        self.games_data = {g["id"]: g for g in resp["data"]["games"]}
        for g in self.games_data.values():
            line = f"{g['id']} [{g['version']}] {g['name']} (by {g['author']})"
            self.games_list.insert(tk.END, line)

    def refresh_rooms(self):
        self.rooms_list.delete(0, tk.END)
        resp = self.app.client.list_rooms()
        if resp.get("action") != "list_rooms" or "data" not in resp or "rooms" not in resp.get("data", {}):
            messagebox.showerror("Error", f"Room fetch failed: {resp}")
            return
        for r in resp["data"]["rooms"]:
            line = f"{r['id']} - game={r['game_id']} players={len(r['players'])} status={r['status']}"
            self.rooms_list.insert(tk.END, line)

    def refresh_players(self):
        self.players_list.delete(0, tk.END)
        resp = self.app.client.list_players()
        if resp.get("action") != "list_players" or "data" not in resp or "players" not in resp.get("data", {}):
            messagebox.showerror("Error", f"Players fetch failed: {resp}")
            return
        for p in resp["data"]["players"]:
            self.players_list.insert(tk.END, p["username"])

    def get_selected_game_id(self):
        sel = self.games_list.curselection()
        if not sel:
            return None
        text = self.games_list.get(sel[0])
        return text.split()[0]  # first token is game_id

    def get_selected_game(self):
        gid = self.get_selected_game_id()
        if not gid:
            return None, None
        return gid, self.games_data.get(gid)

    def show_game_info(self, event=None):
        gid, g = self.get_selected_game()
        if not gid:
            messagebox.showwarning("Warning", "Select a game first")
            return
        if not g:
            messagebox.showerror("Error", "Game data not available. Refresh games and try again.")
            return
        avg = g.get("avg_rating")
        avg_text = f"{avg:.2f}" if isinstance(avg, (int, float)) else "N/A"
        info_lines = [
            f"Name: {g.get('name', gid)}",
            f"ID: {gid}",
            f"Version: {g.get('version', '')}",
            f"Author: {g.get('author', '')}",
            f"Max players: {g.get('max_players', 'N/A')}",
            f"Avg rating: {avg_text}",
            f"Description: {g.get('description', '')}",
        ]
        messagebox.showinfo("Game Info", "\n".join(info_lines))

    def get_selected_room_id(self):
        sel = self.rooms_list.curselection()
        if not sel:
            return None
        text = self.rooms_list.get(sel[0])
        return int(text.split()[0])

    def create_room(self):
        gid = self.get_selected_game_id()
        if not gid:
            messagebox.showwarning("Warning", "Select a game first")
            return
        resp = self.app.client.create_room(gid)
        if resp.get("action") == "error":
            messagebox.showerror("Error", str(resp))
            return
        messagebox.showinfo("Room", f"Room created: {resp['data']['room_id']}")
        self.app.show_frame("RoomFrame")

    def join_room(self):
        rid = self.get_selected_room_id()
        if rid is None:
            messagebox.showwarning("Warning", "Select a room first")
            return
        resp = self.app.client.join_room(rid)
        if resp.get("action") == "error":
            messagebox.showerror("Error", str(resp))
            return
        messagebox.showinfo("Room", f"Joined room {rid}")
        self.app.show_frame("RoomFrame")


class RoomFrame(ttk.Frame):
    def __init__(self, parent, app: App):
        super().__init__(parent)
        self.app = app
        self.refresh_job = None

        top = ttk.Frame(self)
        top.pack(fill="x", pady=5)
        ttk.Label(top, text="Room", font=("Arial", 16)).pack(side="left", padx=10)
        ttk.Button(top, text="Back to Lobby", command=self.back_lobby).pack(side="right", padx=10)

        self.info_text = tk.Text(self, height=10)
        self.info_text.pack(fill="x", padx=10, pady=5)

        btns = ttk.Frame(self)
        btns.pack(pady=10)
        ttk.Button(btns, text="Refresh Room State", command=self.refresh_room).grid(row=0, column=0, padx=5)
        ttk.Button(btns, text="Ready (auto update game)", command=self.ready).grid(row=0, column=1, padx=5)
        ttk.Button(btns, text="Leave Room", command=self.leave_room).grid(row=0, column=2, padx=5)

    def on_show(self):
        self.start_auto_refresh()

    def back_lobby(self):
        self.app.client.leave_room()
        self.stop_auto_refresh()
        self.app.show_frame("LobbyFrame")
        if self.app.is_gaming:
            # if user leaves early, stop tracking
            self.app.is_gaming = False
            self.app.set_title_user(self.app.client.username)

    def leave_room(self):
        self.app.client.leave_room()
        self.stop_auto_refresh()
        self.app.show_frame("LobbyFrame")

    def refresh_room(self):
        rid = self.app.client.current_room_id
        if not rid:
            self.info_text.delete("1.0", tk.END)
            self.info_text.insert(tk.END, "Not in any room.\n")
            return
        resp = self.app.client.get_room(rid)
        self.info_text.delete("1.0", tk.END)
        if resp.get("action") == "error":
            self.info_text.insert(tk.END, str(resp))
            return
        room = resp["data"]["room"]
        self.info_text.insert(tk.END, f"Room ID: {room['id']}\n")
        self.info_text.insert(tk.END, f"Game ID: {room['game_id']}\n")
        self.info_text.insert(tk.END, f"Host: {room['host']}\n")
        self.info_text.insert(tk.END, f"Status: {room['status']}\n")
        self.info_text.insert(tk.END, f"Port: {room.get('port')}\n")
        self.info_text.insert(tk.END, "Players & Ready:\n")
        for p in room["players"]:
            r = room["ready"].get(p, False)
            self.info_text.insert(tk.END, f"  - {p}: {'READY' if r else 'NOT READY'}\n")

        # auto-launch if in_game and not already gaming
        if room.get("status") == "in_game" and room.get("port") and not self.app.is_gaming:
            host = SERVER_HOST
            port = room["port"]
            messagebox.showinfo("Game", f"Game started on {host}:{port}")
            self.app.launch_game_client(room["game_id"], host, port)

    def ready(self):
        rid = self.app.client.current_room_id
        if not rid:
            messagebox.showwarning("Warning", "Not in a room")
            return
        resp = self.app.client.get_room(rid)
        if resp.get("action") != "get_room":
            messagebox.showerror("Error", str(resp))
            return
        room = resp.get("data", {}).get("room")
        if not room:
            messagebox.showerror("Error", "Room not found")
            return
        game_id = room["game_id"]

        # first try ready: if need_update, download game
        result = self.app.client.ready(game_id)
        if result.get("action") == "ready":
            data = result["data"]
            if data["status"] == "need_update":
                if messagebox.askyesno("Update game",
                                       f"Need to download latest version of {game_id}.\n\n"
                                       f"Description: {data['description']}\n\nDownload now?"):
                    dresp = self.app.client.download_game(game_id)
                    if dresp.get("data", {}).get("status") != "ok":
                        messagebox.showerror("Error", f"Download failed: {dresp}")
                        return
                    # try ready again
                    result = self.app.client.ready(game_id)
                else:
                    return

        if result.get("action") == "game_started":
            data = result["data"]
            host = data["host"]
            port = data["port"]
            messagebox.showinfo("Game", f"Game started on {host}:{port}")
            self.app.launch_game_client(game_id, host, port)
        elif result.get("action") == "ready":
            messagebox.showinfo("Ready", "You are ready. Waiting for others...")
            self.after(1000, lambda: self.poll_room_start(rid, game_id))
        else:
            messagebox.showerror("Error", str(result))

    def poll_room_start(self, room_id, game_id):
        resp = self.app.client.get_room(room_id)
        if resp.get("action") != "get_room":
            return
        room = resp.get("data", {}).get("room")
        if not room:
            return
        if room.get("status") == "in_game" and room.get("port"):
            host = SERVER_HOST
            port = room["port"]
            messagebox.showinfo("Game", f"Game started on {host}:{port}")
            self.app.launch_game_client(game_id, host, port)
        else:
            self.after(1000, lambda: self.poll_room_start(room_id, game_id))

    def start_auto_refresh(self):
        self.stop_auto_refresh()
        self.refresh_room()
        self.refresh_job = self.after(1000, self._auto_refresh)

    def _auto_refresh(self):
        self.refresh_room()
        self.refresh_job = self.after(1000, self._auto_refresh)

    def stop_auto_refresh(self):
        if self.refresh_job is not None:
            try:
                self.after_cancel(self.refresh_job)
            except Exception:
                pass
            self.refresh_job = None


class RateFrame(ttk.Frame):
    def __init__(self, parent, app: App):
        super().__init__(parent)
        self.app = app

        ttk.Label(self, text="Rate Game", font=("Arial", 16)).pack(pady=10)

        self.info_label = ttk.Label(self, text="You can rate the last played game.")
        self.info_label.pack(pady=5)

        form = ttk.Frame(self)
        form.pack(pady=10)

        ttk.Label(form, text="Rating (1-5):").grid(row=0, column=0, padx=5, pady=5)
        ttk.Label(form, text="Comment:").grid(row=1, column=0, padx=5, pady=5, sticky="n")

        self.rating_var = tk.IntVar(value=5)
        ttk.Entry(form, textvariable=self.rating_var).grid(row=0, column=1, padx=5, pady=5)

        self.comment_text = tk.Text(form, width=40, height=5)
        self.comment_text.grid(row=1, column=1, padx=5, pady=5)

        btns = ttk.Frame(self)
        btns.pack(pady=10)
        ttk.Button(btns, text="Submit", command=self.submit).grid(row=0, column=0, padx=5)
        ttk.Button(btns, text="Back to Lobby", command=self.back_lobby).grid(row=0, column=1, padx=5)

    def on_show(self):
        gid = self.app.current_game_id or "(none)"
        self.info_label.config(text=f"Game: {gid}")

    def submit(self):
        gid = self.app.current_game_id
        if not gid:
            messagebox.showwarning("Warning", "No game to rate")
            return
        rating = self.rating_var.get()
        comment = self.comment_text.get("1.0", tk.END).strip()
        resp = self.app.client.rate_game(gid, rating, comment)
        if resp.get("action") == "error":
            messagebox.showerror("Error", str(resp))
        else:
            messagebox.showinfo("Thank you", "Rating submitted")
            self.comment_text.delete("1.0", tk.END)
            self.app.show_frame("LobbyFrame")

    def back_lobby(self):
        self.app.show_frame("LobbyFrame")


if __name__ == "__main__":
    app = App()
    app.protocol("WM_DELETE_WINDOW", app.on_closing)
    app.mainloop()
