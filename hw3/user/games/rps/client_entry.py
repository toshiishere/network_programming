# developers/games/rps/client_entry.py
import argparse
import socket
import pygame
import sys
import threading

WIDTH, HEIGHT = 720, 480
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


def parse_result(msg: str):
    if not msg.startswith("result"):
        return "unknown", msg
    parts = msg.split(None, 2)
    if len(parts) < 2:
        return "unknown", msg
    outcome = parts[1]
    detail = parts[2] if len(parts) > 2 else ""
    return outcome, detail


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
        "result_text": "Waiting for opponent...",
        "status_text": "Waiting for opponent...",
        "can_choose": False,
        "waiting_for_result": False,
        "outcome": "pending",
    }

    clock = pygame.time.Clock()

    def wait_for_start():
        try:
            msg = recv_line(sock)
            if msg.startswith("your_turn"):
                state["status_text"] = "Your turn! Pick rock, paper or scissors."
                state["result_text"] = state["status_text"]
                state["can_choose"] = True
            else:
                state["status_text"] = f"Unexpected: {msg}"
                state["result_text"] = state["status_text"]
        except Exception as exc:
            state["status_text"] = f"Connection error: {exc}"
            state["result_text"] = state["status_text"]

    def wait_for_result():
        try:
            rmsg = recv_line(sock)
            outcome, detail = parse_result(rmsg)
            state["result_text"] = detail or rmsg
            state["outcome"] = outcome
        except Exception as exc:
            state["result_text"] = f"Connection error: {exc}"
        finally:
            state["status_text"] = state["result_text"]
            state["waiting_for_result"] = False

    threading.Thread(target=wait_for_start, daemon=True).start()

    running = True
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
                            send_line(sock, ch)
                            state["can_choose"] = False
                            state["waiting_for_result"] = True
                            state["status_text"] = "Waiting for result..."
                            threading.Thread(target=wait_for_result, daemon=True).start()
                        except Exception as exc:
                            state["result_text"] = f"Send failed: {exc}"
                        break

        screen.fill((18, 18, 40))
        pygame.draw.rect(screen, (30, 30, 70), (30, 30, WIDTH - 60, HEIGHT - 60), border_radius=18)

        title_surf = big_font.render("Rock Paper Scissors", True, (255, 255, 255))
        screen.blit(title_surf, (WIDTH // 2 - title_surf.get_width() // 2, 40))

        instr_surf = font.render(state["status_text"], True, (200, 220, 255))
        screen.blit(instr_surf, (WIDTH // 2 - instr_surf.get_width() // 2, 120))

        for rect, ch in buttons:
            pygame.draw.rect(screen, (80, 90, 200), rect, border_radius=12)
            pygame.draw.rect(screen, (120, 140, 255), rect, width=3, border_radius=12)
            txt = font.render(ch.capitalize(), True, (255, 255, 255))
            screen.blit(txt, (rect.x + rect.width // 2 - txt.get_width() // 2,
                              rect.y + rect.height // 2 - txt.get_height() // 2))

        color_map = {
            "win": (60, 200, 100),
            "lose": (220, 80, 80),
            "tie": (230, 200, 80),
        }
        banner_color = color_map.get(state.get("outcome"), (120, 120, 120))
        pygame.draw.rect(screen, banner_color, (60, 340, WIDTH - 120, 60), border_radius=10)
        res_surf = font.render(state["result_text"], True, (0, 0, 0))
        screen.blit(res_surf, (WIDTH // 2 - res_surf.get_width() // 2, 350))

        legend = small_font.render("Outcome colors: green=win, yellow=tie, red=lose", True, (180, 180, 200))
        screen.blit(legend, (60, HEIGHT - 40))

        pygame.display.flip()

    sock.close()
    pygame.quit()
    sys.exit()


if __name__ == "__main__":
    main()
