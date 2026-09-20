#!/usr/bin/env python3
"""Run a local SMTP integration test against the release client."""

import socket
import subprocess
import sys
import threading
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
CLIENT = ROOT / "build" / "release" / "myapp"


def read_line(connection):
    data = bytearray()
    while not data.endswith(b"\r\n"):
        chunk = connection.recv(1)
        if not chunk:
            raise RuntimeError("client closed the connection unexpectedly")
        data.extend(chunk)
    return bytes(data)


def fake_smtp_server(server_socket, failure):
    try:
        connection, _ = server_socket.accept()
        with connection:
            connection.sendall(b"220-test server\r\n220 ready\r\n")

            expected_commands = [
                b"HELO test-client\r\n",
                b"MAIL FROM:<sender@example.com>\r\n",
                b"RCPT TO:<recipient@example.com>\r\n",
                b"DATA\r\n",
            ]
            responses = [
                b"250-test server\r\n250 hello\r\n",
                b"250 sender accepted\r\n",
                b"250 recipient accepted\r\n",
                b"354 send message\r\n",
            ]

            for expected, response in zip(expected_commands, responses):
                actual = read_line(connection)
                if actual != expected:
                    raise AssertionError(
                        f"expected {expected!r}, received {actual!r}"
                    )
                connection.sendall(response)

            data = bytearray()
            while not data.endswith(b"\r\n.\r\n"):
                chunk = connection.recv(1)
                if not chunk:
                    raise RuntimeError("client closed during DATA")
                data.extend(chunk)

            expected_payload = (
                b"From: <sender@example.com>\r\n"
                b"To: <recipient@example.com>\r\n"
                b"Subject: integration test\r\n"
                b"\r\n"
                b"..first line\r\n"
                b"second line\r\n"
                b".\r\n"
            )
            if bytes(data) != expected_payload:
                raise AssertionError(
                    f"unexpected DATA payload: {bytes(data)!r}"
                )
            connection.sendall(b"250 message queued\r\n")

            actual = read_line(connection)
            if actual != b"QUIT\r\n":
                raise AssertionError(f"expected QUIT, received {actual!r}")
            connection.sendall(b"221 closing\r\n")
    except BaseException as error:  # Pass server-thread failures to main.
        failure.append(error)
    finally:
        server_socket.close()


def main():
    if not CLIENT.exists():
        print("Build the client first with: make release", file=sys.stderr)
        return 2

    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_socket.bind(("127.0.0.1", 0))
    server_socket.listen(1)
    port = server_socket.getsockname()[1]
    failure = []
    server_thread = threading.Thread(
        target=fake_smtp_server, args=(server_socket, failure), daemon=True
    )
    server_thread.start()

    command = [
        str(CLIENT),
        "-f",
        "sender@example.com",
        "-t",
        "recipient@example.com",
        "-s",
        "integration test",
        "-b",
        ".first line\nsecond line",
        "-p",
        str(port),
        "-H",
        "test-client",
        "127.0.0.1",
    ]
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    server_thread.join(timeout=5)

    if server_thread.is_alive():
        print("FAIL: fake SMTP server did not finish", file=sys.stderr)
        return 1
    if failure:
        print(f"FAIL: fake SMTP server: {failure[0]}", file=sys.stderr)
        return 1
    if result.returncode != 0:
        print(f"FAIL: client exited with {result.returncode}", file=sys.stderr)
        if result.stderr:
            print(result.stderr, file=sys.stderr, end="")
        return 1

    print("PASS: SMTP session, multiline replies, CRLF, and dot-stuffing")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())