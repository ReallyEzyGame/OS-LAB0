// Host-only fixture for testing the real user/sysinfo.c presentation code.
// This is NOT a kernel implementation and is never linked into xv6.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/types.h"
#include "kernel/sysinfo.h"

void
xv6_printf(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}

void
xv6_fprintf(int fd, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  vfprintf(fd == 2 ? stderr : stdout, fmt, args);
  va_end(args);
}

void
xv6_exit(int status)
{
  exit(status);
}

int
sysinfo(struct sysinfo *out)
{
  const char *fixture = getenv("SYSINFO_FIXTURE");
  if (!fixture || strcmp(fixture, "failure") == 0)
    return -1;
  memset(out, 0, sizeof(*out));
  out->nfiles = 1;
  out->nprocs = 2;
  out->files[0] = (struct sysinfo_file){
    .id = 7,
    .type = SYSINFO_FD_INODE,
    .mode = SYSINFO_READ | SYSINFO_WRITE,
    .ref = 3,
    .nproc = 2,
    .pids = {10, 11},
  };
  out->procs[0] = (struct sysinfo_proc){
    .pid = 10,
    .ppid = 1,
    .state = SYSINFO_RUNNING,
    .nfiles = 2,
    .nsched = 5000000000UL,
    .files = {{3, 7}, {4, 7}},
  };
  out->procs[1] = (struct sysinfo_proc){
    .pid = 11,
    .ppid = 10,
    .state = SYSINFO_SLEEPING,
    .nfiles = 1,
    .nsched = 2,
    .files = {{3, 7}},
  };

  if (strcmp(fixture, "negative-files") == 0)
    out->nfiles = -1;
  else if (strcmp(fixture, "overflow-files") == 0)
    out->nfiles = NFILE + 1;
  else if (strcmp(fixture, "negative-procs") == 0)
    out->nprocs = -1;
  else if (strcmp(fixture, "overflow-procs") == 0)
    out->nprocs = NPROC + 1;
  else if (strcmp(fixture, "negative-pids") == 0)
    out->files[0].nproc = -1;
  else if (strcmp(fixture, "overflow-pids") == 0)
    out->files[0].nproc = NPROC + 1;
  else if (strcmp(fixture, "negative-fds") == 0)
    out->procs[0].nfiles = -1;
  else if (strcmp(fixture, "overflow-fds") == 0)
    out->procs[0].nfiles = NOFILE + 1;
  else if (strcmp(fixture, "empty") == 0)
    memset(out, 0, sizeof(*out));
  else if (strcmp(fixture, "unknown") == 0) {
    out->files[0].type = 999;
    out->procs[0].state = 999;
  } else if (strcmp(fixture, "full") == 0) {
    out->nfiles = NFILE;
    out->nprocs = NPROC;
    for (int i = 0; i < NFILE; i++) {
      out->files[i].id = i;
      out->files[i].type = SYSINFO_FD_DEVICE;
      out->files[i].mode = i % 4;
      out->files[i].ref = NPROC;
      out->files[i].nproc = NPROC;
      for (int j = 0; j < NPROC; j++)
        out->files[i].pids[j] = j + 1;
    }
    for (int i = 0; i < NPROC; i++) {
      out->procs[i].pid = i + 1;
      out->procs[i].ppid = 0;
      out->procs[i].state = SYSINFO_RUNNABLE;
      out->procs[i].nfiles = NOFILE;
      out->procs[i].nsched = 5000000000UL;
      for (int j = 0; j < NOFILE; j++)
        out->procs[i].files[j] = (struct sysinfo_fd){j, j};
    }
  }
  return 0;
}
