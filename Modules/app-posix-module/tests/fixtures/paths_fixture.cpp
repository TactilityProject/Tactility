// SPDX-License-Identifier: Apache-2.0
#include <tactility/paths.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

/** @return the number of the first check that failed, 0 when all passed */
int run_relative_path_checks(const std::string& directory) {
    if (chdir(directory.c_str()) != 0) {
        return 1;
    }

    int fd = open("file.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return 2;
    }
    if (write(fd, "abc", 3) != 3) {
        return 3;
    }
    close(fd);

    if (mkdir("sub", 0755) != 0) {
        return 4;
    }
    struct stat st {};
    if (stat("./sub", &st) != 0 || !S_ISDIR(st.st_mode)) {
        return 5;
    }
    DIR* dir = opendir("sub");
    if (dir == nullptr) {
        return 6;
    }
    closedir(dir);

    // "." and ".." segments are collapsed
    FILE* file = fopen("sub/.././file.txt", "r");
    if (file == nullptr) {
        return 7;
    }
    char content[4] = {};
    const size_t read_count = fread(content, 1, 3, file);
    fclose(file);
    if (read_count != 3 || strcmp(content, "abc") != 0) {
        return 8;
    }

    if (rename("file.txt", "sub/moved.txt") != 0) {
        return 9;
    }
    if (access("sub/moved.txt", F_OK) != 0 || access("file.txt", F_OK) == 0) {
        return 10;
    }
    if (truncate("sub/moved.txt", 1) != 0 || stat("sub/moved.txt", &st) != 0 || st.st_size != 1) {
        return 11;
    }
    if (unlink("sub/moved.txt") != 0) {
        return 12;
    }
    if (rmdir("sub") != 0) {
        return 13;
    }

    // Left behind for the test to find in the app's cwd, rather than in the process's
    fd = open("kept.txt", O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        return 14;
    }
    close(fd);

    // chdir() collapses ".." too
    if (chdir("..") != 0) {
        return 15;
    }
    char cwd[FILE_MAX_PATH_STRING_LENGTH];
    const std::string parent = directory.substr(0, directory.rfind('/'));
    if (getcwd(cwd, sizeof(cwd)) == nullptr || parent != cwd) {
        return 16;
    }

    const std::string too_long(FILE_MAX_PATH_STRING_LENGTH, 'a');
    if (open(too_long.c_str(), O_RDONLY) != -1 || errno != ENAMETOOLONG) {
        return 17;
    }

    if (chdir(directory.c_str()) != 0) {
        return 18;
    }

    // A trailing separator requires a directory
    fd = open("plain", O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        return 19;
    }
    close(fd);
    if (unlink("plain/") != -1 || errno != ENOTDIR || access("plain", F_OK) != 0) {
        return 20;
    }
    unlink("plain");

    // ".." after a symlink goes to the parent of its target
    // symlink() isn't resolved against the app's cwd, so it gets absolute paths
    const std::string link_target = directory + "/real/inner";
    const std::string link_path = directory + "/link";
    if (mkdir("real", 0755) != 0 || mkdir("real/inner", 0755) != 0 || symlink(link_target.c_str(), link_path.c_str()) != 0) {
        return 21;
    }
    fd = open("real/marker", O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        return 22;
    }
    close(fd);
    if (access("link/../marker", F_OK) != 0) {
        return 23;
    }
    unlink("real/marker");
    unlink("link");
    rmdir("real/inner");
    rmdir("real");

    return 0;
}

} // namespace

// Runs the checks in the directory argv[1] and writes the number of the first one that failed to argv[2] (absolute)
extern "C" int32_t main(int argc, char* argv[]) {
    if (argc < 3) {
        return 1;
    }
    const int failed_check = run_relative_path_checks(argv[1]);
    FILE* output = fopen(argv[2], "w");
    if (output == nullptr) {
        return 2;
    }
    fprintf(output, "%d", failed_check);
    fclose(output);
    return 0;
}
