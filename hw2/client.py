#!/usr/bin/python3
import socket, struct, json, select, sys
import pygame
import threading

# ========= Config =========
SERVER_IP = "127.0.0.1"
SERVER_PORT = 45632


# ========= Length-prefixed message helpers =========
def recv_exact(sock, n):
    """Receive exactly n bytes or return None if disconnected."""
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


def recv_msg(sock):
    """Receive a single length-prefixed JSON message (C++ compatible)."""
    hdr = recv_exact(sock, 4)
    if not hdr:
        return None
    (length,) = struct.unpack("!I", hdr)
    if length <= 0 or length > 10_000_000:
        print("Invalid length from server:", length)
        return None
    payload = recv_exact(sock, length)
    if not payload:
        return None
    return payload.decode("utf-8")


def send_msg(sock, obj):
    """Send a JSON message with a 4-byte length prefix."""
    data = json.dumps(obj, separators=(",", ":")).encode("utf-8")
    sock.sendall(struct.pack("!I", len(data)) + data)


# ========= Login/Register =========
def login_or_register(sock, action: str, name: str, password: str):
    """Send login or register JSON to the game server."""
    req = {
        "action": action,  # "login" or "register"
        "name": name,
        "password": password
    }
    print(f"→ Sending {action} request for user '{name}'...")
    send_msg(sock, req)
    reply = recv_msg(sock)
    if not reply:
        print("⚠️  No reply or disconnected from server.")
        return
    try:
        res = json.loads(reply)
        if(res['response']=='success'):
            return True
        else: 
            print("← Server response:")
            print(json.dumps(res, indent=2))
    except Exception as e:
        print("⚠️  Failed to parse JSON:", e)
        print("Raw reply:", reply)


def lobby_op(sock, action: str, roomname:str=""):
    if(action in ('curinvite', 'curroom')):
        req = {
            "action": action
        }
        send_msg(sock, req)
        reply = recv_msg(sock)
        if not reply:
            print("⚠️  No reply or disconnected from server.")
            return False
        try:
            res = json.loads(reply)
            if(res['response']=='success'):
                for room in res['data']:
                    print(json.dumps(room, indent=2))
                return False #success, but noting changed
            else: 
                print("← Server response:")
                print(json.dumps(res, indent=2))
        except Exception as e:
            print("⚠️  Failed to parse JSON:", e)
            print("Raw reply:", reply)
    elif action in ('create', 'join', 'spec'):
        req = {
            "action": action,
            "roomname" : roomname
        }
        if action == 'create':
            req["visibility"]='public' if input("make it public? y or n ")=='y' else 'private'
        send_msg(sock, req)
        reply = recv_msg(sock)
        if not reply:
            print("⚠️  No reply or disconnected from server.")
            return False
        try:
            res = json.loads(reply)
            if(res['response']=='success'):
                print('success, now join THE room')
                return True #success, move to room
            else: 
                print("← Server response:")
                print(json.dumps(res, indent=2))
        except Exception as e:
            print("⚠️  Failed to parse JSON:", e)
            print("Raw reply:", reply)

def room_op(sock, action:str):
    if(action == "invite"):
        toinvite=input("Enter someone you want to invte: ").strip().lower()
        req = {
            "action": action,
            "name": toinvite
        }
        send_msg(sock, req)
        reply = recv_msg(sock)
        if not reply:
            print("⚠️  No reply or disconnected from server.")
            return -1
        try:
            res = json.loads(reply)
            if(res['response']=='success'):
                print("invited "+toinvite)
                return 0
            else: 
                print("← Server response:")
                print(json.dumps(res, indent=2))
        except Exception as e:
            print("⚠️  Failed to parse JSON:", e)
            print("Raw reply:", reply)
    elif action == "start":
        req = {
            "action": action,
        }
        send_msg(sock, req)
        reply = recv_msg(sock)
        if not reply:
            print("⚠️  No reply or disconnected from server.")
            return -1
        try:
            res = json.loads(reply)
            if res.get('response') == 'success':
                print("continue to start the game...")
                # Don't return yet, wait for the "action":"start" message
                return 0
            elif res.get('action') == 'start':
                print("\nReceived message:", json.dumps(res, indent=2))
                print("Game is starting!")
                return 1
            else:
                print("← Server response:")
                print(json.dumps(res, indent=2))
                return -1
        except Exception as e:
            print("⚠️  Failed to parse JSON:", e)
            print("Raw reply:", reply)
            return -1


