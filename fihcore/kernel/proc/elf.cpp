// Loader for static i386 ELF executables linked inside the user image range.
#include "proc.hpp"
#include "fs/vfs.hpp"
#include "lib/lib.hpp"
#include "mm/pmm.hpp"
#include "mm/vmm.hpp"

namespace {
struct [[gnu::packed]] Ehdr {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version, entry, phoff, shoff, flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct [[gnu::packed]] Phdr {
    uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align;
};
constexpr uint32_t PT_LOAD = 1;
constexpr int MAX_PHDRS = 16;
}

int elf_load(uint32_t pd, File* f, uint32_t* entry, uint32_t* image_end, uint32_t* pages) {
    fih_stat_t st;
    int e = file_stat(f, &st);
    if (e) return e;
    Ehdr eh;
    f->pos = 0;
    if (file_read(f, &eh, sizeof(eh)) != (int)sizeof(eh)) return -FIH_ENOEXEC;
    if (memcmp(eh.ident, "\x7f" "ELF", 4) != 0 || eh.ident[4] != 1 /* 32-bit */ ||
        eh.ident[5] != 1 /* little endian */ || eh.type != 2 /* ET_EXEC */ || eh.machine != 3 /* i386 */ ||
        eh.phentsize != sizeof(Phdr) || eh.phnum == 0 || eh.phnum > MAX_PHDRS)
        return -FIH_ENOEXEC;
    if (eh.entry < USER_BASE || eh.entry >= USER_IMAGE_END) return -FIH_ENOEXEC;
    if (eh.phoff > st.size || eh.phnum * sizeof(Phdr) > st.size - eh.phoff) return -FIH_ENOEXEC;

    Phdr ph[MAX_PHDRS];
    f->pos = eh.phoff;
    if (file_read(f, ph, eh.phnum * sizeof(Phdr)) != (int)(eh.phnum * sizeof(Phdr))) return -FIH_ENOEXEC;

    uint32_t end = USER_BASE;
    *pages = 0;
    for (int i = 0; i < eh.phnum; i++) {
        const Phdr& p = ph[i];
        if (p.type != PT_LOAD || p.memsz == 0) continue;
        if (p.filesz > p.memsz || p.vaddr < USER_BASE || p.vaddr >= USER_IMAGE_END ||
            p.memsz > USER_IMAGE_END - p.vaddr || p.offset > st.size || p.filesz > st.size - p.offset)
            return -FIH_ENOEXEC;
        uint32_t start = align_down(p.vaddr, PAGE_SIZE), stop = align_up(p.vaddr + p.memsz, PAGE_SIZE);
        // ponytail: all segments are mapped writable; add per-segment
        // protection (W^X) once the loader tracks page permissions.
        if ((e = vmm_alloc_range(pd, start, stop - start))) return e;
        *pages += (stop - start) / PAGE_SIZE;
        // Copy file data page by page through the identity map.
        uint32_t copied = 0;
        while (copied < p.filesz) {
            uint32_t va = p.vaddr + copied;
            uint32_t off = va & (PAGE_SIZE - 1);
            uint32_t chunk = kmin(p.filesz - copied, PAGE_SIZE - off);
            uint32_t pa = vmm_pte(pd, va) & ~0xFFFu;
            f->pos = p.offset + copied;
            if (file_read(f, reinterpret_cast<void*>(pa + off), chunk) != (int)chunk) return -FIH_EIO;
            copied += chunk;
        }
        if (stop > end) end = stop;
    }
    if (end == USER_BASE) return -FIH_ENOEXEC;
    *entry = eh.entry;
    *image_end = end;
    return 0;
}
