import argparse
import random
import socket
from typing import List, Tuple, Dict

BOARD_SIZE = 6
SHIPS = [3, 2, 2]  # lengths of ships per player


def send_line(sock: socket.socket, text: str):
    sock.sendall((text + "\n").encode("utf-8"))


def recv_line(sock: socket.socket, timeout: float | None = None) -> str:
    prev = sock.gettimeout()
    if timeout is not None:
        sock.settimeout(timeout)
    try:
        buf = []
        while True:
            ch = sock.recv(1)
            if not ch:
                raise ConnectionError("socket closed")
            if ch == b"\n":
                break
            buf.append(ch)
        return b"".join(buf).decode("utf-8")
    finally:
        sock.settimeout(prev)


def empty_board() -> List[List[str]]:
    return [["." for _ in range(BOARD_SIZE)] for _ in range(BOARD_SIZE)]


def place_ships() -> Tuple[List[List[str]], List[Dict]]:
    board = empty_board()
    ships: List[Dict] = []
    for length in SHIPS:
        placed = False
        attempts = 0
        while not placed and attempts < 100:
            attempts += 1
            horiz = random.choice([True, False])
            if horiz:
                r = random.randrange(BOARD_SIZE)
                c = random.randrange(BOARD_SIZE - length + 1)
                coords = [(r, c + i) for i in range(length)]
            else:
                r = random.randrange(BOARD_SIZE - length + 1)
                c = random.randrange(BOARD_SIZE)
                coords = [(r + i, c) for i in range(length)]
            if any(board[rr][cc] != "." for rr, cc in coords):
                continue
            for rr, cc in coords:
                board[rr][cc] = "S"
            ships.append({"coords": set(coords), "hits": set(), "len": length})
            placed = True
        if not placed:
            raise RuntimeError("failed to place ships")
    return board, ships


def all_sunk(ships: List[Dict]) -> bool:
    return all(s["coords"] == s["hits"] for s in ships)


def process_shot(board: List[List[str]], ships: List[Dict], row: int, col: int) -> Tuple[str, int]:
    # returns (result, remaining_ships)
    if not (0 <= row < BOARD_SIZE and 0 <= col < BOARD_SIZE):
        return "invalid", len(ships)
    cell = board[row][col]
    if cell in ("X", "O"):
        return "repeat", len(ships)
    if cell == "S":
        board[row][col] = "X"
        for ship in ships:
            if (row, col) in ship["coords"]:
                ship["hits"].add((row, col))
                sunk = ship["coords"] == ship["hits"]
                if sunk:
                    result = "sunk"
                else:
                    result = "hit"
                break
    else:
        board[row][col] = "O"
        result = "miss"
    remaining = sum(1 for s in ships if s["coords"] != s["hits"])
    return result, remaining


def broadcast(clients: List[socket.socket], text: str):
    for c in clients:
        try:
            send_line(c, text)
        except Exception:
            pass


def wait_for_disconnects(clients: List[socket.socket], timeout: float = 30.0):
    for c in clients:
        try:
            c.settimeout(timeout)
            while True:
                chunk = c.recv(1024)
                if not chunk:
                    break
        except Exception:
            pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", required=True, type=int)
    parser.add_argument("--room", required=False, default="-")
    parser.add_argument("--players", required=False, type=int, default=2)
    args = parser.parse_args()

    target_players = 2  # battleship is strictly 2-player

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((args.host, args.port))
        server.listen(target_players)
        print(f"[BATTLESHIP] listening on {args.host}:{args.port} room={args.room}")

        clients: List[socket.socket] = []
        try:
            while len(clients) < target_players:
                conn, addr = server.accept()
                idx = len(clients) + 1
                conn.settimeout(25)
                send_line(conn, f"welcome {idx} 2 room={args.room}")
                clients.append(conn)
                print(f"[BATTLESHIP] player {idx} connected from {addr}")

            # setup boards
            boards: List[List[List[str]]] = []
            fleets: List[List[Dict]] = []
            for _ in range(target_players):
                b, s = place_ships()
                boards.append(b)
                fleets.append(s)

            start_line = f"start size={BOARD_SIZE} ships=" + ",".join(str(x) for x in SHIPS)
            broadcast(clients, start_line)

            current = 0  # index 0 or 1
            while True:
                broadcast(clients, f"turn {current + 1}")
                shooter = clients[current]
                defender_idx = 1 - current
                defender_board = boards[defender_idx]
                defender_fleet = fleets[defender_idx]

                send_line(shooter, "your_turn")
                try:
                    line = recv_line(shooter, timeout=30)
                except Exception as exc:
                    print(f"[BATTLESHIP] player {current+1} disconnected/timeout: {exc}")
                    break
                parts = line.strip().split()
                if len(parts) != 3 or parts[0].lower() != "fire":
                    send_line(shooter, "invalid bad_command")
                    continue
                try:
                    row = int(parts[1])
                    col = int(parts[2])
                except ValueError:
                    send_line(shooter, "invalid bad_coords")
                    continue

                result, remaining = process_shot(defender_board, defender_fleet, row, col)
                if result == "invalid":
                    send_line(shooter, "invalid out_of_bounds")
                    continue
                if result == "repeat":
                    send_line(shooter, "invalid repeat")
                    continue

                broadcast(clients, f"shot player={current + 1} row={row} col={col} result={result} remaining={remaining}")

                if remaining == 0:
                    broadcast(clients, f"game_over winner={current + 1} message=Player_{current + 1}_wins")
                    break

                current = defender_idx

            wait_for_disconnects(clients)
        finally:
            for c in clients:
                try:
                    c.close()
                except Exception:
                    pass


if __name__ == "__main__":
    main()
