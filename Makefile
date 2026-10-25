# FishOS build. Requires the Fihcore submodule (git submodule update --init).
#   make            kernel, user space, boot image -> build/fishos.iso
#   make disk       create build/data.img (32 MiB FihFS) if it does not exist
#   make run        boot in QEMU with the persistent data disk
#   make test       automated boot + GUI workflow test (tests/run-smoke.sh)

CROSS   ?= i686-elf-
CC      := $(CROSS)gcc
CXX     := $(CROSS)g++
QEMU    ?= qemu-system-i386
GRUB_MKRESCUE ?= $(shell command -v i686-elf-grub-mkrescue || command -v grub-mkrescue)
FIHCORE := fihcore
B       := build
DISK    ?= $(B)/data.img

FLAGS    := -ffreestanding -O2 -g -Wall -Wextra -fno-pic -fno-stack-protector -MMD -MP \
            -I$(FIHCORE)/include -Iinclude -Ilib/libc/include
CFLAGS   := $(FLAGS) -std=c11
CXXFLAGS := $(FLAGS) -std=c++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics

KERNEL   := $(FIHCORE)/build/fihcore.elf
CRT0     := $(FIHCORE)/build/user/crt0.o
FSTOOL   := $(FIHCORE)/build/host/fihfs-tool

LIB_SRC  := $(wildcard lib/libc/*.c lib/libc/*.cpp lib/gfx/*.cpp lib/ui/*.cpp)
LIB_OBJ  := $(patsubst %,$(B)/%.o,$(LIB_SRC))
LIB      := $(B)/libfishos.a

# Every directory under system/ or apps/ with C++ sources is one program.
PROGRAMS := $(sort $(notdir $(patsubst %/,%,$(dir $(wildcard system/*/*.cpp apps/*/*.cpp)))))
BINS     := $(addprefix $(B)/bin/,$(PROGRAMS))
src_of    = $(wildcard system/$(1)/*.cpp apps/$(1)/*.cpp)

all: $(B)/fishos.iso

$(KERNEL) $(CRT0): FORCE
	$(MAKE) -C $(FIHCORE) all

$(FSTOOL): FORCE
	$(MAKE) -C $(FIHCORE) tools

$(B)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(LIB): $(LIB_OBJ)
	rm -f $@
	$(CROSS)ar rcs $@ $^

define program
$(B)/bin/$(1): $(patsubst %,$(B)/%.o,$(call src_of,$(1))) $(LIB) $(CRT0) $(FIHCORE)/user/user.ld
	@mkdir -p $$(dir $$@)
	$(CXX) -T $(FIHCORE)/user/user.ld -nostdlib -o $$@ $(CRT0) $(patsubst %,$(B)/%.o,$(call src_of,$(1))) $(LIB) -lgcc
endef
$(foreach p,$(PROGRAMS),$(eval $(call program,$(p))))

# Boot image: /system inside the running system (read-only).
$(B)/initrd.tar: $(BINS) $(wildcard system/apps/*.app) README.md
	rm -rf $(B)/root
	mkdir -p $(B)/root/bin $(B)/root/apps
	for f in $(BINS); do $(CROSS)strip -o $(B)/root/bin/$$(basename $$f) $$f; done
	for f in $(wildcard system/apps/*.app); do cp $$f $(B)/root/apps/; done
	COPYFILE_DISABLE=1 tar --format=ustar -cf $@ -C $(B)/root .

$(B)/fishos.iso: $(KERNEL) $(B)/initrd.tar grub.cfg
	rm -rf $(B)/iso
	mkdir -p $(B)/iso/boot/grub
	cp $(KERNEL) $(B)/iso/boot/fihcore.elf
	cp $(B)/initrd.tar $(B)/iso/boot/initrd.tar
	cp grub.cfg $(B)/iso/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -o $@ $(B)/iso 2>/dev/null

# The data disk holds /home and is never recreated if it already exists.
disk: $(FSTOOL)
	@if [ -e $(DISK) ]; then echo "$(DISK) exists, keeping it"; \
	else $(FSTOOL) mkfs $(DISK) 32 fishos-data && echo "created $(DISK)"; fi

QEMU_ARGS := -m 256M -cdrom $(B)/fishos.iso -boot d -vga std \
             -drive file=$(DISK),format=raw,if=ide,index=0 -serial stdio

run: all disk
	$(QEMU) $(QEMU_ARGS)

test: all $(FSTOOL)
	tests/run-smoke.sh $(B)

clean:
	rm -rf $(B)

.PHONY: all disk run test clean FORCE
-include $(shell find $(B) -name '*.d' 2>/dev/null)
