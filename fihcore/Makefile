# Fihcore build. Targets:
#   make                kernel (build/fihcore.elf) and user runtime (build/user/crt0.o)
#   make tools          host tools (build/host/fihfs-tool)
#   make test-host      FihFS unit tests on the host
#   make test           kernel integration test in QEMU (serial log in build/test/serial.log)
#   make iso            bootable test ISO with the integration test as init

VERSION   := 0.1.0
CROSS     ?= i686-elf-
CC        := $(CROSS)gcc
CXX       := $(CROSS)g++
HOSTCXX   ?= c++
QEMU      ?= qemu-system-i386
GRUB_MKRESCUE ?= $(shell command -v i686-elf-grub-mkrescue || command -v grub-mkrescue)
B         := build

COMMON    := -ffreestanding -O2 -g -Wall -Wextra -fno-pic -fno-stack-protector -Iinclude -MMD -MP
KFLAGS    := $(COMMON) -mgeneral-regs-only -DFIH_KERNEL -DFIHCORE_VERSION='"$(VERSION)"' -Ikernel
KCXXFLAGS := $(KFLAGS) -std=c++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics
UFLAGS    := $(COMMON) -std=c11

KSRC := $(shell find kernel -name '*.cpp' -o -name '*.S' | sort)
KOBJ := $(patsubst %,$(B)/%.o,$(KSRC))

all: $(B)/fihcore.elf $(B)/user/crt0.o

$(B)/fihcore.elf: $(KOBJ) kernel/linker.ld
	$(CXX) -T kernel/linker.ld -nostdlib -o $@ $(KOBJ) -lgcc

$(B)/kernel/%.cpp.o: kernel/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(KCXXFLAGS) -c $< -o $@

$(B)/kernel/%.S.o: kernel/%.S
	@mkdir -p $(dir $@)
	$(CC) $(KFLAGS) -c $< -o $@

$(B)/user/crt0.o: user/crt0.c include/fihcore/syscall.h
	@mkdir -p $(dir $@)
	$(CC) $(UFLAGS) -c $< -o $@

# ---- host tools and tests ----------------------------------------------------
HOSTFLAGS := -std=c++17 -O1 -g -Wall -Wextra -Iinclude -Ikernel

tools: $(B)/host/fihfs-tool

$(B)/host/fihfs-tool: tools/fihfs-tool.cpp kernel/fs/fihfs.cpp kernel/fs/fihfs.hpp include/fihcore/abi.h
	@mkdir -p $(dir $@)
	$(HOSTCXX) $(HOSTFLAGS) tools/fihfs-tool.cpp kernel/fs/fihfs.cpp -o $@

$(B)/host/fihfs_test: tests/fihfs_test.cpp kernel/fs/fihfs.cpp kernel/fs/fihfs.hpp include/fihcore/abi.h
	@mkdir -p $(dir $@)
	$(HOSTCXX) $(HOSTFLAGS) -fsanitize=address,undefined tests/fihfs_test.cpp kernel/fs/fihfs.cpp -o $@

test-host: $(B)/host/fihfs_test
	$(B)/host/fihfs_test

# ---- integration test in QEMU ------------------------------------------------
$(B)/tests/ktest.elf: tests/ktest/ktest.c $(B)/user/crt0.o user/user.ld include/fihcore/syscall.h
	@mkdir -p $(dir $@)
	$(CC) $(UFLAGS) -c tests/ktest/ktest.c -o $(B)/tests/ktest.o
	$(CC) -T user/user.ld -nostdlib -o $@ $(B)/user/crt0.o $(B)/tests/ktest.o -lgcc

$(B)/test/fihcore-test.iso: $(B)/fihcore.elf $(B)/tests/ktest.elf tests/grub.cfg
	rm -rf $(B)/test/iso $(B)/test/root
	mkdir -p $(B)/test/iso/boot/grub $(B)/test/root/bin
	cp $(B)/tests/ktest.elf $(B)/test/root/bin/init
	COPYFILE_DISABLE=1 tar --format=ustar -cf $(B)/test/iso/boot/initrd.tar -C $(B)/test/root .
	cp $(B)/fihcore.elf $(B)/test/iso/boot/
	cp tests/grub.cfg $(B)/test/iso/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -o $@ $(B)/test/iso 2>/dev/null

iso: $(B)/test/fihcore-test.iso

test: $(B)/test/fihcore-test.iso $(B)/host/fihfs-tool
	tests/run-ktest.sh $(B)

clean:
	rm -rf $(B)

.PHONY: all tools test-host test iso clean
-include $(shell find $(B) -name '*.d' 2>/dev/null)
