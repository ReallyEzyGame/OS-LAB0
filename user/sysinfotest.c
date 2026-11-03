#include "kernel/types.h"
#include "kernel/sysinfo.h"
#include "kernel/fcntl.h"
#include "user/user.h"

// Keep large buffers out of xv6's one-page user stack.
static struct sysinfo info;
static char output[4096];
static char path[32];
static const char *testname = "startup";
static int child = -1;
static int passed;
static int created;

static void
check(int ok, const char *message)
{
  if (ok)
    return;
  fprintf(2, "SYSINFOTEST FAIL %s: %s\n", testname, message);
  if (child > 0) {
    kill(child);
    wait(0);
  }
  if (created && child != 0)
    unlink(path);
  exit(1);
}

static void
pass(void)
{
  printf("SYSINFOTEST PASS %s\n", testname);
  passed++;
}

static struct sysinfo_proc *
process(int pid)
{
  for (int i = 0; i < info.nprocs; i++)
    if (info.procs[i].pid == pid)
      return &info.procs[i];
  return 0;
}

static struct sysinfo_file *
file(int id)
{
  for (int i = 0; i < info.nfiles; i++)
    if (info.files[i].id == id)
      return &info.files[i];
  return 0;
}

static int
haspid(struct sysinfo_file *f, int pid)
{
  for (int i = 0; i < f->nproc; i++)
    if (f->pids[i] == pid)
      return 1;
  return 0;
}

static int
fileid(struct sysinfo_proc *p, int fd)
{
  check(p != 0, "process missing");
  for (int i = 0; i < p->nfiles; i++)
    if (p->files[i].fd == fd)
      return p->files[i].file_id;
  return -1;
}

static struct sysinfo_file *
ownfile(int fd)
{
  struct sysinfo_file *f = file(fileid(process(getpid()), fd));
  check(f != 0, "descriptor has no file entry");
  check(haspid(f, getpid()), "file does not list current PID");
  return f;
}

static void
snapshot(void)
{
  // Poison the buffer so a success return without filling it cannot pass.
  memset(&info, 0xa5, sizeof(info));
  check(sysinfo(&info) == 0, "sysinfo failed (kernel handler required)");
  check(info.nfiles >= 0 && info.nfiles <= NFILE, "file count out of bounds");
  check(info.nprocs > 0 && info.nprocs <= NPROC, "process count out of bounds");
  for (int i = 0; i < info.nfiles; i++) {
    struct sysinfo_file *f = &info.files[i];
    check(f->id >= 0 && f->id < NFILE, "invalid file ID");
    check(f->type >= SYSINFO_FD_NONE && f->type <= SYSINFO_FD_DEVICE,
          "invalid file type");
    check(f->mode >= 0 && f->mode <= (SYSINFO_READ | SYSINFO_WRITE),
          "invalid file mode");
    check(f->ref > 0, "file with zero references was counted");
    check(f->nproc >= 0 && f->nproc <= NPROC, "PID count out of bounds");
    for (int j = 0; j < i; j++)
      check(info.files[j].id != f->id, "duplicate file ID");
    for (int j = 0; j < f->nproc; j++) {
      check(f->pids[j] > 0, "invalid referring PID");
      for (int k = 0; k < j; k++)
        check(f->pids[k] != f->pids[j], "referring PID was not deduplicated");
    }
  }
  for (int i = 0; i < info.nprocs; i++) {
    struct sysinfo_proc *p = &info.procs[i];
    check(p->pid > 0 && p->ppid >= 0, "invalid PID or PPID");
    check(p->state > SYSINFO_UNUSED && p->state <= SYSINFO_ZOMBIE,
          "invalid state or UNUSED process was counted");
    check(p->nfiles >= 0 && p->nfiles <= NOFILE, "fd count out of bounds");
    for (int j = 0; j < i; j++)
      check(info.procs[j].pid != p->pid, "duplicate PID");
    for (int j = 0; j < p->nfiles; j++) {
      check(p->files[j].fd >= 0 && p->files[j].fd < NOFILE, "invalid fd");
      check(p->files[j].file_id >= 0 && p->files[j].file_id < NFILE,
            "invalid fd-to-file mapping");
      for (int k = 0; k < j; k++)
        check(p->files[k].fd != p->files[j].fd, "duplicate fd");
    }
  }
  check(process(getpid()) != 0, "calling process missing");
}

