#!/usr/bin/python3
import sys
import socket, struct, json, sys, time, select
import pygame

# ========= Config =========
HOST = "127.0.0.1"      # your game server IP
PORT = 45632            # your game server's listening port
CELL = 32               # pixels per cell
FPS  = 60               # redraw limit

BOARD_W, BOARD_H = 10, 20

# Colors for piece IDs (C++ enum: 0=Empty, 1=I,2=O,3=T,4=S,5=Z,6=J,7=L)
COLORS = {
    0: (18, 18, 18),      # board bg
    1: (0, 240, 240),     # I
    2: (240, 240, 0),     # O
    3: (160, 0, 240),     # T
    4: (0, 240, 0),       # S
    5: (240, 0, 0),       # Z
    6: (0, 0, 240),       # J
    7: (240, 160, 0),     # L
}
GHOST = (80, 80, 80)
GRID  = (30, 30, 30)
TEXT  = (230, 230, 230)
PANEL = (12, 12, 12)
BORDER= (70, 70, 70)

# ========= Length-prefixed helpers =========
def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf

def recv_msg(sock):
    """Receive one length-prefixed (big-endian u32) JSON string."""
    hdr = recv_exact(sock, 4)
    if not hdr:
        return None
    (length,) = struct.unpack("!I", hdr)
    if length == 0 or length > 10_000_000:
        return None
    payload = recv_exact(sock, length)
    if not payload:
        return None
    return payload.decode("utf-8")

def send_msg(sock, obj):
    """Send a JSON object as length-prefixed string."""
    data = json.dumps(obj, separators=(",", ":")).encode("utf-8")
    sock.sendall(struct.pack("!I", len(data)) + data)

# ========= Drawing =========
def draw_cell(surf, x, y, color):
    r = pygame.Rect(x, y, CELL, CELL)
    pygame.draw.rect(surf, color, r)
    pygame.draw.rect(surf, BORDER, r, 1)