# ========= Pygame Tetris GUI =========
# Tetris piece shapes (matching C++ SHAPES array from tetris.cpp)
SHAPES = {
    0: [],  # Empty
    1: [(0,1), (1,1), (2,1), (3,1)],  # I
    2: [(1,1), (2,1), (1,2), (2,2)],  # O
    3: [(0,1), (1,1), (2,1), (1,2)],  # T
    4: [(1,1), (2,1), (0,2), (1,2)],  # S
    5: [(0,1), (1,1), (1,2), (2,2)],  # Z
    6: [(0,1), (1,1), (2,1), (0,2)],  # J
    7: [(0,1), (1,1), (2,1), (2,2)],  # L
}

# Colors for each piece type
COLORS = {
    0: (40, 40, 40),      # Empty - dark gray
    1: (0, 255, 255),     # I - cyan
    2: (255, 255, 0),     # O - yellow
    3: (160, 0, 255),     # T - purple
    4: (0, 255, 0),       # S - green
    5: (255, 0, 0),       # Z - red
    6: (0, 0, 255),       # J - blue
    7: (255, 165, 0),     # L - orange
    8: (100, 100, 100),   # Ghost - gray (outline will be drawn)
    9: (255, 255, 255),   # Active - white (will use piece color)
}

def rotate_coords(x, y, rot):
    """Rotate piece coordinates according to rotation (0-3)."""
    nx, ny = x, y
    for _ in range(rot % 4):
        nx, ny = 3 - ny, nx
    return nx, ny

def get_piece_cells(piece_id, rot):
    """Get rotated cell positions for a piece."""
    if piece_id not in SHAPES or piece_id == 0:
        return []
    cells = []
    for dx, dy in SHAPES[piece_id]:
        rx, ry = rotate_coords(dx, dy, rot)
        cells.append((rx, ry))
    return cells

def convert_board_to_2d(board_array):
    """Convert 1D board array to 2D array."""
    BOARD_WIDTH = 10
    BOARD_HEIGHT = 20

    # Convert to 2D board
    board = []
    for y in range(BOARD_HEIGHT):
        row = []
        for x in range(BOARD_WIDTH):
            idx = y * BOARD_WIDTH + x
            if idx < len(board_array):
                row.append(board_array[idx])
            else:
                row.append(0)
        board.append(row)
    return board


