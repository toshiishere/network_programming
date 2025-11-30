import argparse
import socket
from typing import Dict, List, Tuple

# Hex axial directions
DIRS = [(1, 0), (1, -1), (0, -1), (-1, 0), (-1, 1), (0, 1)]
RADIUS = 2  # small board radius
PLAYERS = 3  # fixed


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


def axial_add(a: Tuple[int, int], b: Tuple[int, int]) -> Tuple[int, int]:
    return a[0] + b[0], a[1] + b[1]


def axial_scale(a: Tuple[int, int], k: int) -> Tuple[int, int]:
    return a[0] * k, a[1] * k


def axial_distance(a: Tuple[int, int], b: Tuple[int, int]) -> int:
    return int((abs(a[0] - b[0]) + abs(a[0] + a[1] - b[0] - b[1]) + abs(a[1] - b[1])) / 2)


def board_cells() -> List[Tuple[int, int]]:
    cells = []
    for q in range(-RADIUS, RADIUS + 1):
        for r in range(-RADIUS, RADIUS + 1):
            if axial_distance((0, 0), (q, r)) <= RADIUS:
                cells.append((q, r))
    return cells


CELLS = board_cells()


def corner_cells(dir_idx: int) -> List[Tuple[int, int]]:
    # three cells near the corner in given direction
    d = DIRS[dir_idx]
    prev_d = DIRS[(dir_idx - 1) % 6]
    next_d = DIRS[(dir_idx + 1) % 6]
    base = axial_scale(d, RADIUS)
    cells = [base, axial_add(base, prev_d), axial_add(base, next_d)]
    return [c for c in cells if c in CELLS]


STARTS = [corner_cells(0), corner_cells(2), corner_cells(4)]
GOALS = [corner_cells(3), corner_cells(5), corner_cells(1)]  # opposite corners


def initial_positions() -> Dict[Tuple[int, int], int]:
    occ: Dict[Tuple[int, int], int] = {}
    for idx, cells in enumerate(STARTS, start=1):
        for c in cells:
            occ[c] = idx
    return occ


def neighbors(cell: Tuple[int, int]) -> List[Tuple[int, int]]:
    q, r = cell
    res = []
    for dq, dr in DIRS:
        cand = (q + dq, r + dr)
        if cand in CELLS:
            res.append(cand)
    return res


def jump_targets(cell: Tuple[int, int], occ: Dict[Tuple[int, int], int]) -> List[Tuple[int, int]]:
    q, r = cell
    res = []
    for dq, dr in DIRS:
        mid = (q + dq, r + dr)
        dest = (q + 2 * dq, r + 2 * dr)
        if mid in occ and dest in CELLS and dest not in occ:
            res.append(dest)
    return res


def positions_by_player(occ: Dict[Tuple[int, int], int]) -> Dict[int, List[Tuple[int, int]]]:
    res = {1: [], 2: [], 3: []}
    for pos, owner in occ.items():
        res.setdefault(owner, []).append(pos)
    for k in res:
        res[k].sort()
    return res


def format_state(turn_player: int, occ: Dict[Tuple[int, int], int]) -> str:
    parts = [f"state turn={turn_player}"]
    grouped = positions_by_player(occ)
    for pid, cells in grouped.items():
        cell_txt = ";".join(f"{q},{r}" for q, r in cells)
        parts.append(f"p{pid}={cell_txt}")
    return " ".join(parts)


def parse_move(line: str):
    tokens = line.strip().split()
    if len(tokens) != 5 or tokens[0].lower() != "move":
        return None
    try:
        q1, r1, q2, r2 = map(int, tokens[1:])
    except ValueError:
        return None
    return (q1, r1), (q2, r2)


def check_win(occ: Dict[Tuple[int, int], int], pid: int) -> bool:
    goal = set(GOALS[pid - 1])
    return goal and all(occ.get(c) == pid for c in goal)


def wait_for_disconnects(clients: List[socket.socket], timeout: float = 30.0):
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
    parser.add_argument("--players", required=False, type=int, default=3)
    args = parser.parse_args()

    target_players = PLAYERS  # fixed at 3

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((args.host, args.port))
        server.listen(target_players)
        print(f"[CCH] listening on {args.host}:{args.port} room={args.room}")

        clients: List[socket.socket] = []
        try:
            while len(clients) < target_players:
                conn, addr = server.accept()
                idx = len(clients) + 1
                conn.settimeout(40)
                send_line(conn, f"welcome {idx} {target_players} room={args.room}")
                clients.append(conn)
                print(f"[CCH] player {idx} connected from {addr}")

            occ = initial_positions()
            start_info = "start board=hex radius=2 players=3 corners=p1(0) p2(2) p3(4)"
            for c in clients:
                send_line(c, start_info)
                send_line(c, format_state(1, occ))

            current = 1  # player id (1-based)
            while True:
                broadcast_msg = f"turn {current}"
                for c in clients:
                    send_line(c, broadcast_msg)
                cur_sock = clients[current - 1]
                send_line(cur_sock, "your_turn")
                try:
                    line = recv_line(cur_sock, timeout=60)
                except Exception as exc:
                    print(f"[CCH] player {current} timeout/disconnect: {exc}")
                    break
                parsed = parse_move(line)
                if not parsed:
                    send_line(cur_sock, "invalid format move q1 r1 q2 r2")
                    continue
                src, dst = parsed
                if src not in occ or occ[src] != current:
                    send_line(cur_sock, "invalid not_your_piece")
                    continue
                if dst in occ or dst not in CELLS:
                    send_line(cur_sock, "invalid bad_dest")
                    continue

                legal = False
                if dst in neighbors(src):
                    legal = True
                elif dst in jump_targets(src, occ):
                    legal = True
                if not legal:
                    send_line(cur_sock, "invalid not_adjacent_or_jump")
                    continue

                occ.pop(src)
                occ[dst] = current

                if check_win(occ, current):
                    state_line = format_state(current, occ)
                    for c in clients:
                        send_line(c, state_line)
                        send_line(c, f"game_over winner={current}")
                    break

                # next player
                current = 1 + (current % target_players)
                state_line = format_state(current, occ)
                for c in clients:
                    send_line(c, state_line)
        finally:
            wait_for_disconnects(clients)
            for c in clients:
                try:
                    c.close()
                except Exception:
                    pass


if __name__ == "__main__":
    main()
