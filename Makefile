PREFIX ?= /opt/homebrew/bin/

CC = $(PREFIX)i686-elf-gcc
CXX = $(PREFIX)i686-elf-g++
LD = $(PREFIX)i686-elf-ld
GRUB_MKRESCUE = PATH="/opt/homebrew/bin:$$PATH" $(PREFIX)i686-elf-grub-mkrescue
QEMU = $(PREFIX)qemu-system-i386
QEMU_IMG = $(PREFIX)qemu-img

CFLAGS = -ffreestanding -O2 -Wall -Wextra -Iinclude
CXXFLAGS = -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti -fno-builtin -Iinclude
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

C_SRCS = $(wildcard src/*.c)
CXX_SRCS = $(wildcard src/*.cpp)
ASM_SRCS = $(wildcard src/*.s)

OBJS = $(ASM_SRCS:.s=.o) $(C_SRCS:.c=.o) $(CXX_SRCS:.cpp=.o)

all: myos.iso

src/%.o: src/%.s
	$(CC) $(CFLAGS) -c $< -o $@

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

myos.bin: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o myos.bin $(OBJS)

initrd.tar:
	mkdir -p rootfs
	echo "Welcome to FishOS v2.0!\nEdit this file and save it." > rootfs/readme.txt
	echo "1. Browse folders\n2. Spawn Ring 3 tasks\n3. Enjoy multitasking" > rootfs/todo.txt
	tar -cf initrd.tar -C rootfs .

myos.iso: myos.bin grub.cfg initrd.tar
	mkdir -p isodir/boot/grub
	cp myos.bin isodir/boot/
	cp initrd.tar isodir/boot/
	cp grub.cfg isodir/boot/grub/
	$(GRUB_MKRESCUE) -o myos.iso isodir

hdd.img:
	$(QEMU_IMG) create -f raw hdd.img 10M

run: myos.iso hdd.img
	$(QEMU) -cdrom myos.iso -drive file=hdd.img,format=raw,index=0,media=disk -m 256M -vga std -audiodev coreaudio,id=snd0 -machine pcspk-audiodev=snd0

clean:
	rm -rf src/*.o myos.bin myos.iso isodir initrd.tar rootfs hdd.img