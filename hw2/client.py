#!/usr/bin/python3
import socket, struct, json

# ========= Config =========
SERVER_IP = "127.0.0.1"
SERVER_PORT = 45632


# ========= Length-prefixed message helpers =========
def recv_exact(sock, n):
    """Receive exactly n bytes or return None if disconnected."""
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


def recv_msg(sock):
    """Receive a single length-prefixed JSON message (C++ compatible)."""
    hdr = recv_exact(sock, 4)
    if not hdr:
        return None
    (length,) = struct.unpack("!I", hdr)
    if length <= 0 or length > 10_000_000:
        print("Invalid length from server:", length)
        return None
    payload = recv_exact(sock, length)
    if not payload:
        return None
    return payload.decode("utf-8")


def send_msg(sock, obj):
    """Send a JSON message with a 4-byte length prefix."""
    data = json.dumps(obj, separators=(",", ":")).encode("utf-8")
    sock.sendall(struct.pack("!I", len(data)) + data)


# ========= Login/Register =========
def login_or_register(sock, action: str, name: str, password: str):
    """Send login or register JSON to the game server."""
    req = {
        "action": action,  # "login" or "register"
        "name": name,
        "password": password
    }
    print(f"→ Sending {action} request for user '{name}'...")
    send_msg(sock, req)
    reply = recv_msg(sock)
    if not reply:
        print("⚠️  No reply or disconnected from server.")
        return
    try:
        res = json.loads(reply)
        if(res['response']=='success'):
            return True
        else: 
            print("← Server response:")
            print(json.dumps(res, indent=2))
    except Exception as e:
        print("⚠️  Failed to parse JSON:", e)
        print("Raw reply:", reply)


def lobby_op(sock, action: str, roomname:str=""):
    if(action in ('curinvite', 'curroom')):
        req = {
            "action": action
        }
        send_msg(sock, req)
        reply = recv_msg(sock)
        if not reply:
            print("⚠️  No reply or disconnected from server.")
            return False
        try:
            res = json.loads(reply)
            if(res['response']=='success'):
                for room in res['data']:
                    print(json.dumps(room, indent=2))
                return False #success, but noting changed
            else: 
                print("← Server response:")
                print(json.dumps(res, indent=2))
        except Exception as e:
            print("⚠️  Failed to parse JSON:", e)
            print("Raw reply:", reply)
    elif action in ('create', 'join', 'spec'):
        req = {
            "action": action,
            "roomname" : roomname
        }
        if action == 'create':
            req["visibility"]='public' if input("make it public? y or n ")=='y' else 'private'
        send_msg(sock, req)
        reply = recv_msg(sock)
        if not reply:
            print("⚠️  No reply or disconnected from server.")
            return False
        try:
            res = json.loads(reply)
            if(res['response']=='success'):
                print('success, now join THE room')
                return True #success, move to room
            else: 
                print("← Server response:")
                print(json.dumps(res, indent=2))
        except Exception as e:
            print("⚠️  Failed to parse JSON:", e)
            print("Raw reply:", reply)

def room_op(sock, action:str):
    if(action == "invite"):
        toinvite=input("Enter someone you want to invte: ").strip().lower()
        req = {
            "action": action,
            "name": toinvite
        }
        send_msg(sock, req)
        reply = recv_msg(sock)
        if not reply:
            print("⚠️  No reply or disconnected from server.")
            return -1
        try:
            res = json.loads(reply)
            if(res['response']=='success'):
                print("invited "+toinvite)
                return 0
            else: 
                print("← Server response:")
                print(json.dumps(res, indent=2))
        except Exception as e:
            print("⚠️  Failed to parse JSON:", e)
            print("Raw reply:", reply)
    elif action == "start":
        req = {
            "action": action,
        }
        send_msg(sock, req)
        reply = recv_msg(sock)
        if not reply:
            print("⚠️  No reply or disconnected from server.")
            return -1
        try:
            res = json.loads(reply)
            if(res['response']=='success'):
                print("continue to start the game...")
                return 1
            else: 
                print("← Server response:")
                print(json.dumps(res, indent=2))
        except Exception as e:
            print("⚠️  Failed to parse JSON:", e)
            print("Raw reply:", reply)




# ========= Main =========
def main():
    state = 'login' #login, idle, gaming, room
    print(f"Connecting to {SERVER_IP}:{SERVER_PORT} ...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect((SERVER_IP, SERVER_PORT))
        print("✅ Connected to game server!\n")
    except Exception as e:
        print("❌ Connection failed:", e)
        return

    while True:
        cmd = input("Enter 'login' or 'register' (or 'quit'): ").strip().lower()
        if cmd == "quit":
            break
        if cmd not in ("login", "register"):
            print("Invalid command.\n")
            continue
        name = input("Username: ").strip()
        pw = input("Password: ").strip()
        if(login_or_register(sock, cmd, name, pw)):
            state='idle'
            break

    while True:
        if(state == 'idle'):
            cmd = input("For game room, Enter 'curinvite' or'curroom' or 'create' or 'join' or 'spec' (or 'quit'): ").strip().lower()
            if cmd == 'quit':
                break
            if cmd not in ("curinvite", "curroom", "create", "join", "spec"):
                print("Invalid command.\n")
                continue
            roomname=""
            if cmd in ("create", "join", "spec"):
                roomname = input("Enter room name: ").strip()
            if lobby_op(sock, cmd, roomname):
                state = 'room'

        elif state == 'room':
            cmd = input("Enter 'invite' or 'start' (or 'quit'): ").strip().lower()
            if cmd == 'quit':
                break
            if cmd not in ("invite", "start"):
                print("Invalid command.\n")
                continue
            result=room_op(sock,cmd)
            if result > 0:
                state = 'gaming'
            elif result == 0:
                print("Nothing changed, still in the room")
            else:
                state = 'idle'
                print("this shouldn't happen, bug happened")
                print("exited the room")

        elif(state == 'gaming'):
            #connect to new socket
            #create GUI window for game
            #close everything, go back to lobby
            state = 'idle'



    print("Closing connection...")
    sock.close()
    print("Disconnected.")


if __name__ == "__main__":
    main()
