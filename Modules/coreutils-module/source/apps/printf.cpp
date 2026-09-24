#include <app/manifest.h>

#include <cstdio>
#include <cstdlib>

namespace coreutils::printf {

/**
 * printf: writes the format string with backslash escapes interpreted, substituting arguments.
 *
 * Only the conversions a shell script realistically uses are handled (%s, %d, %%). This is not a
 * general printf, and the format is never handed to the C library, since a script-supplied format
 * string with an unexpected conversion would read arbitrary stack.
 *
 * Calls into ::printf() throughout: this function's own enclosing namespace is also named
 * `printf`, so an unqualified call here would resolve to that namespace, not <cstdio>'s printf().
 */
static int32_t main(int argc, char* argv[]) {
    if (argc < 2) {
        puts("usage: printf <format> [args...]");
        return 1;
    }

    int nextArg = 2;

    for (const char* p = argv[1]; *p != '\0'; p++) {
        if (*p == '\\' && p[1] != '\0') {
            p++;

            // Octal escapes: \033 is how a script writes ESC to start an ANSI colour sequence.
            if (*p >= '0' && *p <= '7') {
                int value = 0;
                int digits = 0;
                while (digits < 3 && *p >= '0' && *p <= '7') {
                    value = value * 8 + (*p - '0');
                    p++;
                    digits++;
                }
                p--; // the loop's own p++ will step past the last digit
                putchar(static_cast<char>(value));
                continue;
            }

            switch (*p) {
                case 'n': putchar('\n'); break;
                case 't': putchar('\t'); break;
                case 'r': putchar('\r'); break;
                case 'e': putchar('\x1B'); break; // \e, a common shorthand for ESC
                case 'a': break;                               // bell: nothing to ring
                case '\\': putchar('\\'); break;
                default: {
                    const char text[3] = { '\\', *p, '\0' };
                    ::printf("%s", text);
                    break;
                }
            }
            continue;
        }

        if (*p == '%' && p[1] != '\0') {
            p++;
            if (*p == '%') {
                putchar('%');
                continue;
            }
            const char* value = (nextArg < argc) ? argv[nextArg++] : "";
            switch (*p) {
                case 's': ::printf("%s", value); break;
                case 'd':
                case 'i': ::printf("%d", atoi(value)); break;
                default: {
                    const char text[3] = { '%', *p, '\0' };
                    ::printf("%s", text);
                    break;
                }
            }
            continue;
        }

        const char text[2] = { *p, '\0' };
        ::printf("%s", text);
    }

    return 0;
}

extern const ::AppManifest manifest = {
    .id = "printf",
    .name = "printf",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 4096, .desired_memory_capability = 0 },
};

} // namespace coreutils::printf
