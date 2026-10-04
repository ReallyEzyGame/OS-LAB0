#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/riscv.h"
#include "kernel/vm.h"
#include "user/user.h"

//
// wrapper so that it's OK if main() does not call exit().
//
void
start(int argc, char **argv)
{
  int r;
  extern int main(int argc, char **argv);
  r = main(argc, argv);
  exit(r);
}

// Copy a string from s to t
char *
strcpy(char *s, const char *t)
{
  char *os;

  os = s;
  while ((*s++ = *t++) != 0)
    ;
  return os;
}

// Compare two strs base on lexicographical order
// Return negative value if p < q
//    0 if p = q
//    1 if p > q
int
strcmp(const char *p, const char *q)
{
  while (*p && *p == *q)
    p++, q++;
  return (uchar)*p - (uchar)*q;
}

// Return an uint size of the str
uint
strlen(const char *s)
{
  int n;

  for (n = 0; s[n]; n++)
    ;
  return n;
}

// Set a block of memory to some value
void *
memset(void *dst, int c, uint n)
{
  char *cdst = (char *)dst;
  int i;
  for (i = 0; i < n; i++) {
    cdst[i] = c;
  }
  return dst;
}

// Find the first character matches
// Return its address
// Otherwise return 0( null) 
char *
strchr(const char *s, char c)
{
  for (; *s; s++)
    if (*s == c)
      return (char *)s;
  return 0;
}

// Read a portition of SOMETHING( may be from file, terminal?...input stream)
// Return a line from the current INPUT STREAM(idk:))) or at most size 'max'
char *
gets(char *buf, int max)
{
  int i, cc;
  char c;

  for (i = 0; i + 1 < max;) {
    cc = read(0, &c, 1);
    if (cc < 1)
      break;
    buf[i++] = c;
    if (c == '\n' || c == '\r')
      break;
  }
  buf[i] = '\0';
  return buf;
}

// Read for file status
// Return file status in st and status code 
int
stat(const char *n, struct stat *st)
{
  int fd;
  int r;

  fd = open(n, O_RDONLY);
  if (fd < 0)
    return -1;
  r = fstat(fd, st);
  close(fd);
  return r;
}

// Converte a str to int
// Return int value
int
atoi(const char *s)
{
  int n;

  n = 0;
  while ('0' <= *s && *s <= '9')
    n = n * 10 + *s++ - '0';
  return n;
}

// Move a block of data from source block to destination block
void *
memmove(void *vdst, const void *vsrc, int n)
{
  char *dst;
  const char *src;

  dst = vdst;
  src = vsrc;
  if (src > dst) {
    while (n-- > 0)
      *dst++ = *src++;
  } else {
    dst += n;
    src += n;
    while (n-- > 0)
      *--dst = *--src;
  }
  return vdst;
}

// Compare two block of data with the same size n
// Return negative value if p1[i] < p2[i]
// 0 if both are equal
// Positive value if p1[i] > p2[i]
int
memcmp(const void *s1, const void *s2, uint n)
{
  const char *p1 = s1, *p2 = s2;
  while (n-- > 0) {
    if (*p1 != *p2) {
      return *p1 - *p2;
    }
    p1++;
    p2++;
  }
  return 0;
}

// Copy size of n data from src datablock to dst datablock
// Return the pointer of dst pointer
void *
memcpy(void *dst, const void *src, uint n)
{
  return memmove(dst, src, n);
}

// Still have not found out yet
char *
sbrk(int n)
{
  return sys_sbrk(n, SBRK_EAGER);
}

// Still have not found out yet
char *
sbrklazy(int n)
{
  return sys_sbrk(n, SBRK_LAZY);
}
