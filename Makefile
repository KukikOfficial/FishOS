CC = gcc
CXX = g++
LD = ld

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -Iinclude
CXXFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti -fno-builtin -Iinclude
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

OBJS = src/boot.o src/drivers.o src/vfs.o src/graphics.o src/gui.o src/kernel.o

all: myos.iso

src/boot.o: src/boot.s
	$(CC) $(CFLAGS) -c $< -o $@

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

myos.bin: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o myos.bin $(OBJS)

initrd.tar:
	mkdir -p rootfs
	echo "Welcome to Modular LiteOS!\nEdit this file and save it." > rootfs/readme.txt
	echo "1. Test minimize button\n2. Test delete button\n3. Draw art" > rootfs/todo.txt
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
	rm -rf src/*.o myos.bin myos.iso isodir initrd.tar rootfs