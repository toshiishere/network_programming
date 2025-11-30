import argparse
import socket
import pygame
import sys
import threading

WIDTH, HEIGHT = 720, 480
CHOICES = ("rock", "paper", "scissors")


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


def send_line(sock: socket.socket, text: str):
    sock.sendall((text + "\n").encode("utf-8"))


def parse_result(msg: str):
    """
    Parse a result line like:
      result outcome=win you=rock winning=paper counts=rock:1 paper:2 scissors:0
    """
    if not msg.startswith("result"):
        return "unknown", msg
    parts = msg.split()
    outcome = "unknown"
    detail_bits = []
    for p in parts[1:]:
        if p.startswith("outcome="):
            outcome = p.split("=", 1)[1]
        else:
            detail_bits.append(p)
    detail_text = " ".join(detail_bits) if detail_bits else msg
    return outcome, detail_text


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", required=True, type=int)
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((args.host, args.port))

    pygame.init()
    screen = pygame.display.set_mode((WIDTH, HEIGHT))
    pygame.display.set_caption("Rock Paper Scissors")

    font = pygame.font.SysFont(None, 32)
    big_font = pygame.font.SysFont(None, 56)
    small_font = pygame.font.SysFont(None, 24)

    buttons = []
    for i, ch in enumerate(CHOICES):
        rect = pygame.Rect(80 + i * 200, 240, 160, 70)
        buttons.append((rect, ch))

    state = {
        "player_idx": None,
        "total_players": None,
        "result_text": "Waiting for players...",
        "status_text": "Waiting for players to join...",
        "can_choose": False,
        "waiting_for_result": False,
        "outcome": "pending",
        "finished": False,
    }
    stopped = threading.Event()

    def listener():
        try:
            while not stopped.is_set():
                msg = recv_line(sock)
                if msg.startswith("welcome"):
                    parts = msg.split()
                    if len(parts) >= 3:
                        state["player_idx"] = int(parts[1])
                        state["total_players"] = int(parts[2])
                        state["status_text"] = (
                            f"You are player {state['player_idx']}/{state['total_players']}"
                        )
                        state["result_text"] = "Waiting for the game to start..."
                elif msg.startswith("start"):
                    state["status_text"] = "Game starting! Choose once when prompted."
                    state["result_text"] = "Waiting for prompt..."
                elif msg.startswith("choose"):
                    state["can_choose"] = True
                    state["waiting_for_result"] = False
                    state["status_text"] = "Pick rock, paper, or scissors."
                    state["result_text"] = "Tap a button to lock in."
                elif msg.startswith("result"):
                    outcome, detail = parse_result(msg)
                    state["result_text"] = detail
                    state["status_text"] = detail
                    state["outcome"] = outcome
                    state["waiting_for_result"] = False
                    state["can_choose"] = False
                    state["finished"] = True
                elif msg.startswith("error"):
                    state["status_text"] = msg
                    state["result_text"] = msg
                    state["can_choose"] = False
                    state["waiting_for_result"] = False
                else:
                    state["status_text"] = f"Server: {msg}"
        except Exception as exc:
            if not stopped.is_set():
                if state.get("finished"):
                    # Server closed after game end: keep final result text
                    state["status_text"] = state.get("result_text", "Game finished")
                else:
                    state["status_text"] = f"Connection error: {exc}"
                    state["result_text"] = state["status_text"]
                state["can_choose"] = False

    threading.Thread(target=listener, daemon=True).start()

    running = True
    clock = pygame.time.Clock()
    while running:
        clock.tick(30)
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif (event.type == pygame.MOUSEBUTTONDOWN and state["can_choose"]
                  and not state["waiting_for_result"]):
                x, y = event.pos
                for rect, ch in buttons:
                    if rect.collidepoint(x, y):
                        try:
                            send_line(sock, f"choice {ch}")
                            state["can_choose"] = False
                            state["waiting_for_result"] = True
                            state["status_text"] = "Waiting for everyone to finish..."
                            state["result_text"] = state["status_text"]
                        except Exception as exc:
                            state["result_text"] = f"Send failed: {exc}"
                        break

        screen.fill((12, 16, 40))
        pygame.draw.rect(screen, (30, 32, 80), (30, 30, WIDTH - 60, HEIGHT - 60), border_radius=18)

        title_surf = big_font.render("Rock Paper Scissors", True, (255, 255, 255))
        screen.blit(title_surf, (WIDTH // 2 - title_surf.get_width() // 2, 40))

        subtitle = state.get("status_text", "")
        instr_surf = font.render(subtitle, True, (210, 220, 255))
        screen.blit(instr_surf, (WIDTH // 2 - instr_surf.get_width() // 2, 120))

        for rect, ch in buttons:
            base_color = (80, 90, 200) if state["can_choose"] else (60, 60, 90)
            pygame.draw.rect(screen, base_color, rect, border_radius=12)
            pygame.draw.rect(screen, (120, 140, 255), rect, width=3, border_radius=12)
            txt = font.render(ch.capitalize(), True, (255, 255, 255))
            screen.blit(txt, (rect.x + rect.width // 2 - txt.get_width() // 2,
                              rect.y + rect.height // 2 - txt.get_height() // 2))

        color_map = {
            "win": (60, 200, 100),
            "lose": (220, 80, 80),
            "tie": (230, 200, 80),
            "pending": (120, 120, 120),
        }
        banner_color = color_map.get(state.get("outcome"), (120, 120, 120))
        pygame.draw.rect(screen, banner_color, (60, 340, WIDTH - 120, 60), border_radius=10)
        res_text = state.get("result_text", "")
        res_surf = font.render(res_text, True, (0, 0, 0))
        screen.blit(res_surf, (WIDTH // 2 - res_surf.get_width() // 2, 350))

        legend = small_font.render("Outcome colors: green=win, yellow=tie, red=lose", True, (180, 180, 200))
        screen.blit(legend, (60, HEIGHT - 40))

        pygame.display.flip()

    stopped.set()
    try:
        sock.close()
    except Exception:
        pass
    pygame.quit()
    sys.exit()


if __name__ == "__main__":
    main()
