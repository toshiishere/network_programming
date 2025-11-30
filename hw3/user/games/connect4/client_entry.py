import argparse
import socket
import pygame
import sys
import threading

WIDTH, HEIGHT = 700, 600
ROWS, COLS = 6, 7
CELL_SIZE = 80
MARGIN = 20


def send_line(sock, text: str):
    sock.sendall((text + "\n").encode("utf-8"))


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


def parse_state(state_str):
    cells = [int(ch) for ch in state_str.strip()]
    board = [[0 for _ in range(COLS)] for _ in range(ROWS)]
    for i, val in enumerate(cells):
        r = i // COLS
        c = i % COLS
        board[r][c] = val
    return board


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", required=True, type=int)
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((args.host, args.port))

    pygame.init()
    screen = pygame.display.set_mode((WIDTH, HEIGHT))
    pygame.display.set_caption("Connect 4")
    font = pygame.font.SysFont(None, 32)
    big_font = pygame.font.SysFont(None, 48)

    state = {
        "role": None,
        "board": [[0 for _ in range(COLS)] for _ in range(ROWS)],
        "turn": 1,
        "can_move": False,
        "status": "Connecting...",
        "game_over": False,
        "result": "",
    }

    def listener():
        try:
            while True:
                msg = recv_line(sock)
                if msg.startswith("role"):
                    _, role_s = msg.split()
                    state["role"] = int(role_s)
                    state["status"] = f"You are player {state['role']}"
                elif msg.startswith("state"):
                    try:
                        _, turn_s, board_s = msg.split()
                        state["turn"] = int(turn_s)
                        state["board"] = parse_state(board_s)
                        state["status"] = f"Player {state['turn']}'s turn"
                    except Exception:
                        state["status"] = "Bad state from server"
                    state["can_move"] = False
                elif msg.startswith("your_turn"):
                    state["can_move"] = True
                    state["status"] = "Your turn: click a column"
                elif msg.startswith("invalid"):
                    state["status"] = f"Invalid move: {msg}"
                    state["can_move"] = True
                elif msg.startswith("game_over"):
                    parts = msg.split()
                    if len(parts) >= 2 and parts[1] == "winner" and len(parts) >= 3:
                        winner = parts[2]
                        if str(state.get("role")) == winner:
                            state["result"] = "You win!"
                        elif winner in ("1", "2"):
                            state["result"] = f"Player {winner} wins"
                        else:
                            state["result"] = "Game over"
                    else:
                        state["result"] = "Draw"
                    state["status"] = state["result"]
                    state["game_over"] = True
                    state["can_move"] = False
        except Exception:
            state["status"] = "Disconnected"
            state["can_move"] = False

    threading.Thread(target=listener, daemon=True).start()

    def draw_board():
        screen.fill((20, 20, 60))
        for r in range(ROWS):
            for c in range(COLS):
                x = MARGIN + c * CELL_SIZE
                y = MARGIN + r * CELL_SIZE
                pygame.draw.rect(screen, (30, 30, 120), (x, y, CELL_SIZE, CELL_SIZE))
                pygame.draw.circle(screen, (0, 0, 0), (x + CELL_SIZE // 2, y + CELL_SIZE // 2), CELL_SIZE // 2 - 4)
                val = state["board"][r][c]
                if val == 1:
                    color = (220, 40, 40)
                elif val == 2:
                    color = (240, 200, 50)
                else:
                    color = None
                if color:
                    pygame.draw.circle(screen, color, (x + CELL_SIZE // 2, y + CELL_SIZE // 2), CELL_SIZE // 2 - 6)

        title = big_font.render("Connect 4", True, (255, 255, 255))
        screen.blit(title, (WIDTH // 2 - title.get_width() // 2, 10))

        status = font.render(state["status"], True, (240, 240, 240))
        screen.blit(status, (MARGIN, HEIGHT - 60))

        role_txt = f"You are player {state['role']}" if state.get("role") else "Connecting..."
        role_surf = font.render(role_txt, True, (200, 200, 200))
        screen.blit(role_surf, (MARGIN, HEIGHT - 30))

    running = True
    clock = pygame.time.Clock()
    while running:
        clock.tick(30)
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.MOUSEBUTTONDOWN and state["can_move"] and not state["game_over"]:
                x, _ = event.pos
                col = (x - MARGIN) // CELL_SIZE
                try:
                    send_line(sock, f"move {int(col)}")
                    state["can_move"] = False
                except Exception:
                    state["status"] = "Failed to send move"

        draw_board()
        pygame.display.flip()

    try:
        sock.close()
    except Exception:
        pass
    pygame.quit()
    sys.exit()


if __name__ == "__main__":
    main()
