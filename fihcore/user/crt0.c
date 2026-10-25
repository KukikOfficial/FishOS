/* Process entry for Fihcore user programs: the kernel enters _start with a
 * cdecl frame (return address 0, argc, argv). */
#include <fihcore/syscall.h>

int main(int argc, char** argv);

__attribute__((used, noreturn)) void _start(int argc, char** argv) {
    fih_exit(main(argc, argv));
}
