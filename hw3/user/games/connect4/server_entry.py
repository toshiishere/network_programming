import argparse
import socket

ROWS, COLS = 6, 7


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


def serialize(board):
    return "".join(str(cell) for row in board for cell in row)


def drop_disc(board, col, player):
    if col < 0 or col >= COLS:
        return False
    for r in range(ROWS - 1, -1, -1):
        if board[r][col] == 0:
            board[r][col] = player
            return True
    return False


def check_win(board, player):
    # horizontal
    for r in range(ROWS):
        for c in range(COLS - 3):
            if all(board[r][c + i] == player for i in range(4)):
                return True
    # vertical
    for c in range(COLS):
        for r in range(ROWS - 3):
            if all(board[r + i][c] == player for i in range(4)):
                return True
    # diag down-right
    for r in range(ROWS - 3):
        for c in range(COLS - 3):
            if all(board[r + i][c + i] == player for i in range(4)):
                return True
    # diag up-right
    for r in range(3, ROWS):
        for c in range(COLS - 3):
            if all(board[r - i][c + i] == player for i in range(4)):
                return True
    return False


def board_full(board):
    return all(cell != 0 for row in board for cell in row)


def broadcast(clients, text):
    for c in clients:
        try:
            send_line(c, text)
        except Exception:
            pass


def broadcast_state(clients, board, current):
    state = serialize(board)
    broadcast(clients, f"state {current} {state}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", required=True, type=int)
    parser.add_argument("--room", required=False)
    args = parser.parse_args()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind((args.host, args.port))
        s.listen()
        print(f"[CONNECT4] listening on {args.host}:{args.port}")

        clients = []
        while len(clients) < 2:
            conn, addr = s.accept()
            print(f"[CONNECT4] client connected {addr}")
            clients.append(conn)
            send_line(conn, f"role {len(clients)}")

        board = [[0 for _ in range(COLS)] for _ in range(ROWS)]
        turn = 1
        broadcast_state(clients, board, turn)

        try:
            while True:
                current_idx = turn - 1
                current_sock = clients[current_idx]
                send_line(current_sock, "your_turn")
                msg = recv_line(current_sock)
                if not msg.startswith("move"):
                    send_line(current_sock, "invalid bad_command")
                    continue
                try:
                    _, col_s = msg.split()
                    col = int(col_s)
                except Exception:
                    send_line(current_sock, "invalid invalid_move")
                    continue
                if not drop_disc(board, col, turn):
                    send_line(current_sock, "invalid column_full")
                    continue

                print(f"[CONNECT4] player {turn} dropped in col {col}")
                broadcast_state(clients, board, 3 - turn)

                if check_win(board, turn):
                    broadcast(clients, f"game_over winner {turn}")
                    break
                if board_full(board):
                    broadcast(clients, "game_over draw")
                    break

                turn = 3 - turn  # switch 1<->2
        finally:
            for c in clients:
                try:
                    c.close()
                except Exception:
                    pass


if __name__ == "__main__":
    main()
