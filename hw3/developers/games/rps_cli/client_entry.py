import argparse
import socket
import sys

CHOICES = ("rock", "paper", "scissors")


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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", required=True, type=int)
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    # retry connects briefly to handle slight server startup lag
    for _ in range(10):
        try:
            sock.connect((args.host, args.port))
            break
        except ConnectionRefusedError:
            import time
            time.sleep(0.3)
    else:
        print("Unable to connect to game server.")
        sys.exit(1)

    try:
        while True:
            msg = recv_line(sock)
            if msg.startswith("welcome"):
                print(msg)
            elif msg.startswith("start"):
                print(msg)
            elif msg.startswith("choose"):
                choice = None
                while choice not in CHOICES:
                    try:
                        choice = input(f"Choose {', '.join(CHOICES)}: ").strip().lower()
                    except EOFError:
                        choice = "rock"
                send_line(sock, choice)
            elif msg.startswith("result"):
                print(msg)
                break
            else:
                print(msg)
    except ConnectionError:
        print("Disconnected")
    finally:
        try:
            sock.close()
        except Exception:
            pass
    sys.exit()


if __name__ == "__main__":
    main()
