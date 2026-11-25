#!/usr/bin/env python3
"""Boot real xv6 and verify vmprint and exec_vmprint. Requires Linux/WSL and the xv6 toolchain."""

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
    "normal-exec",
    "exec-vmprint-basic",
    "exec-vmprint-missing",
    "exec-vmprint-badargs",
    "cli-usage",
    "cli-missing",
    "cli-exec",
    "cli-multi-arg",
)

HEADER_RE = re.compile(r"^page table (0x[0-9a-f]{16})$")
ENTRY_RE = re.compile(
    r"^(\.\.|\.\. \.\.|\.\. \.\. \.\.)(\d+): pte (0x[0-9a-f]{16}) pa (0x[0-9a-f]{16})$"
)
DEPTH_BY_PREFIX = {
    "..": 0,
    ".. ..": 1,
    ".. .. ..": 2,
}


class Guest:
    def __init__(self, cpus, log, timeout):
        self.log = log
        self.timeout = timeout
        self.buffer = b""
        self.proc = subprocess.Popen(
            ["make", "--no-print-directory", "qemu", f"CPUS={cpus}"],
            cwd=ROOT,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.proc.stdout, selectors.EVENT_READ)

    def expect(self, marker):
        deadline = time.monotonic() + self.timeout
        while True:
            failure = re.search(rb"(?:panic:|VMPRINTTEST FAIL )[^\r\n]*[\r\n]", self.buffer)
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


def validate_page_table_block(text, context):
    lines = [line.rstrip("\r") for line in text.splitlines()]
    start = None
    for idx, line in enumerate(lines):
        if HEADER_RE.match(line):
            start = idx
            break
    if start is None:
        raise RuntimeError(f"{context}: missing 'page table 0x...' header")

    pt_lines = [lines[start]]
    for line in lines[start + 1 :]:
        if ENTRY_RE.match(line):
            pt_lines.append(line)
        else:
            break

    if len(pt_lines) < 2:
        raise RuntimeError(f"{context}: page table has no PTE entries")

    leaves = {}
    last_idx = [-1, -1, -1]
    current_path = [-1, -1]
    parent_is_nonleaf = [False, False]
    has_child = [False, False]

    for line in pt_lines[1:]:
        m = ENTRY_RE.match(line)
        if not m:
            raise RuntimeError(f"{context}: malformed PTE line {line!r}")
        prefix, idx_str, pte_str, pa_str = m.groups()
        depth = DEPTH_BY_PREFIX[prefix]
        idx = int(idx_str)
        pte = int(pte_str, 16)
        pa = int(pa_str, 16)

        if not (0 <= idx < 512):
            raise RuntimeError(f"{context}: PTE index out of range: {idx}")
        if (pte & 1) == 0:
            raise RuntimeError(f"{context}: printed non-valid PTE: {line!r}")
        if ((pte >> 10) << 12) != pa:
            raise RuntimeError(f"{context}: PA mismatch in {line!r}")

        if depth == 0:
            if has_child[0] is False and current_path[0] != -1:
                raise RuntimeError(f"{context}: depth-0 entry {current_path[0]} had no children")
            if idx <= last_idx[0]:
                raise RuntimeError(f"{context}: depth-0 indices not strictly increasing")
            if (pte & 0xE) != 0:
                raise RuntimeError(f"{context}: depth-0 entry should be non-leaf: {line!r}")
            last_idx[0] = idx
            last_idx[1] = -1
            last_idx[2] = -1
            current_path[0] = idx
            current_path[1] = -1
            parent_is_nonleaf[0] = True
            parent_is_nonleaf[1] = False
            has_child[0] = False
            has_child[1] = False
        elif depth == 1:
            if not parent_is_nonleaf[0]:
                raise RuntimeError(f"{context}: depth-1 entry without depth-0 parent: {line!r}")
            if idx <= last_idx[1]:
                raise RuntimeError(f"{context}: depth-1 indices not strictly increasing")
            if (pte & 0xE) != 0:
                raise RuntimeError(f"{context}: depth-1 entry should be non-leaf: {line!r}")
            has_child[0] = True
            last_idx[1] = idx
            last_idx[2] = -1
            current_path[1] = idx
            parent_is_nonleaf[1] = True
            has_child[1] = False
        else:  # depth == 2
            if not parent_is_nonleaf[1]:
                raise RuntimeError(f"{context}: depth-2 entry without depth-1 parent: {line!r}")
            if idx <= last_idx[2]:
                raise RuntimeError(f"{context}: depth-2 indices not strictly increasing")
            if (pte & 0xE) == 0:
                raise RuntimeError(f"{context}: depth-2 leaf missing R/W/X bits: {line!r}")
            has_child[1] = True
            last_idx[2] = idx
            leaves[(current_path[0], current_path[1], idx)] = (pte, pa)

    # Verify expected xv6 user address space leaf pages
    if (0, 0, 0) not in leaves:
        raise RuntimeError(f"{context}: missing user text page at (0, 0, 0)")
    if (leaves[(0, 0, 0)][0] & 0x1F) != 0x1B:
        raise RuntimeError(
            f"{context}: unexpected flags on text page (0,0,0): {hex(leaves[(0, 0, 0)][0] & 0x1F)}"
        )

    low_flags = [pte & 0x1F for (l2, l1, _), (pte, _) in leaves.items() if l2 == 0 and l1 == 0]
    if 0x07 not in low_flags:
        raise RuntimeError(f"{context}: missing user stack guard page (flags 0x07) in (0,0,*)")
    if 0x17 not in low_flags:
        raise RuntimeError(f"{context}: missing user stack/data page (flags 0x17) in (0,0,*)")

    if (255, 511, 510) not in leaves or (leaves[(255, 511, 510)][0] & 0x1F) != 0x07:
        raise RuntimeError(f"{context}: missing or invalid TRAPFRAME mapping at (255, 511, 510)")
    if (255, 511, 511) not in leaves or (leaves[(255, 511, 511)][0] & 0x1F) != 0x0B:
        raise RuntimeError(f"{context}: missing or invalid TRAMPOLINE mapping at (255, 511, 511)")

    return "\n".join(pt_lines), "\n".join(lines[start + len(pt_lines) :])


