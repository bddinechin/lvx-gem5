/* Freestanding exercise of the LVX SE-mode syscall shim.
   Issues scalls directly (no libc), returns 0 on success or the failing
   step number, so the harness's exit-code comparison can see it.  */

typedef unsigned long u64;

/* scall takes the number as an immediate, so one macro per call site.  */
#define SCALL(nr, a, b, c)                                                  \
  ({                                                                        \
    register long _r0 __asm__("r0") = (long)(a);                            \
    register long _r1 __asm__("r1") = (long)(b);                            \
    register long _r2 __asm__("r2") = (long)(c);                            \
    __asm__ volatile("scall " #nr "\n\t;;"                                  \
                     : "+r"(_r0)                                            \
                     : "r"(_r1), "r"(_r2)                                   \
                     : "memory");                                           \
    _r0;                                                                    \
  })

/* Target open() flags (S_* in scall_no.h), not the host's.  */
#define S_RDWR  0x004
#define S_CREAT 0x010
#define S_TRUNC 0x020

static int
streq (const char *a, const char *b, int n)
{
  for (int i = 0; i < n; i++)
    if (a[i] != b[i])
      return 0;
  return 1;
}

int
main (void)
{
  static const char path[] = "/tmp/lvx_scalltest.txt";
  static const char msg[] = "hello";
  char buf[8];
  u64 st[13];
  long fd, r;

  fd = SCALL (10, path, S_RDWR | S_CREAT | S_TRUNC, 0644);   /* open  */
  if (fd < 0)
    return 1;

  r = SCALL (17, fd, msg, 5);                                /* write */
  if (r != 5)
    return 2;

  r = SCALL (9, fd, 0, 0);                                   /* lseek SEEK_SET */
  if (r != 0)
    return 3;

  buf[0] = 0;
  r = SCALL (11, fd, buf, 5);                                /* read  */
  if (r != 5 || !streq (buf, msg, 5))
    return 4;

  r = SCALL (6, fd, st, 0);                                  /* fstat */
  if (r != 0 || st[7] != 5)                                  /* st[7] = st_size */
    return 5;

  r = SCALL (19, fd, 0, 0);                                  /* isatty: 0, a file */
  if (r != 0)
    return 6;

  r = SCALL (4, fd, 0, 0);                                   /* close */
  if (r != 0)
    return 7;

  r = SCALL (14, path, st, 0);                               /* stat  */
  if (r != 0 || st[7] != 5)
    return 8;

  r = SCALL (52, path, 0, 0);                                /* access, exists */
  if (r != 0)
    return 9;

  r = SCALL (8, path, 0, 0);                                 /* unlink */
  if (r != 0)
    return 10;

  r = SCALL (52, path, 0, 0);                                /* access, gone */
  if (r != -2)                                               /* -ENOENT */
    return 11;

  r = SCALL (99, 0, 0, 0);                                   /* unimplemented */
  if (r != -38)                                              /* -ENOSYS */
    return 12;

  return 0;
}
