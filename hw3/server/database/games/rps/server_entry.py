# developers/games/rps/server_entry.py
import socket
import argparse

CHOICES = ["rock", "paper", "scissors"]


def outcome(a, b):
    if a == b:
        return "tie"
    if (a == "rock" and b == "scissors") or \
       (a == "scissors" and b == "paper") or \
       (a == "paper" and b == "rock"):
        return "a"
    return "b"


def recv_line(sock):
    buf = []
    while True:
        ch = sock.recv(1)
        if not ch:
            raise ConnectionError("socket closed")
        if ch == b"\n":
            break
        buf.append(ch)
    return b"".join(buf).decode("utf-8")


def send_line(sock, text: str):
    sock.sendall((text + "\n").encode("utf-8"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", required=True, type=int)
    parser.add_argument("--room", required=False)
    args = parser.parse_args()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind((args.host, args.port))
        s.listen()
        print(f"[RPS SERVER] listening on {args.host}:{args.port}")

        clients = []
        while len(clients) < 2:
            conn, addr = s.accept()
            print("[RPS SERVER] client connected", addr)
            clients.append(conn)

        try:
            send_line(clients[0], "your_turn")
            c1 = recv_line(clients[0]).strip()
            if c1 not in CHOICES:
                c1 = "rock"
            print(f"[RPS SERVER] player1 chose {c1}")
            send_line(clients[1], "your_turn")
            c2 = recv_line(clients[1]).strip()
            if c2 not in CHOICES:
                c2 = "rock"
            print(f"[RPS SERVER] player2 chose {c2}")

            r = outcome(c1, c2)
            print(f"[RPS SERVER] outcome: {r} ({c1} vs {c2})")
            if r == "tie":
                send_line(clients[0], f"result tie {c1} vs {c2}")
                send_line(clients[1], f"result tie {c2} vs {c1}")
            elif r == "a":
                send_line(clients[0], f"result win {c1} vs {c2}")
                send_line(clients[1], f"result lose {c2} vs {c1}")
            else:
                send_line(clients[0], f"result lose {c1} vs {c2}")
                send_line(clients[1], f"result win {c2} vs {c1}")
        finally:
            for c in clients:
                c.close()


if __name__ == "__main__":
    main()
