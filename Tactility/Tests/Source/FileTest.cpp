#include "doctest.h"
#include <Tactility/file/File.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>

using namespace tt;

TEST_CASE("findOrCreateDirectory can create a directory tree without prefix") {
    CHECK_EQ(file::findOrCreateDirectory("test1/test1", 0777), true);
    CHECK_EQ(file::deleteRecursively("test1"), true);
}

TEST_CASE("findOrCreateDirectory can create a directory tree with prefix") {
    CHECK_EQ(file::findOrCreateDirectory("/tmp/test2", 0777), true);
    CHECK_EQ(file::deleteRecursively("/tmp/test2"), true);
}

TEST_CASE("deleteRecursively removes special entries") {
    CHECK_EQ(file::findOrCreateDirectory("/tmp/test3/sub", 0777), true);
    REQUIRE_EQ(mkfifo("/tmp/test3/sub/fifo", 0600), 0);

    const int socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    REQUIRE_GE(socket_fd, 0);
    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, "/tmp/test3/socket");
    REQUIRE_EQ(bind(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
    close(socket_fd);

    // Directly, and as part of a directory
    CHECK_EQ(file::deleteRecursively("/tmp/test3/socket"), true);
    CHECK_EQ(file::deleteRecursively("/tmp/test3"), true);
    struct stat info;
    CHECK_NE(stat("/tmp/test3", &info), 0);
}
