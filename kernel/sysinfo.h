#ifndef XV6_SYSINFO_H
#define XV6_SYSINFO_H


// Shared user/kernel ABI. Include types.h before this header.
#include "param.h"

// Values match struct file.type and enum procstate, without exposing pointers
// or requiring user programs to include kernel-internal structures.
enum sysinfo_file_type {
  SYSINFO_FD_NONE,
  SYSINFO_FD_PIPE,
  SYSINFO_FD_INODE,
  SYSINFO_FD_DEVICE
};

enum sysinfo_proc_state {
  SYSINFO_UNUSED,
  SYSINFO_USED,
  SYSINFO_SLEEPING,
  SYSINFO_RUNNABLE,
  SYSINFO_RUNNING,
  SYSINFO_ZOMBIE
};

#define SYSINFO_READ  1
#define SYSINFO_WRITE 2

struct sysinfo_file {
  int id; // Index in ftable.file[]; valid only for this snapshot.
  int type;
  int mode; // SYSINFO_READ | SYSINFO_WRITE, not the original open() flags.
  int ref;  // Number of references, not number of distinct processes.
  int nproc;
  int pids[NPROC]; // Distinct referring PIDs; only [0, nproc) is valid.
};

struct sysinfo_fd {
  int fd;
  int file_id; // Refers to sysinfo_file.id, not its position in files[].
};

struct sysinfo_proc {
  int pid;
  int ppid; // Zero when there is no parent.
  int state;
  int nfiles;    // Number of descriptors, including duplicates.
  unsigned sched_count;  // executing time counter
  uint64 nsched; // Times dispatched by scheduler(), starting from zero.
  struct sysinfo_fd files[NOFILE]; // Only [0, nfiles) is valid.
};

struct sysinfo {
  int nfiles; // Number of ftable slots with ref > 0 (includes pipes/devices).
  int nprocs; // Number of processes whose state != UNUSED (includes ZOMBIE).
  struct sysinfo_file files[NFILE]; // Packed entries [0, nfiles).
  struct sysinfo_proc procs[NPROC]; // Packed entries [0, nprocs).
};

// sysinfo(out): 0 on success, -1 on failure/invalid destination.
// The kernel must initialize the entire output, including unused array slots.
// Do not put this large structure on a one-page user/kernel stack.
// See docs/sysinfo-user.md for snapshot consistency and integration details.

#endif
