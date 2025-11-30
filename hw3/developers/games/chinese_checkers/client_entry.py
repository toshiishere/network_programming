import argparse
import math
import socket
import sys
import pygame
from typing import Dict, List, Tuple

DIRS = [(1, 0), (1, -1), (0, -1), (-1, 0), (-1, 1), (0, 1)]
RADIUS = 2

WIDTH, HEIGHT = 640, 540
HEX_SIZE = 40

COLORS = {
    1: (220, 70, 70),
    2: (70, 170, 240),
    3: (80, 200, 120),
    "bg": (20, 22, 35),
    "board": (40, 45, 70),
    "highlight": (250, 220, 120),
}


def send_line(sock: socket.socket, text: str):
    sock.sendall((text + "\n").encode("utf-8"))


def recv_line(sock: socket.socket) -> str:
    buf = []
    while True:
        ch = sock.recv(1)
        if not ch:
            raise ConnectionError("socket closed")
        if ch == b"\n":
            break
        buf.append(ch)
    return b"".join(buf).decode("utf-8")


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


def axial_to_pixel(q: int, r: int) -> Tuple[int, int]:
    x = HEX_SIZE * (math.sqrt(3) * q + math.sqrt(3) / 2 * r)
    y = HEX_SIZE * (3 / 2 * r)
    return int(x), int(y)


def draw_hex(surface, center, color):
    cx, cy = center
    points = []
    for i in range(6):
        angle = math.pi / 3 * i + math.pi / 6
        x = cx + HEX_SIZE * 0.95 * math.cos(angle)
        y = cy + HEX_SIZE * 0.95 * math.sin(angle)
        points.append((x, y))
    pygame.draw.polygon(surface, color, points)
    pygame.draw.polygon(surface, (230, 230, 230), points, 2)


def center_board(cells: List[Tuple[int, int]]):
    pts = [axial_to_pixel(q, r) for q, r in cells]
    minx = min(x for x, _ in pts)
    maxx = max(x for x, _ in pts)
    miny = min(y for _, y in pts)
    maxy = max(y for _, y in pts)
    offx = WIDTH // 2 - (minx + maxx) // 2
    offy = HEIGHT // 2 - (miny + maxy) // 2
    return offx, offy


def parse_state(line: str):
    # state turn=1 p1=q,r;... p2=... p3=...
    parts = line.strip().split()
    state = {"turn": None, "p": {1: [], 2: [], 3: []}}
    for p in parts[1:]:
        if p.startswith("turn="):
            try:
                state["turn"] = int(p.split("=", 1)[1])
            except ValueError:
                state["turn"] = None
        elif p.startswith("p1="):
            coords = p.split("=", 1)[1]
            state["p"][1] = parse_coords(coords)
        elif p.startswith("p2="):
            coords = p.split("=", 1)[1]
            state["p"][2] = parse_coords(coords)
        elif p.startswith("p3="):
            coords = p.split("=", 1)[1]
            state["p"][3] = parse_coords(coords)
    return state


