#!/usr/bin/python3
import socket
import select
import sys

SERVER_HOST = "127.0.0.1"
SERVER_PORT = 45632

def main():
    # Create a TCP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((SERVER_HOST, SERVER_PORT))
    print(f"Connected to {SERVER_HOST}:{SERVER_PORT}")
    print("Type messages and press Enter. Ctrl+C to quit.\n")

    sock.setblocking(False)

    while True:
        # Monitor both stdin and socket for input
        read_sockets, _, _ = select.select([sys.stdin, sock], [], [])

        for s in read_sockets:
            if s == sock:
                # Data from server
                data = s.recv(4096)
                if not data:
                    print("Server closed the connection.")
                    return
                print(data.decode(), end="")
            else:
                # Data from user (stdin)
                msg = sys.stdin.readline()
                if msg.strip().lower() == "/quit":
                    print("Closing connection.")
                    sock.close()
                    return
                sock.sendall(msg.encode())

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nYou Decided to be Disconnected.")
