#include <Tactility/app/shell/commands/Commands.h>

#include <cstdio>
#include <ctime>

int cmdDate(int, char**) {
    const time_t now = time(nullptr);
    struct tm parts;
    localtime_r(&now, &parts);

    char text[64];
    strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &parts);
    printf("%s\n", text);
    return 0;
}
