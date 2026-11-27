#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static void
fail(const char *name, const char *msg)
{
  printf("VMPRINTTEST FAIL %s: %s\n", name, msg);
  exit(1);
}

static int
run_child_exec(int use_vmprint, char *path, char **argv)
{
  int pid = fork();
  if (pid < 0)
    return -2;
  if (pid == 0) {
    if (use_vmprint)
      exec_vmprint(path, argv);
    else
      exec(path, argv);
    exit(1);
  }
  int status = -1;
  if (wait(&status) < 0)
    return -3;
  return status;
}

static void
test_normal_exec(void)
{
  char *argv[] = { "echo", "normal_ok", 0 };
  printf("VMPRINTTEST BEGIN normal-exec\n");
  int st = run_child_exec(0, "echo", argv);
  printf("VMPRINTTEST END normal-exec\n");
  if (st != 0)
    fail("normal-exec", "expected exit status 0");
  printf("VMPRINTTEST PASS normal-exec\n");
}

static void
test_exec_vmprint_basic(void)
{
  char *argv[] = { "echo", "vmprint_ok", "arg2", 0 };
  printf("VMPRINTTEST BEGIN exec-vmprint-basic\n");
  int st = run_child_exec(1, "echo", argv);
  printf("VMPRINTTEST END exec-vmprint-basic\n");
  if (st != 0)
    fail("exec-vmprint-basic", "expected exit status 0");
  printf("VMPRINTTEST PASS exec-vmprint-basic\n");
}

static void
test_exec_vmprint_missing(void)
{
  char *argv[] = { "/does_not_exist", 0 };
  printf("VMPRINTTEST BEGIN exec-vmprint-missing\n");
  int ret = exec_vmprint("/does_not_exist", argv);
  printf("VMPRINTTEST END exec-vmprint-missing\n");
  if (ret != -1)
    fail("exec-vmprint-missing", "expected -1 for missing binary");
  printf("VMPRINTTEST PASS exec-vmprint-missing\n");
}

static void
test_exec_vmprint_badargs(void)
{
  char *argv[] = { "echo", 0 };
  if (exec_vmprint((char *)0, argv) != -1)
    fail("exec-vmprint-badargs", "expected -1 for null path");
  if (exec_vmprint("echo", (char **)0xffffffffffff0000ULL) != -1)
    fail("exec-vmprint-badargs", "expected -1 for bad argv pointer");
  printf("VMPRINTTEST PASS exec-vmprint-badargs\n");
}

static void
test_cli_usage(void)
{
  char *argv[] = { "vmprint", 0 };
  int st = run_child_exec(0, "vmprint", argv);
  if (st != 1)
    fail("cli-usage", "expected exit status 1");
  printf("VMPRINTTEST PASS cli-usage\n");
}

static void
test_cli_missing(void)
{
  char *argv[] = { "vmprint", "/no_such_cmd", 0 };
  int st = run_child_exec(0, "vmprint", argv);
  if (st != 1)
    fail("cli-missing", "expected exit status 1");
  printf("VMPRINTTEST PASS cli-missing\n");
}

static void
test_cli_exec(void)
{
  char *argv[] = { "vmprint", "echo", "cli_ok", 0 };
  int st = run_child_exec(0, "vmprint", argv);
  if (st != 0)
    fail("cli-exec", "expected exit status 0");
  printf("VMPRINTTEST PASS cli-exec\n");
}

static void
test_cli_multi_arg(void)
{
  char *argv[] = { "vmprint", "wc", "README", 0 };
  int st = run_child_exec(0, "vmprint", argv);
  if (st != 0)
    fail("cli-multi-arg", "expected exit status 0");
  printf("VMPRINTTEST PASS cli-multi-arg\n");
}

int
main(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  test_normal_exec();
  test_exec_vmprint_basic();
  test_exec_vmprint_missing();
  test_exec_vmprint_badargs();
  test_cli_usage();
  test_cli_missing();
  test_cli_exec();
  test_cli_multi_arg();

  printf("VMPRINTTEST ALL PASS (8 cases)\n");
  exit(0);
}
