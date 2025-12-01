import argparse
import socket
import sys
import pygame
from typing import List, Tuple, Dict

WIDTH, HEIGHT = 15, 12
CELL = 32
MARGIN = 20

DIRS = {
    pygame.K_w: "w",
    pygame.K_a: "a",
    pygame.K_s: "s",
    pygame.K_d: "d",
}


def send_line(sock: socket.socket, text: str):
    try:
        sock.sendall((text + "\n").encode("utf-8"))
    except Exception:
        pass


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


def parse_state(line: str) -> Dict:
    # state tick=1 p1=x,y;... p2=... dir1=w dir2=a
    parts = line.strip().split()
    data = {"tick": 0, "p1": [], "p2": [], "dir1": "w", "dir2": "a"}
    for p in parts[1:]:
        if p.startswith("tick="):
            try:
                data["tick"] = int(p.split("=", 1)[1])
            except ValueError:
                data["tick"] = 0
        elif p.startswith("p1="):
            coords = p.split("=", 1)[1]
            data["p1"] = parse_coords(coords)
        elif p.startswith("p2="):
            coords = p.split("=", 1)[1]
            data["p2"] = parse_coords(coords)
        elif p.startswith("dir1="):
            data["dir1"] = p.split("=", 1)[1]
        elif p.startswith("dir2="):
            data["dir2"] = p.split("=", 1)[1]
    return data


def parse_coords(txt: str) -> List[Tuple[int, int]]:
    if not txt:
        return []
    res = []
    for part in txt.split(";"):
        if not part:
            continue
        try:
            x, y = part.split(",")
            res.append((int(x), int(y)))
        except Exception:
            continue
    return res


def draw_board(screen, state, my_id):
    screen.fill((15, 18, 30))
    w = WIDTH * CELL
    h = HEIGHT * CELL
    pygame.draw.rect(screen, (30, 35, 60), (MARGIN, MARGIN, w, h))
    for x in range(WIDTH):
        for y in range(HEIGHT):
            rect = (MARGIN + x * CELL, MARGIN + y * CELL, CELL, CELL)
            pygame.draw.rect(screen, (45, 50, 80), rect, 1)

    colors = {1: (220, 80, 80), 2: (80, 170, 230)}
    for idx, key in [(1, "p1"), (2, "p2")]:
        body = state.get(key, [])
        for i, (x, y) in enumerate(body):
            color = colors[idx]
            cx = MARGIN + x * CELL + CELL // 2
            cy = MARGIN + y * CELL + CELL // 2
            rad = CELL // 2 - 3 if i == 0 else CELL // 2 - 6
            pygame.draw.circle(screen, color, (cx, cy), rad)
            pygame.draw.circle(screen, (0, 0, 0), (cx, cy), rad, 2)

    font = pygame.font.SysFont(None, 28)
    info = f"You are player {my_id}. Move with WASD."
    screen.blit(font.render(info, True, (230, 230, 230)), (MARGIN, h + MARGIN + 10))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", required=True, type=int)
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((args.host, args.port))

    pygame.init()
    screen = pygame.display.set_mode((MARGIN * 2 + WIDTH * CELL, MARGIN * 2 + HEIGHT * CELL + 60))
    pygame.display.set_caption("Snake Duel (2P)")
    font = pygame.font.SysFont(None, 28)

    my_id = None
    state = {"p1": [], "p2": [], "tick": 0}
    status = "Connecting..."
    winner = None

    def handle_message(msg: str) -> bool:
        nonlocal state, status, my_id, winner
        if msg.startswith("welcome"):
            parts = msg.split()
            if len(parts) >= 2:
                my_id = parts[1]
            status = f"You are player {my_id}"
        elif msg.startswith("start"):
            status = "Game start"
        elif msg.startswith("state"):
            state = parse_state(msg)
            status = f"Tick {state.get('tick', 0)}"
        elif msg.startswith("game_over"):
            winner = None
            for part in msg.split():
                if part.startswith("winner="):
                    winner = part.split("=", 1)[1]
            if winner == my_id:
                status = "You win!"
            elif winner == "tie":
                status = "Tie"
            else:
                status = f"Player {winner} wins" if winner else "Game over"
            return False
        else:
            status = msg
        return True

    # Block until we receive initial state to avoid empty render/black screen.
    try:
        sock.setblocking(True)
        while True:
            try:
                msg = recv_line(sock)
            except Exception:
                status = "Disconnected"
                break
            if not handle_message(msg):
                break
            if msg.startswith("state"):
                break
    finally:
        try:
            sock.setblocking(False)
        except Exception:
            pass

    running = True
    clock = pygame.time.Clock()
    while running:
        clock.tick(30)
        # listen non-blocking
        try:
            while True:
                msg = recv_line(sock)
                if not handle_message(msg):
                    running = False
                    break
        except BlockingIOError:
            pass
        except Exception:
            running = False
            break

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN and event.key in DIRS and winner is None:
                send_line(sock, f"move {DIRS[event.key]}")

        draw_board(screen, state, my_id)
        status_text = font.render(status, True, (230, 230, 230))
        screen.blit(status_text, (MARGIN, MARGIN + HEIGHT * CELL + 30))
        pygame.display.flip()

        if winner is not None:
            # linger briefly
            pygame.time.delay(1500)
            running = False

    try:
        sock.close()
    except Exception:
        pass
    pygame.quit()
    sys.exit()


if __name__ == "__main__":
    main()
