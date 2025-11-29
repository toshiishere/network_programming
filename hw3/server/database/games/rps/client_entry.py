# developers/games/rps/client_entry.py
import argparse
import socket
import pygame
import sys

WIDTH, HEIGHT = 600, 400
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
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((args.host, args.port))

    pygame.init()
    screen = pygame.display.set_mode((WIDTH, HEIGHT))
    pygame.display.set_caption("Rock Paper Scissors")

    font = pygame.font.SysFont(None, 36)
    big_font = pygame.font.SysFont(None, 48)

    buttons = []
    for i, ch in enumerate(CHOICES):
        rect = pygame.Rect(50 + i * 180, 200, 150, 60)
        buttons.append((rect, ch))

    choice_made = None
    result_text = "Waiting for server..."
    waiting_for_turn = True

    clock = pygame.time.Clock()

    # Wait for "your_turn"
    msg = recv_line(sock)
    if not msg.startswith("your_turn"):
        result_text = "Protocol error"

    running = True
    while running:
        clock.tick(30)
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.MOUSEBUTTONDOWN and waiting_for_turn:
                x, y = event.pos
                for rect, ch in buttons:
                    if rect.collidepoint(x, y):
                        choice_made = ch
                        send_line(sock, ch)
                        waiting_for_turn = False
                        # wait for result
                        rmsg = recv_line(sock)
                        result_text = rmsg
                        break

        screen.fill((30, 30, 30))

        title_surf = big_font.render("Rock Paper Scissors", True, (255, 255, 255))
        screen.blit(title_surf, (WIDTH // 2 - title_surf.get_width() // 2, 50))

        instr = "Click a choice" if waiting_for_turn else "Result received"
        instr_surf = font.render(instr, True, (200, 200, 200))
        screen.blit(instr_surf, (WIDTH // 2 - instr_surf.get_width() // 2, 120))

        for rect, ch in buttons:
            pygame.draw.rect(screen, (70, 70, 200), rect)
            txt = font.render(ch.capitalize(), True, (255, 255, 255))
            screen.blit(txt, (rect.x + rect.width // 2 - txt.get_width() // 2,
                              rect.y + rect.height // 2 - txt.get_height() // 2))

        res_surf = font.render(result_text, True, (255, 255, 0))
        screen.blit(res_surf, (WIDTH // 2 - res_surf.get_width() // 2, 300))

        pygame.display.flip()

    sock.close()
    pygame.quit()
    sys.exit()


if __name__ == "__main__":
    main()
