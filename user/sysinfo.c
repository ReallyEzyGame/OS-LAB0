#include "kernel/types.h"
#include "kernel/sysinfo.h"
#include "user/user.h"

// The complete snapshot is much larger than xv6's one-page user stack.
static struct sysinfo info;

static const char *
filetype(int type)
{
  switch (type) {
  case SYSINFO_FD_NONE:
    return "NONE";
  case SYSINFO_FD_PIPE:
    return "PIPE";
  case SYSINFO_FD_INODE:
    return "INODE";
  case SYSINFO_FD_DEVICE:
    return "DEVICE";
  default:
    return "UNKNOWN";
  }
}

static const char *
procstate(int state)
{
  switch (state) {
  case SYSINFO_UNUSED:
    return "UNUSED";
  case SYSINFO_USED:
    return "USED";
  case SYSINFO_SLEEPING:
    return "SLEEPING";
  case SYSINFO_RUNNABLE:
    return "RUNNABLE";
  case SYSINFO_RUNNING:
    return "RUNNING";
  case SYSINFO_ZOMBIE:
    return "ZOMBIE";
  default:
    return "UNKNOWN";
  }
}

int
main(int argc, char **argv)
{
  if (argc != 1) {
    fprintf(2, "Usage: sysinfo\n");
    exit(1);
  }
  if (sysinfo(&info) != 0) {
    fprintf(2, "sysinfo: syscall failed or is not implemented\n");
    exit(1);
  }

  // Check every array bound before printing any rows.
  int valid = info.nfiles >= 0 && info.nfiles <= NFILE && info.nprocs >= 0 &&
              info.nprocs <= NPROC;
  if (valid) {
    for (int i = 0; i < info.nfiles; i++)
      if (info.files[i].nproc < 0 || info.files[i].nproc > NPROC)
        valid = 0;
    for (int i = 0; i < info.nprocs; i++)
      if (info.procs[i].nfiles < 0 || info.procs[i].nfiles > NOFILE)
        valid = 0;
  }
  if (!valid) {
    fprintf(2, "sysinfo: invalid snapshot bounds\n");
    exit(1);
  }

  printf("FILES %d\n", info.nfiles);
  printf("ID TYPE MODE REF PIDS\n");
  for (int i = 0; i < info.nfiles; i++) {
    struct sysinfo_file *f = &info.files[i];
    printf("%d %s %c%c %d", f->id, filetype(f->type),
           f->mode & SYSINFO_READ ? 'r' : '-',
           f->mode & SYSINFO_WRITE ? 'w' : '-', f->ref);
    for (int j = 0; j < f->nproc; j++)
      printf(" %d", f->pids[j]);
    printf("\n");
  }

  printf("PROCESSES %d\n", info.nprocs);
  printf("PID STATE PPID NSCHED FDS(fd:file-id)\n");
  for (int i = 0; i < info.nprocs; i++) {
    struct sysinfo_proc *p = &info.procs[i];
    printf("%d %s %d %lu", p->pid, procstate(p->state), p->ppid, p->nsched);
    for (int j = 0; j < p->nfiles; j++)
      printf(" %d:%d", p->files[j].fd, p->files[j].file_id);
    printf("\n");
  }
  exit(0);
}