static void
reap(void)
{
  int status = -1;
  int pid = wait(&status);
  int expected = child;
  child = -1;
  check(pid == expected && status == 0,
        "child failed or wait returned wrong PID");
}

static void
waitstate(int pid, int state)
{
  // Poll an observed state with a deadline; do not assume a fixed sleep suffices.
  int start = uptime();
  while (uptime() - start < 100) {
    snapshot();
    struct sysinfo_proc *p = process(pid);
    if (p && p->state == state)
      return;
    pause(1);
  }
  check(0, "timed out waiting for process state");
}

static void
baseline(void)
{
  testname = "baseline";
  snapshot();
  struct sysinfo_proc *p = process(getpid());
  check(p->state == SYSINFO_RUNNING, "caller is not RUNNING");
  check(p->ppid > 0, "caller has no parent");
  check(p->nsched > 0, "caller was never scheduled");
  check(p->nfiles == 3, "run from shell with stdin/stdout/stderr only");
  for (int fd = 0; fd < 3; fd++)
    check(ownfile(fd)->type == SYSINFO_FD_DEVICE,
          "expected console descriptor");
  pass();
}

static void
openmodes(void)
{
  testname = "open-modes";
  snapshot();
  int before = info.nfiles;
  int rd = open(path, O_RDONLY);
  int wr = open(path, O_WRONLY);
  int rw = open(path, O_RDWR);
  check(rd >= 0 && wr >= 0 && rw >= 0, "open failed");
  snapshot();
  check(info.nfiles == before + 3, "open did not allocate three file objects");
  check(ownfile(rd)->mode == SYSINFO_READ, "wrong read-only mode");
  check(ownfile(wr)->mode == SYSINFO_WRITE, "wrong write-only mode");
  check(ownfile(rw)->mode == (SYSINFO_READ | SYSINFO_WRITE),
        "wrong read/write mode");
  check(ownfile(rd)->type == SYSINFO_FD_INODE, "wrong regular-file type");
  check(ownfile(rd)->id != ownfile(wr)->id &&
          ownfile(wr)->id != ownfile(rw)->id &&
          ownfile(rd)->id != ownfile(rw)->id,
        "separate opens of one inode share a file object");
  check(ownfile(rd)->ref == 1 && ownfile(wr)->ref == 1 && ownfile(rw)->ref == 1,
        "new file reference count is not one");
  close(rd);
  close(wr);
  close(rw);
  snapshot();
  check(info.nfiles == before, "close leaked file objects");
  pass();
}

static void
dupclose(void)
{
  testname = "dup-close";
  int fd = open(path, O_RDONLY);
  check(fd >= 0, "open failed");
  snapshot();
  int count = info.nfiles;
  int id = ownfile(fd)->id;
  int other = dup(fd);
  check(other >= 0, "dup failed");
  snapshot();
  check(info.nfiles == count, "dup allocated a file object");
  check(ownfile(other)->id == id && ownfile(fd)->ref == 2,
        "dup did not share file");
  check(ownfile(fd)->nproc == 1, "duplicate fd counted as a second process");
  close(fd);
  snapshot();
  check(fileid(process(getpid()), fd) == -1, "closed fd still present");
  check(ownfile(other)->id == id && ownfile(other)->ref == 1,
        "close ref is wrong");
  close(other);
  snapshot();
  check(file(id) == 0 && info.nfiles == count - 1,
        "last close did not free object");
  pass();
}

static void
pipetest(void)
{
  testname = "pipe";
  int fds[2];
  snapshot();
  int before = info.nfiles;
  check(pipe(fds) == 0, "pipe failed");
  snapshot();
  check(info.nfiles == before + 2, "pipe endpoints not counted separately");
  check(ownfile(fds[0])->type == SYSINFO_FD_PIPE &&
          ownfile(fds[1])->type == SYSINFO_FD_PIPE,
        "wrong pipe type");
  check(ownfile(fds[0])->mode == SYSINFO_READ &&
          ownfile(fds[1])->mode == SYSINFO_WRITE,
        "wrong pipe modes");
  check(ownfile(fds[0])->id != ownfile(fds[1])->id, "pipe endpoints share ID");
  close(fds[0]);
  close(fds[1]);
  snapshot();
  check(info.nfiles == before, "pipe leaked file objects");
  pass();
}

