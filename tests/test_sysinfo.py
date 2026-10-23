#!/usr/bin/env python3
"""Boot real xv6 and run sysinfotest. Requires Linux/WSL and the xv6 toolchain."""

import argparse
import os
from pathlib import Path
import re
import selectors
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
CASES = (
    "baseline", "open-modes", "dup-close", "pipe", "fork-sharing",
    "zombie-wait", "scheduling", "fd-limit", "invalid-pointers",
    "repeated-snapshots", "concurrent-snapshots", "cli-output", "cli-usage",
)
MISSING_CASES = ("cli-usage", "cli-unavailable")


class Guest:
    def __init__(self, cpus, log, timeout):
        self.log = log
        self.timeout = timeout
        self.buffer = b""
        self.proc = subprocess.Popen(
            ["make", "--no-print-directory", "qemu", f"CPUS={cpus}"],
            cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, start_new_session=True,
        )
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.proc.stdout, selectors.EVENT_READ)

    def expect(self, marker):
        deadline = time.monotonic() + self.timeout
        while True:
            failure = re.search(rb"(?:panic:|SYSINFOTEST FAIL )[^\r\n]*[\r\n]", self.buffer)
            if failure:
                raise RuntimeError(failure.group().decode(errors="replace").strip())
            if marker in self.buffer:
                end = self.buffer.index(marker) + len(marker)
                result, self.buffer = self.buffer[:end], self.buffer[end:]
                return result
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"timed out waiting for {marker!r}")
            if not self.selector.select(min(remaining, 1)):
                if self.proc.poll() is not None:
                    raise RuntimeError("QEMU exited before the expected output")
                continue
            data = os.read(self.proc.stdout.fileno(), 65536)
            if not data:
                raise RuntimeError("QEMU closed its output before test completion")
            self.log.write(data)
            self.log.flush()
            sys.stdout.write(data.decode("utf-8", errors="replace"))
            sys.stdout.flush()
            self.buffer += data

    def close(self):
        # Stop the entire make/QEMU process group, including on timeout or Ctrl-C.
        try:
            os.killpg(self.proc.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(self.proc.pid, signal.SIGKILL)
            self.proc.wait(timeout=5)
        self.selector.close()
        self.proc.stdin.close()
        self.proc.stdout.close()


def run(cpus, timeout, log_dir, missing):
    log_path = log_dir / f"sysinfo-{'missing' if missing else 'full'}-{cpus}cpu.log"
    print(f"Running xv6 with {cpus} CPU(s); log: {log_path}", flush=True)
    with log_path.open("wb") as log:
        guest = Guest(cpus, log, timeout)
        try:
            guest.expect(b"$ ")
            command = b"sysinfotest --expect-unimplemented\n" if missing else b"sysinfotest\n"
            guest.proc.stdin.write(command)
            guest.proc.stdin.flush()
            cases = MISSING_CASES if missing else CASES
            summary = "MISSING-KERNEL PASS" if missing else "ALL PASS"
            marker = f"SYSINFOTEST {summary} ({len(cases)} cases)".encode()
            transcript = guest.expect(marker)
            # Require the shell prompt too: the test must actually exit.
            guest.expect(b"$ ")
            for case in cases:
                expected = f"SYSINFOTEST PASS {case}\r\n".encode()
                normalized = transcript.replace(b"\r\n", b"\n")
                if expected.replace(b"\r\n", b"\n") not in normalized:
                    raise RuntimeError(f"missing PASS result for {case}")
        finally:
            guest.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpus", type=int, nargs="+", default=[1, 3])
    parser.add_argument("--timeout", type=float, default=120,
                        help="seconds allowed for boot and for guest tests")
    parser.add_argument("--log-dir", type=Path, default=ROOT / "tests" / ".logs")
    parser.add_argument("--expect-unimplemented", action="store_true",
                        help="ONLY test usage/error handling before kernel integration")
    args = parser.parse_args()
    if os.name != "posix":
        parser.error("run this script inside Linux/WSL")
    if args.timeout <= 0 or any(cpu < 1 or cpu > 8 for cpu in args.cpus):
        parser.error("timeout must be positive; CPU counts must be in 1..8")
    args.log_dir.mkdir(parents=True, exist_ok=True)
    try:
        subprocess.run(["make", "kernel/kernel", "fs.img"], cwd=ROOT,
                       check=True, timeout=args.timeout)
        for cpus in args.cpus:
            run(cpus, args.timeout, args.log_dir, args.expect_unimplemented)
    except (OSError, RuntimeError, subprocess.SubprocessError, TimeoutError) as exc:
        print(f"sysinfo integration: FAIL: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("sysinfo integration: interrupted", file=sys.stderr)
        return 130
    if args.expect_unimplemented:
        print("PASS: missing-kernel error paths only; full integration remains pending.")
    else:
        print("PASS: all sysinfo integration cases on every requested CPU count.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
