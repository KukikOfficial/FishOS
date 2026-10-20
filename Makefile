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

myos.iso: myos.bin grub.cfg
	mkdir -p isodir/boot/grub
	cp myos.bin isodir/boot/
	cp grub.cfg isodir/boot/grub/
	grub-mkrescue -o myos.iso isodir

run: myos.iso
	qemu-system-i386 -cdrom myos.iso

clean:
	rm -rf *.o myos.bin myos.iso isodir