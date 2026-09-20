#!/usr/bin/env python3
"""Submit one integration message to the CS425 AWS SMTP sink."""

import argparse
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
CLIENT = ROOT / "build" / "release" / "myapp"
DEFAULT_SERVER = "ec2-54-148-3-55.us-west-2.compute.amazonaws.com"
DEFAULT_PORT = "2525"
DEFAULT_HELO = "onyx.boisestate.edu"
DEFAULT_FROM = "foo@sender.com"
DEFAULT_TO = "bar@reciever.com"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--from", dest="sender", default=DEFAULT_FROM)
    parser.add_argument("--to", dest="recipient", default=DEFAULT_TO)
    parser.add_argument("--subject", default="CS425 AWS integration test")
    parser.add_argument("--body", default="CS425 AWS integration test\n")
    parser.add_argument("--server", default=DEFAULT_SERVER)
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--helo", default=DEFAULT_HELO)
    args = parser.parse_args()

    if not CLIENT.exists():
        print("Build the client first with: make release", file=sys.stderr)
        return 2

    command = [
        str(CLIENT),
        "-f",
        args.sender,
        "-t",
        args.recipient,
        "-s",
        args.subject,
        "-b",
        args.body,
        "-p",
        args.port,
        "-H",
        args.helo,
        args.server,
    ]
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)

    if result.stdout:
        print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, file=sys.stderr, end="")
    if result.returncode == 0:
        print("PASS: AWS SMTP server accepted the message")
    else:
        print(f"FAIL: client exited with status {result.returncode}", file=sys.stderr)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
