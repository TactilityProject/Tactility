// SPDX-License-Identifier: Apache-2.0
#include <posix_symbols/module.h>

#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#if __has_include(<getopt.h>)
#include <getopt.h>
#define POSIX_SYMBOLS_HAS_GETOPT_LONG 1
#endif

extern "C" {

static const ModuleSymbol SYMBOLS[] = {
    // unistd.h
    DEFINE_MODULE_SYMBOL(usleep),
    DEFINE_MODULE_SYMBOL(sleep),
    DEFINE_MODULE_SYMBOL(exit),
    DEFINE_MODULE_SYMBOL(close),
    DEFINE_MODULE_SYMBOL(rmdir),
    DEFINE_MODULE_SYMBOL(unlink),
    DEFINE_MODULE_SYMBOL(open),
    DEFINE_MODULE_SYMBOL(access),
    DEFINE_MODULE_SYMBOL(isatty),
    DEFINE_MODULE_SYMBOL(read),
    DEFINE_MODULE_SYMBOL(write),
    DEFINE_MODULE_SYMBOL(lseek),
    DEFINE_MODULE_SYMBOL(getcwd),
    DEFINE_MODULE_SYMBOL(chdir),
    DEFINE_MODULE_SYMBOL(ftruncate),
    DEFINE_MODULE_SYMBOL(fsync),
    // sys/ioctl.h
    DEFINE_MODULE_SYMBOL(ioctl),
    // sys/poll.h
    DEFINE_MODULE_SYMBOL(poll),
    // termios.h
    DEFINE_MODULE_SYMBOL(tcgetattr),
    DEFINE_MODULE_SYMBOL(tcsetattr),
    // strings.h
#if defined(__BSD_VISIBLE) && __BSD_VISIBLE
    DEFINE_MODULE_SYMBOL(explicit_bzero),
#endif
    DEFINE_MODULE_SYMBOL(strcasecmp),
    DEFINE_MODULE_SYMBOL(strncasecmp),
    // string.h
    DEFINE_MODULE_SYMBOL(strdup),
    DEFINE_MODULE_SYMBOL(stpcpy),
    // time.h
    DEFINE_MODULE_SYMBOL(clock_gettime),
    DEFINE_MODULE_SYMBOL(localtime_r),
    // setjmp.h
    DEFINE_MODULE_SYMBOL(longjmp),
    DEFINE_MODULE_SYMBOL(setjmp),
    // dirent.h
    DEFINE_MODULE_SYMBOL(opendir),
    DEFINE_MODULE_SYMBOL(closedir),
    DEFINE_MODULE_SYMBOL(readdir),
    // fcntl.h
    DEFINE_MODULE_SYMBOL(fcntl),
    // sys/stat.h
    DEFINE_MODULE_SYMBOL(stat),
    DEFINE_MODULE_SYMBOL(fstat),
    DEFINE_MODULE_SYMBOL(mkdir),
    // stdlib.h
    DEFINE_MODULE_SYMBOL(rand_r),
    DEFINE_MODULE_SYMBOL(setenv),
    DEFINE_MODULE_SYMBOL(unsetenv),
    // stdio.h - lets an app find the descriptor behind a stream. Needed when stdin/stdout have
    // been pointed somewhere other than descriptors 0 and 1, e.g. an app that owns a terminal.
    DEFINE_MODULE_SYMBOL(fileno),
    // getopt.h - optind/opterr/optarg/optopt are POSIX; getopt_long is a GNU/BSD extension, only
    // exported when <getopt.h> is actually available.
    DEFINE_MODULE_SYMBOL(optind),
    DEFINE_MODULE_SYMBOL(opterr),
    DEFINE_MODULE_SYMBOL(optarg),
    DEFINE_MODULE_SYMBOL(optopt),
#ifdef POSIX_SYMBOLS_HAS_GETOPT_LONG
    DEFINE_MODULE_SYMBOL(getopt_long),
#endif
    MODULE_SYMBOL_TERMINATOR,
};

Module posix_symbols_module = {
    .name = "posix-symbols",
    .start = nullptr,
    .stop = nullptr,
    .drivers = nullptr,
    .symbols = SYMBOLS,
    .internal = nullptr,
};

}
