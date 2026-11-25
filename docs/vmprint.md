# Print a Page Table (`vmprint`)

Branch: `feat/vmprint` (branched from `riscv`).

## Scope & Design Rationale

Implemented the **Print a page table** feature from Project 2 (`lab2.md`) without modifying the behavior of the existing `exec` system call or touching `kernel/proc.h`, `kernel/proc.c`, or `SYS_sysinfo` (24):

- **`vmprint(pagetable_t)` in `kernel/vm.c`** (prototyped in `kernel/defs.h`):
  - Prints the root page table pointer (`page table 0x...`) and recursively walks valid (`PTE_V`) entries across the 3-level Sv39 tree (`depth` 0, 1, 2).
  - Uses `printk` with `(void *)` casts for `%p` (required by `printk`'s printf-format attribute under `-Wall -Werror`) and prints each line in a single `printk` call with `..`, `.. ..`, `.. .. ..` prefixes.
  - Descends only when a valid PTE is a non-leaf directory entry (`(pte & (PTE_R | PTE_W | PTE_X)) == 0 && depth < 2`).
- **Non-intrusive `exec` trigger (`SYS_exec_vmprint = 25`)**:
  - `kernel/exec.c` factors `kexec` into `static int kexec_internal(char *path, char **argv, int print_pt)`, calling `vmprint(p->pagetable)` right before `return argc;` (after `proc_freepagetable(oldpagetable, oldsz)`) only when `print_pt != 0`.
  - `kexec()` passes `print_pt = 0` so normal `exec()` and `forkret()` are unaffected; `kexec_vmprint()` passes `print_pt = 1`.
  - `kernel/sysfile.c` shares argument parsing in `static uint64 exec_common(int print_pt)`, exposed via `sys_exec()` and `sys_exec_vmprint()`.
  - `kernel/syscall.h` and `kernel/syscall.c` register `SYS_exec_vmprint` (`25`) in both `syscalls[]` and `syscalls_name[]`.
- **User-space interface & tests**:
  - `user/user.h` and `user/usys.pl` expose `int exec_vmprint(const char *, char **);`.
  - `user/vmprint.c` provides the `vmprint <command> [args...]` CLI utility.
  - `user/vmprinttest.c` and `tests/test_vmprint.py` (with `make test-vmprint`) automate verification across 1 and 3 CPUs in QEMU.

## Files Added or Modified

| File | Role |
| --- | --- |
| `kernel/defs.h` | Prototypes for `vmprint()` and `kexec_vmprint()` |
| `kernel/vm.c` | `vmprint()` and `vmprint_walk()` |
| `kernel/exec.c` | `kexec_internal()`, `kexec()`, and `kexec_vmprint()` |
| `kernel/sysfile.c` | `exec_common()`, `sys_exec()`, and `sys_exec_vmprint()` |
| `kernel/syscall.h` | `SYS_exec_vmprint` (`25`) |
| `kernel/syscall.c` | Syscall table and name registration for `sys_exec_vmprint` |
| `user/user.h` | `exec_vmprint` prototype |
| `user/usys.pl` | `exec_vmprint` syscall stub |
| `user/vmprint.c` | User CLI (`vmprint command [args...]`) |
| `user/vmprinttest.c` | User-space integration test suite (8 cases) |
| `tests/test_vmprint.py` | Automated QEMU runner & PTE tree/permission validator |
| `Makefile` | Added `_vmprint`, `_vmprinttest`, and `test-vmprint` target |
| `docs/vmprint.md` | Feature summary, usage, verification, and leaf-page explanation |

## Captured Output & Leaf Page Explanation

Running `vmprint echo hello_vmprint` in QEMU (`CPUS=3`) produced:

```text
page table 0x0000000087f31000
..0: pte 0x0000000021fc7001 pa 0x0000000087f1c000
.. ..0: pte 0x0000000021fc7401 pa 0x0000000087f1d000
.. .. ..0: pte 0x0000000021fc801b pa 0x0000000087f20000
.. .. ..1: pte 0x0000000021fcc017 pa 0x0000000087f30000
.. .. ..2: pte 0x0000000021fcbc07 pa 0x0000000087f2f000
.. .. ..3: pte 0x0000000021fcb817 pa 0x0000000087f2e000
..255: pte 0x0000000021fc8c01 pa 0x0000000087f23000
.. ..511: pte 0x0000000021fc8801 pa 0x0000000087f22000
.. .. ..510: pte 0x0000000021fc9007 pa 0x0000000087f24000
.. .. ..511: pte 0x000000002000180b pa 0x0000000080006000
```

### Non-leaf Page-Table Directory Entries

- `..0` (`0x...001`) and `.. ..0` (`0x...401`): `PTE_V` only (`0x01`), pointing to lower-level page-table pages covering virtual addresses `0x0000000000`–`0x00001fffff`.
- `..255` (`0x...c01`) and `.. ..511` (`0x...801`): `PTE_V` only (`0x01`), pointing to the highest Sv39 page-table pages covering `0x3fffe00000`–`0x3fffffffff`.

### Leaf Pages (`depth = 2`)

| Path `(L2, L1, L0)` | Virtual Address | Raw Flags | Permission Bits | Logical Contents |
| --- | --- | --- | --- | --- |
| `(0, 0, 0)` | `0x0000000000` | `0x1b` | `PTE_V \| PTE_R \| PTE_X \| PTE_U` | User program code (`.text`) and read-only data (`.rodata`) from the first `ELF_PROG_LOAD` segment. Executable and readable by user space, non-writable. |
| `(0, 0, 1)` | `0x0000001000` | `0x17` | `PTE_V \| PTE_R \| PTE_W \| PTE_U` | User initialized data and zero-initialized data (`.data` / `.bss`) from the second `ELF_PROG_LOAD` segment (`user/user.ld` aligns `.data` to `0x1000`). Readable and writable by user space, non-executable. |
| `(0, 0, 2)` | `0x0000002000` | `0x07` | `PTE_V \| PTE_R \| PTE_W` (no `PTE_U`) | User stack guard page allocated in `kexec_internal()` and marked supervisor-only via `uvmclear()`. Any user stack overflow into this page triggers a user page fault instead of corrupting lower memory. |
| `(0, 0, 3)` | `0x0000003000` | `0x17` | `PTE_V \| PTE_R \| PTE_W \| PTE_U` | User stack page (`USERSTACK = 1`), holding initial `argv` strings, the `argv[]` pointer array, and runtime stack frames. |
| `(255, 511, 510)` | `0x3fffffe000` (`TRAPFRAME`) | `0x07` | `PTE_V \| PTE_R \| PTE_W` (no `PTE_U`) | Process `struct trapframe` page (`p->trapframe`), storing saved user registers and kernel trap state (`kernel_satp`, `kernel_sp`, `kernel_trap`, `epc`, etc.). Accessible only in supervisor mode. |
| `(255, 511, 511)` | `0x3ffffff000` (`TRAMPOLINE`) | `0x0b` | `PTE_V \| PTE_R \| PTE_X` (no `PTE_U`) | Shared `trampoline` code page (`uservec` / `userret` in `trampoline.S`), mapped at the same highest virtual address in both user and kernel page tables for trap entry/exit while switching `satp`. Executable only in supervisor mode. |

## Build & Verification

Run inside WSL/Linux from the repository root:

```sh
make kernel/kernel fs.img
python3 tests/test_vmprint.py
# Or using the Makefile target:
make test-vmprint
```

Manual demo in QEMU (`make qemu`):

```text
$ echo hello
$ vmprint echo hello
$ vmprinttest
```

Verified on `feat/vmprint`:
- `make kernel/kernel fs.img` compiled cleanly with zero warnings (`-Wall -Werror`).
- `python3 tests/test_vmprint.py` passed on both 1 CPU and 3 CPUs (`VMPRINTTEST ALL PASS (8 cases)`, plus shell `echo` and `vmprint echo hello_vmprint` checks).
