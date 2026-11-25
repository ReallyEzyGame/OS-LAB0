#include "kernel/types.h"
#include "kernel/riscv.h"
#include "user/user.h"

void
pgaccess_test(void)
{
  char *buf;
  unsigned int abits;

  printf("pgaccess_test starting\n");
  buf = malloc(32 * PGSIZE);
  if (buf == 0) {
    printf("malloc failed\n");
    exit(1);
  }

  if (pgaccess(buf, 32, &abits) < 0) {
    printf("pgaccess initial clear failed\n");
    exit(1);
  }

  buf[PGSIZE * 1] += 1;
  buf[PGSIZE * 2] += 1;
  buf[PGSIZE * 30] += 1;

  if (pgaccess(buf, 32, &abits) < 0) {
    printf("pgaccess failed\n");
    exit(1);
  }

  if (abits != ((1 << 1) | (1 << 2) | (1 << 30))) {
    printf("incorrect access bits set: 0x%x\n", abits);
    exit(1);
  }

  if (pgaccess(buf, 32, &abits) < 0) {
    printf("pgaccess second call failed\n");
    exit(1);
  }
  if (abits != 0) {
    printf("access bits not cleared: 0x%x\n", abits);
    exit(1);
  }

  if (pgaccess(buf, 33, &abits) == 0 || pgaccess(buf, 4, 0) == 0) {
    printf("pgaccess accepted invalid arguments\n");
    exit(1);
  }

  free(buf);
  printf("pgaccess_test: OK\n");
}

int
main(int argc, char *argv[])
{
  pgaccess_test();
  exit(0);
}
