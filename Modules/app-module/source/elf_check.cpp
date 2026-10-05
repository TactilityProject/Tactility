// SPDX-License-Identifier: Apache-2.0
#include <app/elf_check.h>

#include <cstdio>
#include <cstring>

namespace {

constexpr size_t ELF_HEADER_PREFIX_SIZE = 20; // e_ident[16] + e_type(2) + e_machine(2)
constexpr uint8_t ELF_MAGIC[4] = { 0x7f, 'E', 'L', 'F' };

uint16_t read_le16(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
}

} // namespace

bool elf_check_file(const char* path, const struct ElfRequirements* requirements) {
    FILE* file = fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }

    uint8_t header[ELF_HEADER_PREFIX_SIZE];
    size_t read = fread(header, 1, sizeof(header), file);
    fclose(file);

    if (read != sizeof(header)) {
        return false;
    }

    if (memcmp(header, ELF_MAGIC, sizeof(ELF_MAGIC)) != 0) {
        return false;
    }

    uint8_t elf_class = header[4]; // e_ident[EI_CLASS]
    uint8_t data = header[5]; // e_ident[EI_DATA]
    uint16_t type = read_le16(header + 16); // e_type
    uint16_t machine = read_le16(header + 18); // e_machine

    return elf_class == requirements->elf_class
        && data == requirements->data
        && type < 32
        && (requirements->types & ELF_TYPE_MASK(type)) != 0
        && machine == requirements->machine;
}

bool elf_has_magic(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }

    uint8_t magic[sizeof(ELF_MAGIC)];
    size_t read = fread(magic, 1, sizeof(magic), file);
    fclose(file);

    return read == sizeof(magic) && memcmp(magic, ELF_MAGIC, sizeof(ELF_MAGIC)) == 0;
}
