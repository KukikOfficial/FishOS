CC = gcc
CXX = g++
LD = ld

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra
CXXFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti -fno-builtin
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

OBJS = boot.o kernel.o

all: myos.iso

boot.o: boot.s
	$(CC) $(CFLAGS) -c boot.s -o boot.o

kernel.o: kernel.cpp font.hpp
	$(CXX) $(CXXFLAGS) -c kernel.cpp -o kernel.o

myos.bin: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o myos.bin $(OBJS)

initrd.tar:
	mkdir -p rootfs
	echo "Welcome to LiteOS Filesystem!\nYou can edit this text right here in Notepad.\nClick [Save File] to store changes." > rootfs/readme.txt
	echo "System configuration:\nResolution=1024x768\nColorDepth=32bpp\nDriver=VBE Framebuffer\nStorage=TAR VFS" > rootfs/config.sys
	echo "- Buy milk\n- Write an OS in C++\n- Draw a picture in Paint" > rootfs/notes.txt
	tar -cf initrd.tar -C rootfs .

myos.iso: myos.bin grub.cfg initrd.tar
	mkdir -p isodir/boot/grub
	cp myos.bin isodir/boot/
	cp initrd.tar isodir/boot/
	cp grub.cfg isodir/boot/grub/
	grub-mkrescue -o myos.iso isodir

run: myos.iso
	qemu-system-i386 -cdrom myos.iso -m 256M -vga std

clean:
	rm -rf *.o myos.bin myos.iso isodir initrd.tar rootfs