static void
forktest(void)
{
  testname = "fork-sharing";
  int gate[2];
  int fd = open(path, O_RDONLY);
  check(fd >= 0 && pipe(gate) == 0, "setup failed");
  snapshot();
  int before = info.nprocs;
  int id = ownfile(fd)->id;
  child = fork();
  check(child >= 0, "fork failed");
  if (child == 0) {
    char token;
    close(gate[1]);
    if (read(gate[0], &token, 1) != 1)
      exit(2);
    close(gate[0]);
    close(fd);
    exit(0);
  }
  close(gate[0]);
  waitstate(child, SYSINFO_SLEEPING);
  struct sysinfo_proc *p = process(child);
  check(info.nprocs == before + 1, "child not counted");
  check(p->ppid == getpid(), "wrong parent PID");
  check(fileid(p, fd) == id, "child did not inherit file ID");
  check(ownfile(fd)->ref == 2 && ownfile(fd)->nproc == 2 &&
          haspid(ownfile(fd), child) && haspid(ownfile(fd), getpid()),
        "shared file references/PIDs are wrong");
  pass();

  testname = "zombie-wait";
  check(write(gate[1], "x", 1) == 1, "could not release child");
  close(gate[1]);
  waitstate(child, SYSINFO_ZOMBIE);
  check(info.nprocs == before + 1, "ZOMBIE not counted");
  check(process(child)->nfiles == 0, "ZOMBIE still has open files");
  check(ownfile(fd)->ref == 1 && !haspid(ownfile(fd), child),
        "exited child still references file");
  int pid = child;
  reap();
  snapshot();
  check(process(pid) == 0 && info.nprocs == before,
        "wait did not remove child");
  close(fd);
  pass();
}

static void
scheduling(void)
{
  testname = "scheduling";
  snapshot();
  uint64 before = process(getpid())->nsched;
  for (int i = 0; i < 3; i++) {
    pause(2);
    snapshot();
    uint64 now = process(getpid())->nsched;
    check(now > before, "dispatch counter did not increase after blocking");
    before = now;
  }
  pass();
}

static void
fdlimit(void)
{
  testname = "fd-limit";
  int fds[NOFILE];
  snapshot();
  int before = info.nfiles;
  int capacity = NOFILE - process(getpid())->nfiles;
  for (int i = 0; i < capacity; i++) {
    fds[i] = open(path, O_RDONLY);
    check(fds[i] >= 0, "open failed before descriptor limit");
  }
  check(open(path, O_RDONLY) == -1, "open succeeded beyond descriptor limit");
  snapshot();
  check(process(getpid())->nfiles == NOFILE, "full fd table was truncated");
  check(info.nfiles == before + capacity, "failed open leaked file object");
  for (int i = 0; i < capacity; i++)
    check(ownfile(fds[i])->ref == 1, "full table has incorrect references");
  for (int i = 0; i < capacity; i++)
    close(fds[i]);
  snapshot();
  check(info.nfiles == before, "full table cleanup leaked files");
  pass();
}

static void
badpointers(void)
{
  testname = "invalid-pointers";
  check(sysinfo((struct sysinfo *)0) == -1, "NULL destination accepted");
  check(sysinfo((struct sysinfo *)~(uint64)0) == -1,
        "invalid high address accepted");
  // sbrk(0) is the first byte beyond the process's valid address range.
  char *end = sbrk(0);
  check(end != SBRK_ERROR, "sbrk failed");
  check(sysinfo((struct sysinfo *)end) == -1,
        "out-of-range destination accepted");
  // A valid starting address is insufficient if the entire output cannot fit.
  char *small = sbrk(16);
  check(small != SBRK_ERROR, "sbrk failed");
  check(sysinfo((struct sysinfo *)small) == -1, "short output buffer accepted");
  check(sbrk(-16) != SBRK_ERROR, "could not restore heap");
  snapshot();
  pass();
}

static void
repeated(void)
{
  testname = "repeated-snapshots";
  snapshot();
  int files = info.nfiles;
  int procs = info.nprocs;
  for (int i = 0; i < 50; i++) {
    snapshot();
    check(info.nfiles == files && info.nprocs == procs,
          "sysinfo leaked resources");
  }
  pass();
}

