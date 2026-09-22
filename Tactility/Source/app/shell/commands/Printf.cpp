#include <Tactility/app/shell/commands/CommandSupport.h>
#include <Tactility/app/shell/commands/Commands.h>

#include <Tactility/app/shell/Shell.h>

#include <cstdio>
#include <cstdlib>

/**
 * printf: writes the format string with backslash escapes interpreted, substituting arguments.
 *
 * Only the conversions a shell script realistically uses are handled (%s, %d, %%). This is not a
 * general printf, and the format is never handed to the C library, since a script-supplied format
 * string with an unexpected conversion would read arbitrary stack.
 */
int cmdPrintf(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: printf <format> [args...]\n");
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
                case 'n': printf("\n"); break;
                case 't': printf("\t"); break;
                case 'r': printf("\r"); break;
                case 'e': Shell::writeRaw("\x1B", 1); break; // \e, a common shorthand for ESC
                case 'a': break;                               // bell: nothing to ring
                case '\\': printf("\\"); break;
                default: {
                    const char text[3] = { '\\', *p, '\0' };
                    printf("%s", text);
                    break;
                }
            }
            continue;
        }

        if (*p == '%' && p[1] != '\0') {
            p++;
            if (*p == '%') {
                printf("%%");
                continue;
            }
            const char* value = (nextArg < argc) ? argv[nextArg++] : "";
            switch (*p) {
                case 's': printf("%s", value); break;
                case 'd':
                case 'i': printf("%d", atoi(value)); break;
                default: {
                    const char text[3] = { '%', *p, '\0' };
                    printf("%s", text);
                    break;
                }
            }
            continue;
        }

        const char text[2] = { *p, '\0' };
        printf("%s", text);
    }

    return 0;
}
