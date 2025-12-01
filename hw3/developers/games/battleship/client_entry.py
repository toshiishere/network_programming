import argparse
import socket
import sys

BOARD_SIZE_DEFAULT = 6


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


def print_board(board):
    header = "   " + " ".join(str(i) for i in range(len(board[0])))
    print(header)
    for r, row in enumerate(board):
        print(f"{r:2} " + " ".join(row))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", required=True, type=int)
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((args.host, args.port))

    my_index = None
    board_size = BOARD_SIZE_DEFAULT
    enemy_view = []

    try:
        while True:
            msg = recv_line(sock)
            if not msg:
                break
            if msg.startswith("welcome"):
                parts = msg.split()
                if len(parts) >= 2:
                    try:
                        my_index = int(parts[1])
                    except ValueError:
                        my_index = None
                print(f"Connected as player {my_index}")
            elif msg.startswith("start"):
                tokens = msg.split()
                for t in tokens:
                    if t.startswith("size="):
                        try:
                            board_size = int(t.split("=", 1)[1])
                        except ValueError:
                            board_size = BOARD_SIZE_DEFAULT
                enemy_view = [["." for _ in range(board_size)] for _ in range(board_size)]
                print(f"Game start. Board size {board_size}. Ships: {msg}")
            elif msg.startswith("turn"):
                parts = msg.split()
                if len(parts) >= 2:
                    turn = parts[1]
                    turn_txt = "your" if str(my_index) == turn else f"player {turn}'s"
                    print(f"Turn: {turn_txt} turn")
            elif msg.startswith("your_turn"):
                print_board(enemy_view)
                while True:
                    try:
                        coords = input("Enter fire coordinates (row col): ").strip().split()
                    except EOFError:
                        coords = []
                    if len(coords) != 2:
                        print("Enter two numbers, e.g., '2 3'")
                        continue
                    try:
                        r = int(coords[0])
                        c = int(coords[1])
                    except ValueError:
                        print("Numbers only.")
                        continue
                    send_line(sock, f"fire {r} {c}")
                    break
            elif msg.startswith("shot"):
                info = {}
                for part in msg.split()[1:]:
                    if "=" in part:
                        k, v = part.split("=", 1)
                        info[k] = v
                shooter = info.get("player")
                try:
                    row = int(info.get("row", -1))
                    col = int(info.get("col", -1))
                except ValueError:
                    row, col = -1, -1
                result = info.get("result", "")
                remaining = info.get("remaining", "")
                if shooter == str(my_index):
                    if 0 <= row < len(enemy_view) and 0 <= col < len(enemy_view):
                        enemy_view[row][col] = "X" if result in ("hit", "sunk") else "O"
                print(f"Shot by player {shooter} at ({row},{col}) -> {result}. Enemy ships remaining: {remaining}")
            elif msg.startswith("invalid"):
                print("Invalid move:", msg)
            elif msg.startswith("game_over"):
                parts = msg.split()
                winner = None
                for p in parts:
                    if p.startswith("winner="):
                        winner = p.split("=", 1)[1]
                if winner == str(my_index):
                    print("You win!")
                else:
                    print(f"Player {winner} wins.")
                break
            elif msg.startswith("error"):
                print("Server error:", msg)
                break
            else:
                print("Server:", msg)
    except ConnectionError:
        print("Disconnected.")
    finally:
        try:
            sock.close()
        except Exception:
            pass
    sys.exit()


if __name__ == "__main__":
    main()