def extract_section(text, begin_marker, end_marker):
    if begin_marker not in text or end_marker not in text:
        raise RuntimeError(f"missing markers {begin_marker!r} .. {end_marker!r}")
    start = text.index(begin_marker) + len(begin_marker)
    end = text.index(end_marker, start)
    return text[start:end]


def run(cpus, timeout, log_dir):
    log_path = log_dir / f"vmprint-{cpus}cpu.log"
    print(f"Running xv6 with {cpus} CPU(s); log: {log_path}", flush=True)
    with log_path.open("wb") as log:
        guest = Guest(cpus, log, timeout)
        try:
            guest.expect(b"$ ")

            # 1. Normal exec in shell must NOT print a page table
            guest.proc.stdin.write(b"echo normal_shell_exec\n")
            guest.proc.stdin.flush()
            normal_out = guest.expect(b"$ ").decode("utf-8", errors="replace").replace("\r\n", "\n")
            if "normal_shell_exec" not in normal_out:
                raise RuntimeError("normal shell echo did not produce expected output")
            if "page table 0x" in normal_out:
                raise RuntimeError("normal shell exec unexpectedly printed a page table")

            # 2. vmprint CLI in shell must print a valid page table before program output
            guest.proc.stdin.write(b"vmprint echo hello_vmprint\n")
            guest.proc.stdin.flush()
            cli_out = guest.expect(b"$ ").decode("utf-8", errors="replace").replace("\r\n", "\n")
            _, after_pt = validate_page_table_block(cli_out, "shell vmprint echo")
            if "hello_vmprint" not in after_pt:
                raise RuntimeError("hello_vmprint missing after page table in shell vmprint echo")

            # 3. Run full vmprinttest suite
            guest.proc.stdin.write(b"vmprinttest\n")
            guest.proc.stdin.flush()
            marker = f"VMPRINTTEST ALL PASS ({len(CASES)} cases)".encode()
            transcript = guest.expect(marker).decode("utf-8", errors="replace").replace("\r\n", "\n")
            guest.expect(b"$ ")

            for case in CASES:
                if f"VMPRINTTEST PASS {case}\n" not in transcript:
                    raise RuntimeError(f"missing PASS result for {case}")

            normal_sec = extract_section(
                transcript, "VMPRINTTEST BEGIN normal-exec\n", "VMPRINTTEST END normal-exec\n"
            )
            if "normal_ok" not in normal_sec or "page table 0x" in normal_sec:
                raise RuntimeError("vmprinttest normal-exec failed output/no-page-table check")

            missing_sec = extract_section(
                transcript,
                "VMPRINTTEST BEGIN exec-vmprint-missing\n",
                "VMPRINTTEST END exec-vmprint-missing\n",
            )
            if "page table 0x" in missing_sec:
                raise RuntimeError("vmprinttest exec-vmprint-missing unexpectedly printed a page table")

            basic_sec = extract_section(
                transcript,
                "VMPRINTTEST BEGIN exec-vmprint-basic\n",
                "VMPRINTTEST END exec-vmprint-basic\n",
            )
            _, basic_after = validate_page_table_block(basic_sec, "vmprinttest exec-vmprint-basic")
            if "vmprint_ok arg2" not in basic_after:
                raise RuntimeError("vmprinttest exec-vmprint-basic missing expected echo output")
        finally:
            guest.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpus", type=int, nargs="+", default=[1, 3])
    parser.add_argument(
        "--timeout",
        type=float,
        default=120,
        help="seconds allowed for boot and for guest tests",
    )
    parser.add_argument("--log-dir", type=Path, default=ROOT / "tests" / ".logs")
    args = parser.parse_args()
    if os.name != "posix":
        parser.error("run this script inside Linux/WSL")
    if args.timeout <= 0 or any(cpu < 1 or cpu > 8 for cpu in args.cpus):
        parser.error("timeout must be positive; CPU counts must be in 1..8")
    args.log_dir.mkdir(parents=True, exist_ok=True)
    try:
        subprocess.run(
            ["make", "kernel/kernel", "fs.img"],
            cwd=ROOT,
            check=True,
            timeout=args.timeout,
        )
        for cpus in args.cpus:
            run(cpus, args.timeout, args.log_dir)
    except (OSError, RuntimeError, subprocess.SubprocessError, TimeoutError) as exc:
        print(f"vmprint integration: FAIL: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("vmprint integration: interrupted", file=sys.stderr)
        return 130
    print("PASS: all vmprint checks and integration cases passed on every requested CPU count.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
