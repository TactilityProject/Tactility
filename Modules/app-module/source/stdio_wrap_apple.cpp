// SPDX-License-Identifier: Apache-2.0
#ifdef __APPLE__

#include <app/private/stdio_wrap_posix.h>

#include <cstdlib>
#include <sys/ioctl.h>
#include <unistd.h>

// <mach-o/dyld-interposing.h> isn't a public SDK header, so reimplemented locally.
#define TT_DYLD_INTERPOSE(replacement, replacee) \
    __attribute__((used)) static struct { const void* replacement; const void* replacee; } \
        tt_interpose_##replacee __attribute__((section("__DATA,__interpose"))) = { \
            (const void*)(unsigned long)&(replacement), (const void*)(unsigned long)&(replacee) \
        };

TT_DYLD_INTERPOSE(__wrap_read, read)
TT_DYLD_INTERPOSE(__wrap_write, write)
TT_DYLD_INTERPOSE(__wrap_ioctl, ioctl)
TT_DYLD_INTERPOSE(__wrap_close, close)
TT_DYLD_INTERPOSE(__wrap_getcwd, getcwd)
TT_DYLD_INTERPOSE(__wrap_chdir, chdir)
TT_DYLD_INTERPOSE(__wrap_fstat, fstat)
TT_DYLD_INTERPOSE(__wrap_poll, poll)
TT_DYLD_INTERPOSE(__wrap_tcgetattr, tcgetattr)
TT_DYLD_INTERPOSE(__wrap_tcsetattr, tcsetattr)
TT_DYLD_INTERPOSE(__wrap_exit, exit)

TT_DYLD_INTERPOSE(__wrap_vprintf, vprintf)
TT_DYLD_INTERPOSE(__wrap_printf, printf)
TT_DYLD_INTERPOSE(__wrap_vfprintf, vfprintf)
TT_DYLD_INTERPOSE(__wrap_fprintf, fprintf)
TT_DYLD_INTERPOSE(__wrap_puts, puts)
TT_DYLD_INTERPOSE(__wrap_fputs, fputs)
TT_DYLD_INTERPOSE(__wrap_putchar, putchar)
TT_DYLD_INTERPOSE(__wrap_fputc, fputc)
TT_DYLD_INTERPOSE(__wrap_fwrite, fwrite)
TT_DYLD_INTERPOSE(__wrap_getchar, getchar)
TT_DYLD_INTERPOSE(__wrap_fgetc, fgetc)
TT_DYLD_INTERPOSE(__wrap_fgets, fgets)

#endif // __APPLE__
