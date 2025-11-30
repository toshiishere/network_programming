# developers/games/rps/server_entry.py
import socket
import argparse

CHOICES = ["rock", "paper", "scissors"]


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
    parser.add_argument("--players", required=False, type=int, default=2)
    args = parser.parse_args()

    target_players = max(2, min(args.players, 10))

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind((args.host, args.port))
        s.listen()
        print(f"[RPS SERVER] listening on {args.host}:{args.port} for {target_players} players")

        clients = []
        while len(clients) < target_players:
            conn, addr = s.accept()
            print("[RPS SERVER] client connected", addr)
            clients.append(conn)

        try:
            choices = []
            for idx, c in enumerate(clients):
                send_line(c, "your_turn")
                choice = recv_line(c).strip()
                if choice not in CHOICES:
                    choice = "rock"
                choices.append(choice)
                print(f"[RPS SERVER] player{idx+1} chose {choice}")

            counts = {m: choices.count(m) for m in CHOICES}
            distinct = set(choices)
            if len(distinct) == 1 or len(distinct) == 3:
                winning_move = None
            elif distinct == {"rock", "paper"}:
                winning_move = "paper"
            elif distinct == {"rock", "scissors"}:
                winning_move = "rock"
            else:
                winning_move = "scissors"

            for idx, c in enumerate(clients):
                choice = choices[idx]
                if winning_move is None:
                    outcome = "tie"
                elif choice == winning_move:
                    outcome = "win"
                else:
                    outcome = "lose"
                summary = f"result {outcome} you={choice} winning_move={winning_move or 'tie'} counts=rock:{counts['rock']},paper:{counts['paper']},scissors:{counts['scissors']}"
                send_line(c, summary)
            if winning_move:
                print(f"[RPS SERVER] winning move: {winning_move}")
            else:
                print("[RPS SERVER] tie")
        finally:
            for c in clients:
                c.close()


if __name__ == "__main__":
    main()
