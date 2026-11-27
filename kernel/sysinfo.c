/* ===================== kernel/sysinfo.c (REPLACE whole file) ===================== */
#include "types.h"
#include "riscv.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "sysinfo.h"

#define offsetof(t, m) __builtin_offsetof(t, m)

/*
 * Scratch data used to work out "which processes reference which open file".
 * refmask[id] has bit s set if process-table slot s has at least one fd
 * pointing at ftable slot id. A process that dup()'d a file still sets the
 * bit only once, which gives the "distinct PIDs" the ABI asks for.
 * It lives in one kalloc'ed page because the kernel stack is only 4 KB.
 */
#define MASKWORDS ((NPROC + 63) / 64)
struct scratch {
  uint64 refmask[NFILE][MASKWORDS];
  int slotpid[NPROC];           /* pid of the process in each proc slot */
};
_Static_assert(sizeof(struct scratch) <= PGSIZE, "scratch must fit in one page");

/*
 * This repo's copyout() has the signature
 *   copyout(pagetable, psz, dstva, src, len)
 * where psz is the caller's process size (p->sz). Every call below passes
 * me->sz for it.
 *
 * int sysinfo(struct sysinfo *)
 *
 * 1. Zero the ENTIRE user struct (the ABI says unused slots must be
 *    initialized) by copying out a zero page repeatedly.
 * 2. Build one small entry at a time in the kernel and copyout() it to its
 *    offset in the user struct. The ~34 KB struct never exists in the kernel.
 * 3. copyout() is never called while a spinlock is held: procinfo() and
 *    filesnapshot() lock, copy, and unlock before returning.
 * The result is a best-effort snapshot, since other CPUs keep running.
 */
uint64
sys_sysinfo(void)
{
  uint64 addr, off, n;
  struct proc *me = myproc();
  struct scratch *sc = 0;
  char *zero = 0;
  struct sysinfo_proc pi;
  struct sysinfo_file fi;
  int nprocs = 0, nfiles = 0;
  int slot, i, k;

  argaddr(0, &addr);

  sc = kalloc();
  zero = kalloc();
  if(sc == 0 || zero == 0)
    goto bad;
  memset(sc, 0, sizeof(*sc));
  memset(zero, 0, PGSIZE);

  /* Step 1: zero the whole destination (also validates the user pointer). */
  for(off = 0; off < sizeof(struct sysinfo); off += PGSIZE){
    n = sizeof(struct sysinfo) - off;
    if(n > PGSIZE)
      n = PGSIZE;
    if(copyout(me->pagetable, me->sz, addr + off, zero, n) < 0)
      goto bad;
  }
  kfree(zero);
  zero = 0;

  /* Step 2: every process whose state != UNUSED (ZOMBIE included). */
  for(slot = 0; slot < NPROC; slot++){
    memset(&pi, 0, sizeof(pi));
    if(!procinfo(slot, &pi))
      continue;

    sc->slotpid[slot] = pi.pid;
    for(i = 0; i < pi.nfiles; i++){
      int id = pi.files[i].file_id;
      sc->refmask[id][slot / 64] |= 1ULL << (slot % 64);
    }

    if(copyout(me->pagetable, me->sz,
               addr + offsetof(struct sysinfo, procs) + nprocs * sizeof(pi),
               (char *)&pi, sizeof(pi)) < 0)
      goto bad;
    nprocs++;
  }

  /* Step 3: every open file (ftable slot with ref > 0). */
  for(slot = 0; slot < NFILE; slot++){
    memset(&fi, 0, sizeof(fi));
    if(!filesnapshot(slot, &fi))
      continue;

    for(k = 0; k < NPROC; k++){
      if(sc->refmask[slot][k / 64] & (1ULL << (k % 64)))
        fi.pids[fi.nproc++] = sc->slotpid[k];
    }

    if(copyout(me->pagetable, me->sz,
               addr + offsetof(struct sysinfo, files) + nfiles * sizeof(fi),
               (char *)&fi, sizeof(fi)) < 0)
      goto bad;
    nfiles++;
  }

  /* Counts last, so they match the entries actually written. */
  if(copyout(me->pagetable, me->sz,
             addr + offsetof(struct sysinfo, nfiles),
             (char *)&nfiles, sizeof(nfiles)) < 0)
    goto bad;
  if(copyout(me->pagetable, me->sz,
             addr + offsetof(struct sysinfo, nprocs),
             (char *)&nprocs, sizeof(nprocs)) < 0)
    goto bad;

  kfree(sc);
  return 0;

bad:
  if(sc)
    kfree(sc);
  if(zero)
    kfree(zero);
  return -1;
}