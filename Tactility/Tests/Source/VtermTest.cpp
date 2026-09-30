#include "doctest.h"

extern "C" {
#include <Tactility/app/terminal/vterm/vterm.h>
}

#include <cstring>
#include <string>

namespace {

void write(const char* text) {
    vterm_write(0, text, strlen(text));
}

char cell(int row, int col) {
    return vterm_get_direct_buffer()[row * VTERM_COLS + col].ch;
}

std::string column0(int rows) {
    std::string result;
    for (int row = 0; row < rows; row++) {
        result += cell(row, 0);
    }
    return result;
}

} // namespace

TEST_CASE("vterm: a scroll region limits line feeds and inserted lines to its rows") {
    REQUIRE_EQ(vterm_init(), ERROR_NONE);
    vterm_set_size_override(5, 10);

    write("\x1B[2J\x1B[H1\r\n2\r\n3\r\n4\r\n5");
    CHECK_EQ(column0(5), "12345");

    // Rows 2-4 (1-based) scroll, rows 1 and 5 stay put
    write("\x1B[2;4r");
    write("\x1B[4;1H\n");
    CHECK_EQ(column0(5), "134 5");

    write("\x1B[2;1H\x1B[L");
    CHECK_EQ(column0(5), "1 345");

    write("\x1B[2;1H\x1B[M");
    CHECK_EQ(column0(5), "134 5");

    // A full clear keeps the scroll region
    write("\x1B[2J\x1B[H1\r\n2\r\n3\r\n4\x1B[5;1H5");
    CHECK_EQ(column0(5), "12345");
    write("\x1B[4;1H\n");
    CHECK_EQ(column0(5), "134 5");

    // DECSTBM without parameters restores the whole screen as scroll region
    write("\x1B[r\x1B[5;1H\n");
    CHECK_EQ(column0(5), "34 5 ");

    vterm_clear_size_override();
    vterm_deinit();
}

TEST_CASE("vterm: an omitted scroll region top defaults to the first row") {
    REQUIRE_EQ(vterm_init(), ERROR_NONE);
    vterm_set_size_override(5, 10);

    write("\x1B[2J\x1B[H1\r\n2\r\n3\r\n4\r\n5");
    write("\x1B[;4r\x1B[4;1H\n");
    CHECK_EQ(column0(5), "234 5");

    vterm_clear_size_override();
    vterm_deinit();
}

TEST_CASE("vterm: CHA moves the cursor to a column on the current row") {
    REQUIRE_EQ(vterm_init(), ERROR_NONE);
    vterm_set_size_override(5, 10);

    write("\x1B[2J\x1B[3;2H\x1B[5G");
    int col = -1, row = -1, visible = 0;
    vterm_get_cursor(0, &col, &row, &visible);
    CHECK_EQ(col, 4);
    CHECK_EQ(row, 2);

    vterm_clear_size_override();
    vterm_deinit();
}
