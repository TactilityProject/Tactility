// SPDX-License-Identifier: Apache-2.0
#include "doctest.h"

#include <app/elf_check.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace {

constexpr ElfRequirements REQUIREMENTS = {
    .elf_class = ELF_CLASS_32,
    .data = ELF_DATA_2LSB,
    .types = ELF_TYPE_MASK(ELF_TYPE_DYN) | ELF_TYPE_MASK(ELF_TYPE_REL),
    .machine = ELF_MACHINE_XTENSA,
};

class TempFile {
    std::string path;

public:
    explicit TempFile(const uint8_t* data, size_t size) {
        char name[] = "/tmp/elf_check_test_XXXXXX";
        const int fd = mkstemp(name);
        REQUIRE_NE(fd, -1);
        REQUIRE_EQ(write(fd, data, size), static_cast<ssize_t>(size));
        close(fd);
        path = name;
    }

    ~TempFile() { unlink(path.c_str()); }

    const char* get() const { return path.c_str(); }
};

TempFile makeElf(uint8_t elf_class, uint16_t type, uint16_t machine) {
    uint8_t header[20] = { 0x7f, 'E', 'L', 'F', elf_class, ELF_DATA_2LSB };
    header[16] = type & 0xFF;
    header[17] = type >> 8;
    header[18] = machine & 0xFF;
    header[19] = machine >> 8;
    return TempFile(header, sizeof(header));
}

} // namespace

TEST_CASE("elf_check_file accepts every type in the mask") {
    CHECK(elf_check_file(makeElf(ELF_CLASS_32, ELF_TYPE_DYN, ELF_MACHINE_XTENSA).get(), &REQUIREMENTS));
    CHECK(elf_check_file(makeElf(ELF_CLASS_32, ELF_TYPE_REL, ELF_MACHINE_XTENSA).get(), &REQUIREMENTS));
}

TEST_CASE("elf_check_file rejects a type outside the mask") {
    CHECK_FALSE(elf_check_file(makeElf(ELF_CLASS_32, 2, ELF_MACHINE_XTENSA).get(), &REQUIREMENTS));
    CHECK_FALSE(elf_check_file(makeElf(ELF_CLASS_32, 0xFE00, ELF_MACHINE_XTENSA).get(), &REQUIREMENTS));
}

TEST_CASE("elf_check_file rejects a mismatched class or machine") {
    CHECK_FALSE(elf_check_file(makeElf(ELF_CLASS_64, ELF_TYPE_DYN, ELF_MACHINE_XTENSA).get(), &REQUIREMENTS));
    CHECK_FALSE(elf_check_file(makeElf(ELF_CLASS_32, ELF_TYPE_DYN, ELF_MACHINE_RISCV).get(), &REQUIREMENTS));
}

TEST_CASE("elf_has_magic is true for any ELF and false otherwise") {
    CHECK(elf_has_magic(makeElf(ELF_CLASS_32, 2, ELF_MACHINE_RISCV).get()));

    const uint8_t script[] = "echo hello\n";
    CHECK_FALSE(elf_has_magic(TempFile(script, sizeof(script) - 1).get()));

    const uint8_t truncated[] = { 0x7f, 'E', 'L' };
    CHECK_FALSE(elf_has_magic(TempFile(truncated, sizeof(truncated)).get()));

    CHECK_FALSE(elf_has_magic("/nonexistent/elf_check_test"));
}
