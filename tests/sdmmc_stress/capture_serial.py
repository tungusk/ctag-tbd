#!/usr/bin/env python3
import argparse
import signal
import sys
import time

import serial


stop_requested = False


def request_stop(_signum, _frame):
    global stop_requested
    stop_requested = True


def main():
    parser = argparse.ArgumentParser(description="Timed serial logger for SDMMC stress tests")
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--duration", type=float, required=True, help="Capture duration in seconds")
    args = parser.parse_args()

    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)

    deadline = time.monotonic() + args.duration
    with serial.Serial(args.port, args.baud, timeout=0.2) as ser:
        ser.dtr = False
        ser.rts = False
        while not stop_requested and time.monotonic() < deadline:
            data = ser.read(4096)
            if data:
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