def render_board(screen, state):
    board = state.get("board", [[0]*BOARD_W for _ in range(BOARD_H)])
    active = state.get("active", {"id":0,"x":0,"y":0,"rot":0})
    ghostY = state.get("ghostY", 0)
    hold   = state.get("hold", 0)
    nxt    = state.get("next", [0,0,0,0,0,0])
    score  = state.get("score", 0)
    lines  = state.get("lines", 0)
    level  = state.get("level", 1)
    gameOver = state.get("gameOver", False)

    # Layout
    left = 20
    top  = 20
    board_px_w = BOARD_W*CELL
    board_px_h = BOARD_H*CELL

    # Panels background
    screen.fill((0,0,0))
    pygame.draw.rect(screen, PANEL, (left-8, top-8, board_px_w+16, board_px_h+16), border_radius=8)

    # Grid + fixed cells
    for y in range(BOARD_H):
        for x in range(BOARD_W):
            cx = left + x*CELL
            cy = top  + y*CELL
            base = COLORS.get(board[y][x], COLORS[0])
            draw_cell(screen, cx, cy, base)

    # Ghost piece (project active down to ghostY rows)
    # We don't know the shape matrix here; the C++ sends ghostY only.
    # A simple ghost visualization: draw a horizontal shadow at ghostY using active width.
    # Better: rely on server-drawn ghost overlay via 'board'; but we can approximate:
    # We'll skip accurate ghost blocks to avoid re-implementing rotation here.
    # Optional: draw a subtle line at ghostY:
    gy = top + ghostY*CELL
    pygame.draw.rect(screen, (40,40,40), (left, gy, board_px_w, 2))

    # Info panel (right)
    panel_x = left + board_px_w + 24
    panel_w = 6*CELL + 24
    pygame.draw.rect(screen, PANEL, (panel_x-8, top-8, panel_w, board_px_h+16), border_radius=8)

    # Text
    font = pygame.font.SysFont("consolas", 20)
    def blit_text(label, yoff):
        surf = font.render(label, True, TEXT)
        screen.blit(surf, (panel_x, top + yoff))
    blit_text(f"Score: {score}", 0)
    blit_text(f"Lines: {lines}", 28)
    blit_text(f"Level: {level}", 56)

    # Hold box
    blit_text("Hold", 100)
    draw_mini_piece(screen, panel_x, top+130, hold)

    # Next box
    blit_text("Next", 200)
    for i, pid in enumerate(nxt[:5]):
        draw_mini_piece(screen, panel_x, top+230 + i*70, pid)

    # Game over overlay
    if gameOver:
        big = pygame.font.SysFont("consolas", 36, bold=True)
        s = big.render("GAME OVER", True, (255,80,80))
        rect = s.get_rect(center=(left+board_px_w//2, top+board_px_h//2))
        screen.blit(s, rect)

def draw_mini_piece(screen, x, y, pid):
    # tiny preview cell size
    cs = CELL//2
    # simple 4x2 area to show a symbol block representing pid
    # We won't rotate; just draw a 3x2 cluster colored by pid
    color = COLORS.get(int(pid), COLORS[0])
    for dy in range(2):
        for dx in range(3):
            r = pygame.Rect(x + dx*(cs+2), y + dy*(cs+2), cs, cs)
            pygame.draw.rect(screen, color, r)
            pygame.draw.rect(screen, BORDER, r, 1)

# ========= Input mapping =========
def key_to_action(key):
    # Must match what your C++ server expects (e.g., left/right/down/hard/cw/ccw/hold)
    if key == pygame.K_LEFT:  return "left"
    if key == pygame.K_RIGHT: return "right"
    if key == pygame.K_DOWN:  return "down"
    if key == pygame.K_SPACE: return "hard"
    if key == pygame.K_z:     return "ccw"
    if key == pygame.K_x:     return "cw"
    if key == pygame.K_c:     return "hold"
    return None

# ========= Main =========
def main():

    # connect socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect((HOST, PORT))
        sock.setblocking(False)
        print(f"Connected to {HOST}:{PORT}")
    except Exception as e:
        print("Failed to connect:", e)
        pygame.quit()
        sys.exit(1)

    







    pygame.init()
    pygame.display.set_caption("Tetris Client")
    W = 20 + BOARD_W*CELL + 24 + 6*CELL + 24 + 20
    H = 20 + BOARD_H*CELL + 20
    screen = pygame.display.set_mode((W, H))
    clock = pygame.time.Clock()
    # If your server needs an initial sync request, send it here
    # send_msg(sock, {"type":"sync"})

    latest_state = {
        "board": [[0]*BOARD_W for _ in range(BOARD_H)],
        "score": 0, "lines": 0, "level": 1, "gameOver": False,
        "active": {"id":0,"x":0,"y":0,"rot":0},
        "ghostY": 0, "hold": 0, "next": [0,0,0,0,0,0]
    }

    partial = b""
    expected = None

    running = True
    while running:
        # --- events / input ---
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    running = False
                action = key_to_action(event.key)
                if action:
                    # send input as JSON framed message
                    send_msg(sock, {"type":"input", "value":action})

        # --- receive nonblocking ---
        try:
            # use select to check readability
            r, _, _ = select.select([sock], [], [], 0)
            if r:
                # read a full framed message using our local buffer
                # We'll do manual framing to support nonblocking recv
                if expected is None:
                    # need header
                    chunk = sock.recv(4 - len(partial))
                    if chunk:
                        partial += chunk
                    if len(partial) == 4:
                        (expected,) = struct.unpack("!I", partial)
                        partial = b""
                        if expected == 0 or expected > 10_000_000:
                            raise RuntimeError("Bad frame length")
                if expected is not None:
                    chunk = sock.recv(expected - len(partial))
                    if chunk:
                        partial += chunk
                    if len(partial) == expected:
                        # full message!
                        msg = partial.decode("utf-8")
                        partial = b""
                        expected = None
                        try:
                            latest_state = json.loads(msg)
                        except Exception as e:
                            print("JSON parse error:", e)
        except BlockingIOError:
            pass
        except ConnectionResetError:
            print("Connection reset by server.")
            running = False
        except Exception as e:
            # print unexpected receive errors but keep running if possible
            # (comment this out if too chatty)
            # print("recv err:", e)
            pass

        # --- render ---
        render_board(screen, latest_state)
        pygame.display.flip()
        clock.tick(FPS)

    # graceful exit
    try:
        send_msg(sock, {"type":"disconnect"})
    except Exception:
        pass
    sock.close()
    pygame.quit()

if __name__ == "__main__":
    main()
