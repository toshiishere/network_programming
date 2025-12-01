import argparse
import socket
import time
from typing import List, Tuple, Dict

WIDTH, HEIGHT = 15, 12
START_LEN = 5
DIRS = {
    "w": (0, -1),
    "a": (-1, 0),
    "s": (0, 1),
    "d": (1, 0),
}


def send_line(sock: socket.socket, text: str):
    try:
        sock.sendall((text + "\n").encode("utf-8"))
    except Exception:
        pass


def recv_line(sock: socket.socket, timeout: float = 0.01) -> str | None:
    prev = sock.gettimeout()
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
    except socket.timeout:
        return None
    finally:
        sock.settimeout(prev)


def wrap(pos: Tuple[int, int]) -> Tuple[int, int]:
    x, y = pos
    return x % WIDTH, y % HEIGHT


def build_snake(start_head: Tuple[int, int], direction: Tuple[int, int]) -> List[Tuple[int, int]]:
    # create snake of length START_LEN going opposite of direction
    dx, dy = direction
    body = []
    for i in range(START_LEN):
        bx = start_head[0] - dx * i
        by = start_head[1] - dy * i
        body.append(wrap((bx, by)))
    return body


def state_line(tick: int, snakes: List[List[Tuple[int, int]]], turn_dirs: List[str]) -> str:
    parts = [f"state tick={tick}"]
    for idx, body in enumerate(snakes, start=1):
        coords = ";".join(f"{x},{y}" for x, y in body)
        parts.append(f"p{idx}={coords}")
    parts.append(f"dir1={turn_dirs[0]}")
    parts.append(f"dir2={turn_dirs[1]}")
    return " ".join(parts)


def wait_for_disconnects(clients: List[socket.socket], timeout: float = 10.0):
    for c in clients:
        try:
            c.settimeout(timeout)
            while True:
                data = c.recv(1024)
                if not data:
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

    target_players = 2

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((args.host, args.port))
        server.listen(target_players)
        print(f"[SNAKE] listening on {args.host}:{args.port} room={args.room}")

        clients: List[socket.socket] = []
        try:
            while len(clients) < target_players:
                conn, addr = server.accept()
                idx = len(clients) + 1
                conn.settimeout(1)
                send_line(conn, f"welcome {idx} {target_players} room={args.room}")
                clients.append(conn)
                print(f"[SNAKE] player {idx} connected from {addr}")

            start_msg = f"start width={WIDTH} height={HEIGHT} len={START_LEN}"
            for c in clients:
                send_line(c, start_msg)

            # initial snakes
            snakes: List[List[Tuple[int, int]]] = []
            dirs = [(1, 0), (-1, 0)]
            heads = [(2, HEIGHT // 2), (WIDTH - 3, HEIGHT // 2)]
            for head, d in zip(heads, dirs):
                snakes.append(build_snake(head, d))
            dir_keys = ["d", "a"]  # initial directions

            tick = 0
            game_over = False
            while not game_over:
                tick += 1
                # read inputs non-blocking
                for i, c in enumerate(clients):
                    try:
                        msg = recv_line(c, timeout=0.01)
                    except ConnectionError:
                        game_over = True
                        winner = 2 if i == 0 else 1
                        for cc in clients:
                            send_line(cc, f"game_over winner={winner} reason=disconnect")
                        break
                    if not msg:
                        continue
                    parts = msg.strip().split()
                    if len(parts) == 2 and parts[0].lower() == "move" and parts[1].lower() in DIRS:
                        dir_keys[i] = parts[1].lower()

                next_positions = []
                for i, body in enumerate(snakes):
                    dx, dy = DIRS[dir_keys[i]]
                    hx, hy = body[0]
                    nx, ny = wrap((hx + dx, hy + dy))
                    next_positions.append((nx, ny))

                # compute collisions using bodies excluding tails
                lose = [False, False]
                bodies_no_tail = [set(b[:-1]) for b in snakes]
                # head-to-head collision
                if next_positions[0] == next_positions[1]:
                    lose = [True, True]
                else:
                    # p1 collisions
                    if next_positions[0] in bodies_no_tail[0] or next_positions[0] in set(snakes[1]):
                        lose[0] = True
                    # p2 collisions
                    if next_positions[1] in bodies_no_tail[1] or next_positions[1] in set(snakes[0]):
                        lose[1] = True

                if all(lose):
                    for c in clients:
                        send_line(c, "game_over winner=tie reason=head_on")
                    break
                if lose[0]:
                    for c in clients:
                        send_line(c, "game_over winner=2 reason=p1_crash")
                    break
                if lose[1]:
                    for c in clients:
                        send_line(c, "game_over winner=1 reason=p2_crash")
                    break

                # apply moves
                for i, body in enumerate(snakes):
                    body.insert(0, next_positions[i])
                    if len(body) > START_LEN:
                        body.pop()

                state = state_line(tick, snakes, dir_keys)
                for c in clients:
                    send_line(c, state)
                time.sleep(0.2)

            wait_for_disconnects(clients)
        finally:
            for c in clients:
                try:
                    c.close()
                except Exception:
                    pass


if __name__ == "__main__":
    main()
