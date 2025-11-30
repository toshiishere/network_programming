import argparse
import socket
from typing import List, Tuple

CHOICES = ("rock", "paper", "scissors")


def send_line(sock: socket.socket, text: str):
    sock.sendall((text + "\n").encode("utf-8"))


def recv_line(sock: socket.socket, timeout: float | None = None) -> str:
    """Receive a newline-terminated line, optionally with timeout."""
    previous_timeout = sock.gettimeout()
    if timeout is not None:
        sock.settimeout(timeout)
    try:
        buf: List[bytes] = []
        while True:
            ch = sock.recv(1)
            if not ch:
                raise ConnectionError("socket closed")
            if ch == b"\n":
                break
            buf.append(ch)
        return b"".join(buf).decode("utf-8")
    finally:
        sock.settimeout(previous_timeout)


def determine_winner(moves: List[str]) -> Tuple[str | None, dict]:
    """
    Return winning move (None for tie) and frequency counts for each choice.
    Rules scale to N players:
      - All same or all three present -> tie
      - Otherwise rock beats scissors, paper beats rock, scissors beats paper.
    """
    counts = {m: moves.count(m) for m in CHOICES}
    distinct = set(moves)
    if len(distinct) == 1 or len(distinct) == 3:
        return None, counts
    if distinct == {"rock", "paper"}:
        return "paper", counts
    if distinct == {"rock", "scissors"}:
        return "rock", counts
    return "scissors", counts


def extract_move(line: str) -> str:
    tokens = line.strip().lower().split()
    for t in tokens:
        if t in CHOICES:
            return t
    return "rock"


def close_clients(clients: List[socket.socket]):
    for c in clients:
        try:
            c.close()
        except Exception:
            pass


def wait_for_disconnects(clients: List[socket.socket], timeout: float = 60.0):
    """
    Block until each client disconnects (or timeout). This lets clients shut down
    cleanly instead of racing the server's close.
    """
    for c in clients:
        try:
            c.settimeout(timeout)
            while True:
                data = c.recv(1024)
                if not data:
                    break
        except socket.timeout:
            # Give up waiting on this client after timeout
            pass
        except Exception:
            pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", required=True, type=int)
    parser.add_argument("--room", required=False, default="-")
    parser.add_argument("--players", required=False, type=int, default=2)
    args = parser.parse_args()

    target_players = max(2, min(int(args.players or 2), 10))

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((args.host, args.port))
        server.listen(target_players)
        print(f"[RPS] listening on {args.host}:{args.port} for {target_players} players (room {args.room})")

        clients: List[socket.socket] = []
        try:
            while len(clients) < target_players:
                conn, addr = server.accept()
                idx = len(clients) + 1
                conn.settimeout(20)
                send_line(conn, f"welcome {idx} {target_players} room={args.room}")
                clients.append(conn)
                print(f"[RPS] player {idx} connected from {addr}")

            start_line = f"start {target_players} choices=" + ",".join(CHOICES)
            for c in clients:
                send_line(c, start_line)

            moves: List[str] = []
            for idx, c in enumerate(clients, start=1):
                send_line(c, "choose")
                try:
                    raw = recv_line(c, timeout=25)
                    move = extract_move(raw)
                except Exception as exc:
                    print(f"[RPS] player {idx} input failed ({exc}), defaulting to rock")
                    move = "rock"
                if move not in CHOICES:
                    move = "rock"
                moves.append(move)
                print(f"[RPS] player {idx} chose {move}")

            winning_move, counts = determine_winner(moves)

            for idx, c in enumerate(clients, start=1):
                your_move = moves[idx - 1]
                if winning_move is None:
                    outcome = "tie"
                elif your_move == winning_move:
                    outcome = "win"
                else:
                    outcome = "lose"
                counts_text = " ".join(f"{m}:{counts[m]}" for m in CHOICES)
                summary = (
                    f"result outcome={outcome} you={your_move} "
                    f"winning={winning_move or 'tie'} counts={counts_text}"
                )
                try:
                    send_line(c, summary)
                except Exception as exc:
                    print(f"[RPS] failed to send result to player {idx}: {exc}")

            if winning_move:
                print(f"[RPS] winning move: {winning_move}")
            else:
                print("[RPS] game ended in tie")
            # Keep the server alive until clients disconnect to avoid abrupt closes.
            wait_for_disconnects(clients)
        finally:
            close_clients(clients)


if __name__ == "__main__":
    main()