def parse_coords(txt: str) -> List[Tuple[int, int]]:
    if not txt:
        return []
    coords = []
    for part in txt.split(";"):
        if not part:
            continue
        try:
            q, r = part.split(",")
            coords.append((int(q), int(r)))
        except Exception:
            continue
    return coords


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", required=True, type=int)
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((args.host, args.port))

    pygame.init()
    screen = pygame.display.set_mode((WIDTH, HEIGHT))
    pygame.display.set_caption("Chinese Checkers (3-player)")
    font = pygame.font.SysFont(None, 28)
    small = pygame.font.SysFont(None, 20)

    offset = center_board(CELLS)

    my_id = None
    state = {"turn": None, "p": {1: [], 2: [], 3: []}}
    selection = None
    legal_moves: List[Tuple[int, int]] = []
    message = "Connecting..."
    winner = None

    def to_screen(cell):
        x, y = axial_to_pixel(cell[0], cell[1])
        return x + offset[0], y + offset[1]

    def recompute_legal():
        nonlocal legal_moves
        if selection is None or my_id is None:
            legal_moves = []
            return
        occ = {}
        for pid, cells in state["p"].items():
            for c in cells:
                occ[c] = pid
        legal = []
        for dest in neighbors(selection):
            if dest not in occ:
                legal.append(dest)
        for dest in jump_targets(selection, occ):
            legal.append(dest)
        legal_moves = legal

    try:
        sock.setblocking(False)
    except Exception:
        pass

    clock = pygame.time.Clock()

    def handle_server():
        nonlocal my_id, state, message, winner
        try:
            line = recv_line(sock)
        except BlockingIOError:
            return
        except Exception:
            message = "Disconnected"
            return
        if not line:
            return
        if line.startswith("welcome"):
            parts = line.split()
            if len(parts) >= 2:
                try:
                    my_id = int(parts[1])
                except ValueError:
                    my_id = None
            message = f"You are player {my_id}"
        elif line.startswith("start"):
            message = "Game start"
        elif line.startswith("state"):
            state = parse_state(line)
            if state.get("turn") == my_id:
                message = "Your turn: select a piece then destination"
            else:
                message = f"Waiting for player {state.get('turn')}"
            recompute_legal()
        elif line.startswith("your_turn"):
            message = "Your turn: select piece"
        elif line.startswith("invalid"):
            message = f"Invalid: {line}"
        elif line.startswith("game_over"):
            parts = line.split()
            for p in parts:
                if p.startswith("winner="):
                    try:
                        winner = int(p.split("=", 1)[1])
                    except ValueError:
                        winner = None
            if winner == my_id:
                message = "You win!"
            else:
                message = f"Player {winner} wins" if winner else "Game over"
        else:
            message = line

    running = True
    while running:
        clock.tick(30)
        # Poll server messages (non-blocking)
        try:
            handle_server()
        except Exception:
            pass

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.MOUSEBUTTONDOWN and state.get("turn") == my_id and winner is None:
                mx, my = event.pos
                clicked_cell = None
                for c in CELLS:
                    sx, sy = to_screen(c)
                    if (mx - sx) ** 2 + (my - sy) ** 2 <= (HEX_SIZE * 0.8) ** 2:
                        clicked_cell = c
                        break
                if clicked_cell is None:
                    continue
                occ = {}
                for pid, cells in state["p"].items():
                    for cc in cells:
                        occ[cc] = pid
                if selection is None:
                    if occ.get(clicked_cell) == my_id:
                        selection = clicked_cell
                        recompute_legal()
                else:
                    if clicked_cell == selection:
                        selection = None
                        recompute_legal()
                        continue
                    if clicked_cell in legal_moves:
                        q1, r1 = selection
                        q2, r2 = clicked_cell
                        send_line(sock, f"move {q1} {r1} {q2} {r2}")
                        selection = None
                        legal_moves = []
                    elif occ.get(clicked_cell) == my_id:
                        selection = clicked_cell
                        recompute_legal()

        screen.fill(COLORS["bg"])
        # draw board cells
        for c in CELLS:
            draw_hex(screen, to_screen(c), COLORS["board"])

        # highlights
        if selection:
            pygame.draw.circle(screen, COLORS["highlight"], to_screen(selection), int(HEX_SIZE * 0.45), 3)
        for m in legal_moves:
            pygame.draw.circle(screen, COLORS["highlight"], to_screen(m), int(HEX_SIZE * 0.3), 0)

        # pieces
        for pid, cells in state["p"].items():
            color = COLORS.get(pid, (200, 200, 200))
            for c in cells:
                pygame.draw.circle(screen, color, to_screen(c), int(HEX_SIZE * 0.35))
                pygame.draw.circle(screen, (0, 0, 0), to_screen(c), int(HEX_SIZE * 0.35), 2)

        status = font.render(message, True, (230, 230, 230))
        screen.blit(status, (20, HEIGHT - 40))
        turn_txt = f"Turn: {state.get('turn')} (You {my_id})"
        screen.blit(small.render(turn_txt, True, (180, 180, 180)), (20, 10))

        pygame.display.flip()

    try:
        sock.close()
    except Exception:
        pass
    pygame.quit()
    sys.exit()


if __name__ == "__main__":
    main()