static void
concurrent(void)
{
  testname = "concurrent-snapshots";
  int ready[2];
  check(pipe(ready) == 0, "pipe failed");
  child = fork();
  check(child >= 0, "fork failed");
  if (child == 0) {
    close(ready[0]);
    if (write(ready[1], "x", 1) != 1)
      exit(2);
    close(ready[1]);
    for (int i = 0; i < 100; i++) {
      int fd = open(path, O_RDONLY);
      if (fd < 0)
        exit(2);
      int other = dup(fd);
      if (other < 0)
        exit(2);
      // Both callers have private output buffers after fork().
      snapshot();
      close(other);
      close(fd);
    }
    exit(0);
  }
  close(ready[1]);
  char token;
  check(read(ready[0], &token, 1) == 1, "child did not start");
  close(ready[0]);
  for (int i = 0; i < 100; i++)
    snapshot();
  reap();
  snapshot();
  pass();
}

static int
contains(const char *s, const char *needle)
{
  for (; *s; s++) {
    int i = 0;
    while (needle[i] && s[i] == needle[i])
      i++;
    if (!needle[i])
      return 1;
  }
  return 0;
}

static void
cli(int mode)
{
  // 0: normal output, 1: bad arguments, 2: expected missing kernel handler.
  testname = mode == 1   ? "cli-usage"
             : mode == 2 ? "cli-unavailable"
                         : "cli-output";
  int channel[2];
  check(pipe(channel) == 0, "pipe failed");
  child = fork();
  check(child >= 0, "fork failed");
  if (child == 0) {
    close(channel[0]);
    close(1);
    if (dup(channel[1]) != 1)
      exit(2);
    close(2);
    if (dup(channel[1]) != 2)
      exit(2);
    close(channel[1]);
    char *argv[] = {"sysinfo", mode == 1 ? "unexpected" : 0, 0};
    exec(argv[0], argv);
    exit(2);
  }
  close(channel[1]);
  int used = 0, n;
  char chunk[128];
  // Always drain the pipe, even if only its prefix fits in output[].
  while ((n = read(channel[0], chunk, sizeof(chunk))) > 0)
    for (int i = 0; i < n && used < sizeof(output) - 1; i++)
      output[used++] = chunk[i];
  output[used] = 0;
  check(n == 0, "reading command output failed");
  close(channel[0]);
  int status = -1;
  int pid = wait(&status);
  int expected = child;
  child = -1;
  check(pid == expected && status == (mode ? 1 : 0),
        "wrong command exit status");
  if (mode == 1) {
    check(contains(output, "Usage: sysinfo"), "missing usage message");
  } else if (mode == 2) {
    check(contains(output, "sysinfo: syscall failed or is not implemented"),
          "missing syscall failure message");
  } else {
    check(contains(output, "FILES ") &&
            contains(output, "ID TYPE MODE REF PIDS"),
          "missing file table output");
    check(contains(output, "PROCESSES ") &&
            contains(output, "PID STATE PPID NSCHED FDS(fd:file-id)"),
          "missing process table output");
    check(contains(output, "DEVICE") && contains(output, "PIPE") &&
            contains(output, "RUNNING"),
          "missing decoded types/states");
  }
  pass();
}

int
main(int argc, char **argv)
{
  if (argc == 2 && strcmp(argv[1], "--expect-unimplemented") == 0) {
    cli(1);
    cli(2);
    printf("SYSINFOTEST MISSING-KERNEL PASS (%d cases)\n", passed);
    exit(0);
  }
  if (argc != 1) {
    fprintf(2, "Usage: sysinfotest [--expect-unimplemented]\n");
    exit(1);
  }
  // A per-process name avoids clobbering a fixed user file.
  strcpy(path, "si-");
  int n = strlen(path);
  int pid = getpid();
  do {
    path[n++] = '0' + pid % 10;
    pid /= 10;
  } while (pid);
  path[n] = 0;

  // Missing syscall must be a failure, never a skip or a fabricated pass.
  baseline();
  int fd = open(path, O_RDONLY);
  if (fd >= 0) {
    close(fd);
    check(0,
          "test filename already exists; remove it before running this suite");
  }
  fd = open(path, O_CREATE | O_RDWR);
  check(fd >= 0, "could not create test file");
  created = 1;
  close(fd);
  openmodes();
  dupclose();
  pipetest();
  forktest();
  scheduling();
  fdlimit();
  badpointers();
  repeated();
  concurrent();
  cli(0);
  cli(1);
  check(unlink(path) == 0, "could not remove test file");
  printf("SYSINFOTEST ALL PASS (%d cases)\n", passed);
  exit(0);
}
