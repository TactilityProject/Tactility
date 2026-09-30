// SPDX-License-Identifier: Apache-2.0
#include <sdkconfig.h>

#include <esp_elf.h>
#include <private/elf_platform.h>

// elf_loader's Xtensa relocation ignores the addend of R_XTENSA_GLOB_DAT/R_XTENSA_JMP_SLOT, which is
// S + A per the ABI. The compiler does emit addends there: newlib's ctype macros read (_ctype_ + 1)[c],
// which becomes "R_XTENSA_GLOB_DAT _ctype_ + 1". Without the addend every ctype lookup in an app reads
// the previous character's entry (e.g. isspace('!') is true).
extern "C" {

int __real_esp_elf_arch_relocate(esp_elf_t* elf, const elf32_rela_t* rela, const elf32_sym_t* sym, uint32_t addr);

int __wrap_esp_elf_arch_relocate(esp_elf_t* elf, const elf32_rela_t* rela, const elf32_sym_t* sym, uint32_t addr) {
#if CONFIG_IDF_TARGET_ARCH_XTENSA
    constexpr uint32_t R_XTENSA_GLOB_DAT = 3;
    constexpr uint32_t R_XTENSA_JMP_SLOT = 4;
    const uint32_t type = ELF_R_TYPE(rela->info);
    if (type == R_XTENSA_GLOB_DAT || type == R_XTENSA_JMP_SLOT) {
        addr += rela->addend;
    }
#endif
    return __real_esp_elf_arch_relocate(elf, rela, sym, addr);
}

}
