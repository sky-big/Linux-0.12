
#
# if you want the ram-disk device, define this to be the
# size in blocks.
#
RAMDISK =  #-DRAMDISK=1024

# This is a basic Makefile for setting the general configuration
include Makefile.header

LDFLAGS	+= -Ttext 0 -e startup_32
CFLAGS	+= $(RAMDISK)
CPP	+= -Iinclude

#
# ROOT_DEV specifies the default root-device when making the image.
# This can be either FLOPPY, /dev/xxxx or empty, in which case the
# default of /dev/hd6 is used by 'build'.
#
ROOT_DEV=0301
SWAP_DEV=0304

ARCHIVES=kernel/kernel.o mm/mm.o fs/fs.o
DRIVERS =kernel/blk_drv/blk_drv.a kernel/chr_drv/chr_drv.a
MATH	=kernel/math/math.a
LIBS	=lib/lib.a

.c.s:
	$(CC) $(CFLAGS) \
	-nostdinc -Iinclude -S -o $*.s $<
.s.o:
	$(AS) -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) \
	-nostdinc -Iinclude -c -o $*.o $<


all: clean Image

Image: boot/bootsect boot/setup tools/system
	@cp -f tools/system system.tmp
	@strip system.tmp
	@objcopy -O binary -R .note -R .comment system.tmp tools/kernel
	@tools/build.sh boot/bootsect boot/setup tools/kernel Kernel_Image $(ROOT_DEV) $(SWAP_DEV)
	@rm system.tmp
	@rm tools/kernel -f
	@cp Kernel_Image ../linux-0.12-080324
	@sync

boot/bootsect: boot/bootsect.S
	@make bootsect -C boot

boot/setup: boot/setup.S
	@make setup -C boot

boot/head.o: boot/head.s
	@make head.o -C boot

tools/build: tools/build.c
	$(CC) $(CFLAGS) \
	-o tools/build tools/build.c

tools/system:	boot/head.o init/main.o \
		$(ARCHIVES) $(DRIVERS) $(MATH) $(LIBS)
	$(LD) $(LDFLAGS) boot/head.o init/main.o \
	$(ARCHIVES) \
	$(DRIVERS) \
	$(MATH) \
	$(LIBS) \
	-o tools/system
	@nm tools/system | grep -v '\(compiled\)\|\(\.o$$\)\|\( [aU] \)\|\(\.\.ng$$\)\|\(LASH[RL]DI\)'| sort > System.map
	@objdump -S tools/system > system.S

kernel/math/math.a:
	@make -C kernel/math

fs/fs.o:
	@make -C fs

kernel/kernel.o:
	@make -C kernel

mm/mm.o:
	@make -C mm

lib/lib.a:
	@make -C lib

kernel/blk_drv/blk_drv.a:
	@make -C kernel/blk_drv

kernel/chr_drv/chr_drv.a:
	@make -C kernel/chr_drv

clean:
	@rm -f Kernel_Image System.map System_s.map system.S tmp_make core boot/bootsect boot/setup
	@rm -f init/*.o tools/system boot/*.o typescript* info bochsout.txt
	@for i in mm fs kernel lib boot; do make clean -C $$i; done

debug:
	@qemu-system-i386 -m 32M -boot a -fda Image -fdb rootimage-0.12 -hda rootimage-0.12-hd \
	-serial pty -S -gdb tcp::1234

start:
	@qemu-system-i386 -m 32M -boot a -fda Image -fdb rootimage-0.12 -hda rootimage-0.12-hd

dep:
	@sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	@(for i in init/*.c;do echo -n "init/";$(CPP) -M $$i;done) >> tmp_make
	@cp tmp_make Makefile
	@for i in fs kernel mm lib; do make dep -C $$i; done

### Dependencies:
init/main.o: init/main.c include/unistd.h include/sys/stat.h \
 include/sys/types.h include/sys/time.h include/time.h \
 include/sys/times.h include/sys/utsname.h include/sys/param.h \
 include/sys/resource.h include/utime.h include/linux/tty.h \
 include/termios.h include/linux/sched.h include/linux/head.h \
 include/linux/fs.h include/linux/mm.h include/linux/kernel.h \
 include/signal.h include/asm/system.h include/asm/io.h include/stddef.h \
 include/stdarg.h include/fcntl.h include/string.h
