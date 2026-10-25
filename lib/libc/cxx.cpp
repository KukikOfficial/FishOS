// Minimal C++ runtime support for FishOS programs.
#include <stdlib.h>
#include <stddef.h>
#include <fihcore/syscall.h>

void* operator new(size_t n) { return malloc(n); }
void* operator new[](size_t n) { return malloc(n); }
void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }

extern "C" void __cxa_pure_virtual() {
    static const char msg[] = "pure virtual call\n";
    fih_log(msg, sizeof(msg) - 1);
    fih_exit(134);
}