def draw_mini_board(screen, game_state, x, y, cell_size, label):
    """Draw a miniature Tetris board."""
    BOARD_WIDTH = 10
    BOARD_HEIGHT = 20

    # Draw label
    font_small = pygame.font.Font(None, 24)
    label_text = font_small.render(label, True, (255, 255, 255))
    screen.blit(label_text, (x, y - 25))

    # Draw border
    border_rect = pygame.Rect(x - 1, y - 1,
                              BOARD_WIDTH * cell_size + 2,
                              BOARD_HEIGHT * cell_size + 2)
    pygame.draw.rect(screen, (100, 100, 100), border_rect, 1)

    # Get board data and convert to 2D if needed
    board_data = game_state.get('b') or game_state.get('board')
    if isinstance(board_data, list) and len(board_data) > 0 and not isinstance(board_data[0], list):
        # 1D array, convert to 2D
        board = convert_board_to_2d(board_data)
    else:
        # Already 2D or empty
        board = board_data or []

    # Draw board cells (now includes ghost and active pieces)
    for row_y in range(BOARD_HEIGHT):
        for col_x in range(BOARD_WIDTH):
            if row_y < len(board) and col_x < len(board[row_y]):
                cell_value = board[row_y][col_x]
                rect = pygame.Rect(x + col_x * cell_size,
                                  y + row_y * cell_size,
                                  cell_size - 1, cell_size - 1)

                if cell_value == 8:  # Ghost piece
                    # Draw ghost as gray outline
                    pygame.draw.rect(screen, (100, 100, 100), rect, 1)
                elif cell_value == 9:  # Active piece
                    # Draw active piece as white/bright
                    pygame.draw.rect(screen, (255, 255, 255), rect)
                elif cell_value > 0:  # Regular locked piece
                    color = COLORS.get(cell_value, (40, 40, 40))
                    pygame.draw.rect(screen, color, rect)
                else:  # Empty cell
                    color = COLORS.get(0, (40, 40, 40))
                    pygame.draw.rect(screen, color, rect)

    # Draw game over if applicable (check both compact 'g' and verbose 'gameOver')
    if game_state.get('g', game_state.get('gameOver', False)):
        game_over_text = font_small.render("GAME OVER", True, (255, 0, 0))
        text_rect = game_over_text.get_rect(center=(x + BOARD_WIDTH * cell_size // 2,
                                                      y + BOARD_HEIGHT * cell_size // 2))
        # Draw background for text
        bg_rect = text_rect.inflate(10, 5)
        pygame.draw.rect(screen, (0, 0, 0), bg_rect)
        screen.blit(game_over_text, text_rect)


def draw_tetris_game(screen, my_state, opponent_state, player_name):
    """Draw the Tetris game state on the pygame screen."""
    # Constants
    CELL_SIZE = 30
    BOARD_WIDTH = 10
    BOARD_HEIGHT = 20
    BOARD_X = 50
    BOARD_Y = 50

    # Clear screen
    screen.fill((20, 20, 20))

    # Draw player name
    font = pygame.font.Font(None, 36)
    name_text = font.render(f"Player: {player_name}", True, (255, 255, 255))
    screen.blit(name_text, (BOARD_X, 10))

    # Draw main board border
    border_rect = pygame.Rect(BOARD_X - 2, BOARD_Y - 2,
                              BOARD_WIDTH * CELL_SIZE + 4,
                              BOARD_HEIGHT * CELL_SIZE + 4)
    pygame.draw.rect(screen, (100, 100, 100), border_rect, 2)

    # Get board data and convert to 2D if needed
    board_data = my_state.get('b') or my_state.get('board')
    if isinstance(board_data, list) and len(board_data) > 0 and not isinstance(board_data[0], list):
        # 1D array, convert to 2D
        board = convert_board_to_2d(board_data)
    else:
        # Already 2D or empty
        board = board_data or []

    # Draw main board cells (now includes ghost and active pieces)
    for y in range(BOARD_HEIGHT):
        for x in range(BOARD_WIDTH):
            if y < len(board) and x < len(board[y]):
                cell_value = board[y][x]
                rect = pygame.Rect(BOARD_X + x * CELL_SIZE,
                                  BOARD_Y + y * CELL_SIZE,
                                  CELL_SIZE - 1, CELL_SIZE - 1)

                if cell_value == 8:  # Ghost piece
                    # Draw ghost as gray outline
                    pygame.draw.rect(screen, (150, 150, 150), rect, 2)
                elif cell_value == 9:  # Active piece
                    # Draw active piece as bright white
                    pygame.draw.rect(screen, (255, 255, 255), rect)
                elif cell_value > 0:  # Regular locked piece (1-7)
                    color = COLORS.get(cell_value, (40, 40, 40))
                    pygame.draw.rect(screen, color, rect)
                else:  # Empty cell (0)
                    color = COLORS.get(0, (40, 40, 40))
                    pygame.draw.rect(screen, color, rect)

    # Draw opponent's board in mini view (top right)
    MINI_CELL_SIZE = 12
    MINI_X = 380
    MINI_Y = 50
    if opponent_state:
        draw_mini_board(screen, opponent_state, MINI_X, MINI_Y, MINI_CELL_SIZE, "Opponent")

    # Draw game info (score, lines, level)
    INFO_X = BOARD_X + BOARD_WIDTH * CELL_SIZE + 30
    INFO_Y = BOARD_Y + 300  # Move down to make room for opponent board

    font_small = pygame.font.Font(None, 28)
    # Support both compact ('s', 'l', 'v', 'g') and verbose field names
    score = my_state.get('s', my_state.get('score', 0))
    lines = my_state.get('l', my_state.get('lines', 0))
    level = my_state.get('v', my_state.get('level', 1))
    game_over = my_state.get('g', my_state.get('gameOver', False))

    info_texts = [
        f"Score: {score}",
        f"Lines: {lines}",
        f"Level: {level}",
        "",
    ]

    if game_over:
        info_texts.append("GAME OVER")

    for i, text in enumerate(info_texts):
        if text == "GAME OVER":
            color = (255, 0, 0)
        else:
            color = (255, 255, 255)
        text_surface = font_small.render(text, True, color)
        screen.blit(text_surface, (INFO_X, INFO_Y + i * 35))

    # Draw hold piece (support both 'h' and 'hold')
    hold_piece = my_state.get('h', my_state.get('hold', 0))
    if hold_piece > 0:
        hold_text = font_small.render("Hold:", True, (255, 255, 255))
        screen.blit(hold_text, (INFO_X, INFO_Y + 150))

        cells = get_piece_cells(hold_piece, 0)
        color = COLORS.get(hold_piece, (255, 255, 255))
        for dx, dy in cells:
            rect = pygame.Rect(INFO_X + dx * 20, INFO_Y + 180 + dy * 20, 18, 18)
            pygame.draw.rect(screen, color, rect)

    # Draw controls
    CONTROL_Y = BOARD_Y + BOARD_HEIGHT * CELL_SIZE - 200
    controls = [
        "Controls:",
        "leftkey / rightkey : Move",
        "downkey : Soft Drop",
        "Space : Hard Drop",
        "Z : Rotate CCW",
        "X : Rotate CW",
        "C : Hold",
        "ESC : Quit"
    ]

    for i, text in enumerate(controls):
        text_surface = font_small.render(text, True, (150, 150, 150))
        screen.blit(text_surface, (INFO_X, CONTROL_Y + i * 25))

    pygame.display.flip()


def play_game(lobby_sock, room_id, player_name,):
    """Connect to game server and play with pygame GUI."""
    # Initialize pygame
    pygame.init()
    screen = pygame.display.set_mode((600, 700))
    pygame.display.set_caption("Tetris Battle")
    clock = pygame.time.Clock()

    # Connect to the game server (room_id determines port)
    game_port = room_id + 50000
    game_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    try:
        print(f"Connecting to game server on port {game_port}...")
        game_sock.connect((SERVER_IP, game_port))
        print(f"✅ Connected to game on port {game_port}!")
        # Keep socket in blocking mode; we'll use select() to check for data
    except Exception as e:
        print(f"❌ Failed to connect to game server: {e}")
        pygame.quit()
        return

    running = True
    my_state = {}
    opponent_state = {}

    # Main game loop
    while running:
        # Handle pygame events
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN:
                action = None
                if event.key == pygame.K_LEFT:
                    action = "Left"
                elif event.key == pygame.K_RIGHT:
                    action = "Right"
                elif event.key == pygame.K_DOWN:
                    action = "SoftDrop"
                elif event.key == pygame.K_SPACE:
                    action = "HardDrop"
                elif event.key == pygame.K_z:
                    action = "RotateCCW"
                elif event.key == pygame.K_x:
                    action = "RotateCW"
                elif event.key == pygame.K_c:
                    action = "Hold"
                elif event.key == pygame.K_ESCAPE:
                    running = False

                # Send action to server
                if action:
                    try:
                        send_msg(game_sock, {"action": action})
                    except Exception as e:
                        print(f"Failed to send action: {e}")

        # Receive game state from server (use select to check if data is available)
        try:
            readable, _, _ = select.select([game_sock], [], [], 0)
            if readable:
                msg = recv_msg(game_sock)
                if msg:
                    data = json.loads(msg)
                    # Server sends: {"frame": N, "p1": {...}, "p2": {...}}
                    if 'p1' in data and 'p2' in data:
                        my_state = data['p1']
                        opponent_state = data['p2']
                        # Check if game is over (support both 'g' and 'gameOver')
                        if my_state.get('g', my_state.get('gameOver', False)):
                            print("Game Over! You lost.")
                        elif opponent_state.get('g', opponent_state.get('gameOver', False)):
                            print("Game Over! You won!")
                else:
                    # Connection closed
                    print("Server disconnected")
                    running = False
        except Exception as e:
            print(f"Error receiving game state: {e}")
            running = False

        # Draw the game
        if my_state:
            draw_tetris_game(screen, my_state, opponent_state, player_name)
        else:
            # Draw loading screen
            screen.fill((20, 20, 20))
            font = pygame.font.Font(None, 48)
            text = font.render("Waiting for game...", True, (255, 255, 255))
            text_rect = text.get_rect(center=(300, 350))
            screen.blit(text, text_rect)
            pygame.display.flip()

        clock.tick(30) 

    # Cleanup
    game_sock.close()
    pygame.quit()
    print("Game ended.")




# ========= Main =========
def main():
    state = 'login' #login, idle, gaming, room
    username = ""
    current_room_name = ""

    print(f"Connecting to {SERVER_IP}:{SERVER_PORT} ...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect((SERVER_IP, SERVER_PORT))
        print("✅ Connected to game server!\n")
    except Exception as e:
        print("❌ Connection failed:", e)
        return

    while True:
        cmd = input("Enter 'login' or 'register' (or 'quit'): ").strip().lower()
        if cmd == "quit":
            break
        if cmd not in ("login", "register"):
            print("Invalid command.\n")
            continue
        name = input("Username: ").strip()
        pw = input("Password: ").strip()
        if(login_or_register(sock, cmd, name, pw)):
            username = name
            state='idle'
            break

    while True:
        if(state == 'idle'):
            cmd = input("For game room, Enter 'curinvite' or'curroom' or 'create' or 'join' or 'spec' (or 'quit'): ").strip().lower()
            if cmd == 'quit':
                break
            if cmd not in ("curinvite", "curroom", "create", "join", "spec"):
                print("Invalid command.\n")
                continue
            roomname=""
            if cmd in ("create", "join", "spec"):
                roomname = input("Enter room name: ").strip()
            if lobby_op(sock, cmd, roomname):
                current_room_name = roomname
                state = 'room'

        elif state == 'room':
            print("Enter 'invite' or 'start' (or 'quit'): ", end='', flush=True)
            room_info = None
            while state == 'room':
                # Wait for either socket data or stdin input
                readable, _, _ = select.select([sock, sys.stdin], [], [], 0.5)

                # Check for incoming message from server
                if sock in readable:
                    msg = recv_msg(sock)
                    if msg:
                        try:
                            res = json.loads(msg)
                            print(f"\n\nReceived message: {json.dumps(res, indent=2)}")
                            if res.get('action') == 'start':
                                print("Game is starting! Launching GUI...")
                                room_info = res.get('data', {})
                                state = 'gaming'
                                break
                        except Exception as e:
                            print(f"⚠️  Failed to parse incoming message: {e}")
                        print("\nEnter 'invite' or 'start' (or 'quit'): ", end='', flush=True)

                # Check for user input
                if sys.stdin in readable:
                    cmd = sys.stdin.readline().strip().lower()
                    if cmd == 'quit':
                        state = 'idle'
                        break
                    if cmd not in ("invite", "start"):
                        print("Invalid command.\n")
                        print("Enter 'invite' or 'start' (or 'quit'): ", end='', flush=True)
                        continue
                    result = room_op(sock, cmd)
                    if result > 0:
                        # Wait for the start message with room info
                        msg = recv_msg(sock)
                        if msg:
                            try:
                                res = json.loads(msg)
                                if res.get('action') == 'start':
                                    room_info = res.get('data', {})
                                    state = 'gaming'
                                    break
                            except Exception as e:
                                print(f"⚠️  Failed to parse start message: {e}")
                    elif result == 0:
                        print("Nothing changed, still in the room")
                        print("Enter 'invite' or 'start' (or 'quit'): ", end='', flush=True)
                    else:
                        state = 'idle'
                        print("exited the room")
                        break

        elif(state == 'gaming'):
            # Use room info from the start message
            if room_info and 'id' in room_info:
                room_id = room_info.get('id', 0)
                print(f"Room ID: {room_id}, connecting to game server...")

                # Launch pygame GUI
                play_game(sock, room_id, username)
            else:
                print("Error: No room information received")

            # After game ends, go back to idle
            state = 'idle'
            current_room_name = ""
            room_info = None
            print("\nReturned to lobby.")



    print("Closing connection...")
    sock.close()
    print("Disconnected.")


if __name__ == "__main__":
    main()
