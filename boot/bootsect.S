!
! SYS_SIZE is the number of clicks (16 bytes) to be loaded.
! 0x3000 is 0x30000 bytes = 196kB, more than enough for current
! versions of linux
! SYS_SIZE是要加载的系统模块长度，单位是节，每节16字节。0x3000共为0x30000字节=196KB。
! 若以1024字节为1KB计，则因该就192KB。对应当前内核版本这个空间长度已足够了。当该值为
! 0x8000时，表示内核最大为512KB。因为内存0x90000处开始存放移动后的bootsect和setup的代码，
! 因此该值最大不得超过0x9000（表示584KB）。
!
! 头文件linux/config.h中定义了内核用到的一些常数符号和Linus自己使用的默认硬盘参数块。
! 例如定义了以下一些常数：
! DEF_INITSEG	0x9000							//引导扇区程序将被移动到得段值。
! DEF_SYSSEG	0x1000							//引导扇区程序把系统模块加载到内存的段值。
! DEF_SETUPSEG	0x9020							//setup程序所处内存段位置。
! DEF_SYSSIZE	0x3000							//内核系统模块默认最大节数（16字节=1节）。
!
#include <linux/config.h>
SYSSIZE = DEF_SYSSIZE             				!定义一个标号或符号。指明编译连接后system模块的大小。
!
!	bootsect.s		(C) 1991 Linus Torvalds
!	modified by Drew Eckhardt
!
! bootsect.s is loaded at 0x7c00 by the bios-startup routines, and moves
! iself out of the way to address 0x90000, and jumps there.
!
! It then loads 'setup' directly after itself (0x90200), and the system
! at 0x10000, using BIOS interrupts.
!
! NOTE! currently system is at most 8*65536 bytes long. This should be no
! problem, even in the future. I want to keep it simple. This 512 kB
! kernel size should be enough, especially as this doesn't contain the
! buffer cache as in minix
!
! The loader has been made as simple as possible, and continuos
! read errors will result in a unbreakable loop. Reboot by hand. It
! loads pretty fast by getting whole sectors at a time whenever possible.
!
!	bootsect.s		(C) 1991 Linus Torvalds
!	Drew Eckhardt修改
!
! bootsect.S被ROM BIOS启动子程序加载至0x7c00（32KB）处，并将自己移到了地址0x90000
! （576KB）处，并跳转至那里。
!
! 它然后使用BIOS中断将'setup'直接加载到自己的后面（0x90200）（576.5KB），并将system
! 加载到地址0x10000处。
!
! 注意！目前的内核系统最大长度限制为（8*65536）（512KB）B，即使是在将来这也应该没有问题
! 的。我想让他保持简单明了。这样512KB的最大内核长度应该足够了，尤其是这里没有像MINIX中
! 一样包含缓冲区高速缓冲。
.globl begtext, begdata, begbss, endtext, enddata, endbss
.text
begtext:
.data
begdata:
.bss
begbss:
.text

SETUPLEN = 4				! nr of setup-sectors
							! setup程序占用的扇区数
BOOTSEG  = 0x07c0			! original address of boot-sector
							! bootsect代码所在内存原始段地址
INITSEG  = DEF_INITSEG		! we move boot here - out of the way
							! 将bootsect移到位置0x90000 避开系统模块占用处
SETUPSEG = DEF_SETUPSEG		! setup starts here
							! setup程序从内存0x90200处开始
SYSSEG   = DEF_SYSSEG		! system loaded at 0x10000 (65536).
							! system模块加载到0x10000（64KB）处
ENDSEG   = SYSSEG + SYSSIZE	! where to stop loading
							! 停止加载的段地址

! ROOT_DEV & SWAP_DEV are now written by "build".
! 根文件系统设备号ROOT_DEV和交换设备号SWAP_DEV现在由tools目录下的build程序写入
ROOT_DEV = 0				!根文件系统设备使用与系统引导是同样的设备
SWAP_DEV = 0				!交换设备使用与系统引导是同样的设备


entry start					! 告知连接程序,程序从start标号开始执行
start:
    mov	ax, #BOOTSEG		! 将DS段寄存器置为0x07c0
	mov	ds, ax
	mov	ax, #INITSEG		! 将ES段寄存器置为0x9000
	mov	es, ax
	mov	cx, #256			! 设置移动计数值=256字（512字节）
	sub	si, si				! 源地址    ds:si = 0x07c0:0x0000
	sub	di, di				! 目的地址  es:di = 0x9000:0x0000
	rep						! 重复执行并递减cx的值，直到cx=0为止
	movw					! 即movs指令。从内存[si]处移动cx个字到[di]处
	jmpi	go, INITSEG		! 段间跳转（Jump Intersegment)。这里INITSEG指出跳转到得段地址，
							! 标号go是段内偏移地址

! 从现在开始，CPU移动到0x90000位置处的代码中执行。
! 这段代码设置几个寄存器，包括栈寄存器ss和sp。栈指针sp只要指向远大于512字节偏移（即地址0x90200）处都可以。因为
! 从0x90200地址开始出还要开始放置setup程序，而此时setup程序大约为４个扇区，因此sp要指向大于（0x200+0x200*4+堆栈大小）位置处。
! 这里sp设置为0x9ff00-12（参数表长度），即sp=0xfef4。在此之上位置会存放一个自建的驱动器参数表。实际上BIOS把引导扇区加载到0x7c00处
! 并把执行权交给引导程序时，ss=0x00，sp=0xfffe。
go:	mov	ax,cs
	mov	dx, #0xfef4			! arbitrary value >>512 - disk parm size

	mov	ds,ax
	mov	es,ax

	mov	ss,ax				! put stack at 0x9ff00 - 12.
	mov	sp,dx
/*
 *	Many BIOS's default disk parameter tables will not
 *	recognize multi-sector reads beyond the maximum sector number
 *	specified in the default diskette parameter tables - this may
 *	mean 7 sectors in some cases.
 *
 *	Since single sector reads are slow and out of the question,
 *	we must take care of this by creating new parameter tables
 *	(for the first disk) in RAM.  We will set the maximum sector
 *	count to 18 - the most we will encounter on an HD 1.44.
 *
 *	High doesn't hurt.  Low does.
 *
 *	Segments are as follows: ds=es=ss=cs - INITSEG,
 *		fs = 0, gs = parameter table segment
 */
/*
 *
 *	对于多扇区操作说读的扇区数超过默认磁盘参数表中指定的最大扇区数时，很多BIOS将不能进行正确识别。再某些情况下是7个扇区。
 *
 *	由于单扇区读操作太慢，不予考虑，因此我们必须通过在内存中重创建新的参数表（为第1个驱动器）来解决这个问题。我们将把其中
 *	最大扇区数设置为18，即在1.44MB磁盘上会碰到的最大数值。
 *
 *	这个数值大了不会出问题，但是太小就不行了。
 *
 *
 *	段寄存器将被设置成：ds=es=ss=cs 都为INITSEG（0X9000）
 *	fs = 0, gs = 参数表所在的段值。
 */

	push	#0				! 设置段寄存器fs = 0
	pop	fs					! fs:bx指向存有软驱参数表地址出(指针的指针)
	mov	bx, #0x78			! fs:bx is parameter table address
	 ! 下面指令表示下一条语句的操作数在fs段寄存器所指的段中。它只影响其下一条语句。这里把fs:bx所指内存位置处的表地址放到寄存器对gs:si
	 ! 中作为原地址。寄存器对es:di = 0x9000:0xfef4为目的地址。
	seg fs
	lgs	si, (bx)			! gs:si is source

	mov	di, dx				! es:di is destination  ! dx=0xfef4
	mov	cx, #6				! copy 12 bytes
	cld						! 清方向标志.复制时指针递增.

	rep						! 复制12字节的软驱参数表到0x9000:0xfef4处.
	seg gs
	movw

	mov	di, dx				! es:di指向新表，然后修改表中偏移4处的最大扇区数。
	movb 4(di), *18			! patch sector count

	seg fs					! 让中断向量0x1E的值指向新表
	mov	(bx), di
	seg fs
	mov	2(bx), es

	mov ax, cs				! 设置fs = gs = 0x9000
	mov	fs, ax
	mov	gs, ax

	xor	ah, ah				! reset FDC ! 复位软盘控制器,让其采用新参数.
	xor	dl, dl				! dl = 0 ,第1个软驱.
	int 0x13

! load the setup-sectors directly after the bootblock.
! Note that 'es' is already set up.
! 在bootsect程序块后紧跟着加载setup模块的代码数据.
! 注意es已经设置好了.(在移动代码时es已经指向目的段地址处0x9000)

! 以下代码的用途是利用ROM BIOS中断INT 0x13将setup模块从磁盘第2个扇区开始读到0x90200开始处，共读4个扇区。在读操作过程中如果读出错，则显示
! 磁盘上出错扇区位置,然后复位驱动器并重试,没有退路.
! INT 0x13读扇区使用调用参数设置如下:
! ah = 0x02  读磁盘扇区到内存		al = 需要读出的扇区数量;
! ch = 磁道(柱面)号的低8位;		cl = 开始扇区(位0~5),磁道号高2位(位6~7);
! dh = 磁头号；				dl = 驱动器号（如果是硬盘则位7要置位）；
! es:bx 指向数据缓冲区;	如果出错则CF标志置位,ah中是出错码.
load_setup:
	xor	dx, dx						! drive 0, head 0
	mov	cx, #0x0002					! sector 2, track 0
	mov	bx, #0x0200					! address = 512, in INITSEG
	mov	ax, #0x0200 + SETUPLEN		! service 2, nr of sectors
	int	0x13						! read it
	jnc	ok_load_setup				! ok - continue

	push	ax						! dump error code ! 显示出错信息.出错码入栈
	call	print_nl				! 屏幕光标回车
	mov	bp, sp						! ss:bp指向欲显示的字(word).
	call print_hex					! 显示十六进制值.
	pop	ax

	xor	dl, dl						! reset FDC ! 复位磁盘控制器,重试.
	xor	ah, ah
	int	0x13
	j	load_setup					! j即jmp指令.

ok_load_setup:

! Get disk drive parameters, specifically nr of sectors/track
! 这段代码取磁盘驱动器的参数,实际上是取每磁道扇区数,并保存在位置sectors处.
! 取磁盘驱动器参数INT 0x13调用格式和返回信息如下:
! ah = 0x08	dl = 驱动器号(如果是硬盘则要置位7为1).
! 返回信息:
! 如果出错则CF置位,并且ah = 状态码.
! ah = 0, al =0 ,	bl = 驱动器类型(AT/PS2)
! ch = 最大磁道号的低8位	cl = 每磁道最大扇区数(位0~5),最大磁道号高2位(位6~7)
! dh = 最大磁头数		dl = 驱动器数量
! es:di 软驱磁盘参数表.


	xor	dl, dl
	mov	ah, #0x08					! AH=8 is get drive parameters
	int	0x13
	xor	ch, ch
	seg cs
	! 下句保存每磁道扇区数.对软盘来说(dl=0),其最大磁道号不会超过256,ch已经足够表示它,因此cl位6~7肯定为0.又前面已置ch=0,因此此时
	! cx中是每磁道扇区数.
	mov	sectors, cx
	mov	ax, #INITSEG
	mov	es, ax						! 因为上面取磁盘参数中断改了es值,这里重新改回.

! Print some inane message
! 显示信息:"'Loading'+回车+换行",共显示包括回车和换行控制字符在内的9个字符.
! BIOS中断0x10功能号 ah = 0x03,读光标位置.
! 输入:bh = 页号
! 返回: ch = 扫描开始线; cl = 扫描结束线; dh = 行号(0x00 顶端); dl = 列号(0x00 最左边).
!
! BIOS中断0x10功能号 ah = 0z13,显示字符串.
! 输入: al = 放置光标的方式及规定属性.0x01表示使用bl中的属性值,光标停在字符串结尾处.
! es:bp此寄存器对指向要显示的字符串起始位置处.cx = 显示的字符串字符数.bh = 显示页面号;
! bl = 字符属性. dh = 行号; dl = 列号.

	mov	ah, #0x03					! read cursor pos
	xor	bh, bh						! 首先读光标位置.返回光标位置值在dx中.
	int	0x10						! dh 行(0--24); dl 列(0--79).

	mov	cx, #9						! 共显示9个字符
	mov	bx, #0x0007					! page 0, attribute 7 (normal)
	mov	bp, #msg1					! es:bp指向要显示的字符串.
	mov	ax, #0x1301					! write string, move cursor
	int	0x10						! 写字符串并移动光标到串结尾处.

! ok, we've written the message, now
! we want to load the system (at 0x10000)
! 现在开始将system模块加载到0x10000(64KB)开始处.

    mov ax, #SYSSEG
    mov es, ax           			! segment of 0x010000   ! es = 存放system的段地址.
    call read_it         			! 读磁盘上system模块,es为输入参数
    call kill_motor      			! 关闭驱动器马达,这样就可以知道驱动器状态了.
    call print_nl        			! 光标回车换行.

! After that we check which root-device to use. If the device is
! defined (!= 0), nothing is done and the given device is used.
! Otherwise, either /dev/PS0 (2,28) or /dev/at0 (2,8), depending
! on the number of sectors that the BIOS reports currently.
! 此后,我们检查要使用哪个根文件系统设备(简称根设备).如果已经指定了设备(!=0),就直接使用给定的设备.否则就需要根据BIOS报告的每磁盘
! 扇区数来确定到底使用/dev/PS0 (2,28),还是 /dev/at0 (2,8).
! 上面一行中两个设备文件的含义:
! 在Linux中软驱的主设备号是2,次设备号 = type*4 + nr,其中nr为0-3分别对应软驱A,B,C或D;type是软驱的类型(2--1.2MB或7--1.44MB等).
! 因为7*4+0=28,所以/dev/PS0 (2,28)指的是1.44MB A驱动器,其设备号是0x021c
! 同理/dev/at0 (2,8)指的是1.2MB A驱动器,其设备号是0x0208.

! 下面root_dev定义在引导扇区508,509字节处,指根文件系统所在设备号.0x0306指第2个硬盘第1个分区.这个值需要根据你自己根文件系统所在硬盘
! 和分区进行修改.例如,如果你的根文件系统在第1硬盘的第1个分区上,那么该值应该为0x0301,即(0x01,0x03).如果根文件系统是在第2个Bochs软盘
! 上,那么该值应该为0x021D,即(0x1D,0x02).


	seg cs
	mov	ax, root_dev				! 取508,509字节处的根设备号并判断是否已被定义.
	or	ax, ax
	jne	root_defined
	! 取上面保存的每磁道扇区数。如果sectors=15则说明是１.2MB的驱动器；如果sectors=18，则说明是1.44MB软驱。因为是可引导的驱动器，所以肯定是A驱。
	seg cs
	mov	bx, sectors
	mov	ax, #0x0208					! /dev/ps0 - 1.2Mb
	cmp	bx, #15
	je	root_defined
	mov	ax, #0x021c					! /dev/PS0 - 1.44Mb
	cmp	bx, #18
	je	root_defined
undef_root:
	jmp undef_root					! 如果都不一样,则死循环(死机).
root_defined:
	seg cs
	mov	root_dev, ax				! 将检查过的设备号保存到root_dev中.

! after that (everyting loaded), we jump to
! the setup-routine loaded directly after
! the bootblock:
! 到此,所有程序都加载完毕,我们就转到被加载在bootsect后面的setup程序去.
! 下面段间跳转指令.跳转到0x9020:0000(setup.s程序开始处)去执行.

	jmpi	0, SETUPSEG				! 到此本程序就结束了.

! 下面是几个子程序.read_it用于读取磁盘上的system模块.kill_motor用于关闭软驱电动机.还有一些屏幕显示子程序.

! This routine loads the system at address 0x10000, making sure
! no 64kB boundaries are crossed. We try to load it as fast as
! possible, loading whole tracks whenever we can.
!
! in:	es - starting address segment (normally 0x1000)
!
! 该子程序将系统模块加载到内存地址0x10000处,并确定没有跨越64KB的内存边界.
! 我们试图尽快地进行加载,只要可能,就每次加载整条磁道的数据.
! 输入: es 开始内存地址段值(通常是0x1000)
!
! 下面伪操作符.word定义一个2字节目标.相当于C语言程序中定义的变量和所占内存空间大小.'1+SETUPLEN'表示开始时已经读进1个引导扇区和setup程序
! 所占的扇区数SETUPLEN.

sread:	.word 1 + SETUPLEN				! sectors read of current track	! 当前磁道中已读扇区数.
head:	.word 0							! current head	! 当前磁头号
track:	.word 0							! current track	! 当前磁道号.

read_it:

! 首先测试输入的段值.从盘上读入的数据必须存放在位于内存地址64KB的边界开始处,否则进入死循环.
! 清bx寄存器,用于表示当前段内存放数据的开始位置.
! 指令test以位逻辑与两个操作数.若两个操作数对应的位都为1,则结果值的对应位为1,否则为0.该操作结果只影响标志(零标志ZF等).例如若AX=0x1000,那么
! test指令的执行结果是(0x1000 & 0x0fff) = 0x0000,于是ZF标志置位.此时即下一条指令jne条件不成立.

	mov ax, es
	test ax, #0x0fff
die:
	jne die								! es must be at 64kB boundary	! es值必须位于64KB边界!
	xor bx, bx							! bx is starting address within segment	! bx为段内偏移.
rp_read:
	! 接着判断是否已经读入全部数据.比较当前所读段是否就是系统数据末端所处的段(#ENDSEG),
	! 如果不是就跳转至下面ok1_read标号处继续读数据.否则退出子程序返回.
	mov ax, es
	cmp ax, #ENDSEG						! have we loaded all yet?	! 是否已加载了全部数据?
	jb ok1_read
	ret
ok1_read:
! 然后计算和验证当前磁道需要读取的扇区数,放在ax寄存器中.
! 根据当前磁道还未读取的扇区数以及段内数据字节开始偏移位置,计算如果全部读取这些未读扇区所读总字节数是否会超过64KB段长度的限制,若会超过,则根据此次最多
! 能读入的字节数(64KB - 段内偏移位置),反算出此次需要读取的扇区数.
	seg cs
	mov ax, sectors						! 取每磁道扇区数.
	sub ax, sread						! 减去当前磁道已读扇区数.
	mov cx, ax							! cx = ax =当前磁道未读扇区数.
	shl cx, #9							! cx = cx * 512字节 + 段内共读入字节数 = 此次操作后,段内共读入的字节数.
	add cx, bx
	jnc ok2_read
	je ok2_read
	! 若加上此次将读磁道上所有未读扇区时会超过64KB,则计算此时最多能读入的字节数: (64KB - 段内读偏移位置),
	! 再转换成需读取的扇区数.其中0减某数就是取该数64KB的补值.
	xor ax, ax
	sub ax, bx
	shr ax, #9
ok2_read:
	! 读当前磁道上指定开始扇区(cl)和需读扇区数(al)的数据到es:bx开始处.然后统计当前磁道上已经读取的扇区数并与磁道最大扇区数sectors作比较.
	! 如果小于sectors说明当前磁道上还有扇区未读.于是跳转到ok3_read处继续操作.
	call read_track						! 读当前磁道上指定开始扇区和需读扇区数的数据.
	mov cx, ax							! cx = 该次操作已读取的扇区数.
	add ax, sread						! 加上当前磁道上已经读取的扇区数.
	seg cs
	cmp ax, sectors						! 若当前磁道上还有扇区未读,则跳转到ok3_read处.
	jne ok3_read
	! 若该磁道的当前磁头面所有扇区已经读取,则读该磁道的下一磁头面(1号磁头)上的数据.如果已经完成,则去读下一磁道.
	mov ax, #1
	sub ax, head						! 判断当前磁头号
	jne ok4_read						! 如果是0磁头,则再去读1磁头面上的扇区数据.
	inc track							! 否则去读下一磁道
ok4_read:
	mov head, ax						! 保存当前磁头号
	xor ax, ax							! 清当前磁道已读扇区数.
ok3_read:
	! 如果当前磁道上还有未读扇区,则首先保存当前磁道已读扇区数,然后调整存放数据处的开始位置.若小于64KB边界值,则跳转到rp_read处,继续读数据.
	mov sread, ax						! 保存当前磁道已读扇区数.
	shl cx, #9							! 上次已读扇区数*512字节.
	add bx, cx							! 调整当前段内数据开始位置
	jnc rp_read
	! 否则说明已经读取64KB数据.此时调整当前段,为读下一段数据作准备.
	mov ax, es
	add ah, #0x10						! 将段基址调整为指向下一个64KB内存开始处.
	mov es, ax
	xor bx, bx							! 清段内数据开始偏移值.
	jmp rp_read							! 跳转至rp_read处,继续读数据.
	! read_track了程序.读当前磁道上指定开始扇区和需读扇区数的的数据到es:bx开始处.
	! al 需读扇区数;es:bx 缓冲区开始位置.

! 首先调用BIOS中断0x10,功能0x0e(以电传方式写字符),光标前移一位置.显示一个'.'.
read_track:
	pusha								! 压入所有寄存器(push all).
	pusha								! 为调用显示中断压入所有寄存器值.
	mov	ax, #0xe2e 						! loading... message 2e = .
	mov	bx, #7							! 字符前景色属性
 	int	0x10
	popa

	! 然后正式进行磁道扇区读操作.
	mov dx, track						! 取当前磁道号
	mov cx, sread						! 取当前磁道上已读扇区数
	inc cx								! cl = 开始读扇区数
	mov ch, dl							! ch = 当前磁道号
	mov dx, head						! 取当前磁头号
	mov dh, dl							! dh = 磁头号,dl = 驱动器号(为0表示当前A驱动器).
	and dx, #0x0100						! 磁头号不大于1.
	mov ah, #2							! ah = 2,读磁盘扇区功能号.

	push	dx							! save for error dump
	push	cx							! 为出错情况保存一些信息
	push	bx
	push	ax

	int 0x13
	jc bad_rt							! 若出错,则跳转至bad_rt
	add	sp, #8   						! 没有出错.因此丢弃为出错情况保存的信息.
	popa
	ret

! 读磁盘操作出错.则先显示出错信息,然后执行驱动器复位操作(磁盘中断功能号0),再跳转到read_track处重试.
bad_rt:
	push	ax							! save error code
	call	print_all					! ah = error, al = read

	xor ah, ah
	xor dl, dl
	int 0x13

	add	sp, #10							! 丢弃为出错情况保存的信息.
	popa
	jmp read_track

/*
 *	print_all is for debugging purposes.
 *	It will print out all of the registers.  The assumption is that this is
 *	called from a routine, with a stack frame like
 *	dx
 *	cx
 *	bx
 *	ax
 *	error
 *	ret <- sp
 *
*/

/*
 *
 *	子程序print_all用于调试目的.它会显示所有寄存器的内容.前提条件是需要从一个子程序中调用,并且栈帧结构如下所示
 *
 */
! 若标志寄存器的CF = 0,则不显示寄存器名称.
print_all:
	mov	cx, #5							! error code + 4 registers ! 显示值个数
	mov	bp, sp							! 保存当前栈指针sp.

print_loop:
	push	cx							! save count left	! 保存需要显示的剩余个数.
	call	print_nl					! nl for readability	! 为可读性先让光标回车换行.
	jae	no_reg							! see if register name is needed	! 若FLAGS的标志CF=0则不显示寄存器名,于是跳转.

	! 对应入栈寄存器顺序分别显示它们的名称"AX: "等.
	mov	ax, #0xe05 + 0x41 - 1			! ah = 功能号(0x0e); al = 字符(0x05 + 0x41 -1.
	sub	al, cl
	int	0x10

	mov	al, #0x58 						! X		! 显示字符'X'.
	int	0x10

	mov	al, #0x3a 						! :		! 显示字符':'.
	int	0x10

! 显示寄存器bp所指栈中内容.开始时bp指向返回地址.
no_reg:
	add	bp, #2							! next register	! 栈中下一个位置
	call	print_hex					! print it	! 以十六进制显示.
	pop	cx
	loop	print_loop
	ret

! 调用BIOS中断0x10,以电传方式显示回车换行.
print_nl:
	mov	ax, #0xe0d						! CR
	int	0x10
	mov	al, #0xa						! LF
	int 	0x10
	ret

/*
 *	print_hex is for debugging purposes, and prints the word
 *	pointed to by ss:bp in hexadecmial.
*/
/*
 *
 *	子程序print_hex用于调试目的.它使用十六进制在屏幕上显示出ss:bp指向的字.
 *
 */
! 调用BIOS中断0x10,以电传方式和4个十六进制数显示ss:bp指向的字.

print_hex:
	mov	cx, #4							! 4 hex digits		! 要显示4个十六进制数字.
	mov	dx, (bp)						! load word into dx	! 显示值放入dx中.

! 先显示高字节,因此需要把dx中值左旋4位,此时高4位在dx的低4位中.
print_digit:
	rol	dx, #4							! rotate so that lowest 4 bits are used
	mov	ah, #0xe						! 中断功能号.
	mov	al, dl							! mask off so we have only next nibble
	and	al, #0xf						! 放入al中并只取低4位(1个值).
	add	al, #0x30						! convert to 0 based digit, '0'
	cmp	al, #0x39						! check for overflow
	jbe	good_digit
	add	al, #0x41 - 0x30 - 0xa 			! 'A' - '0' - 0xa

good_digit:
	int	0x10
	loop	print_digit					! cx--.若cx>0则去显示下一个值.
	ret


/*
 * This procedure turns off the floppy drive motor, so
 * that we enter the kernel in a known state, and
 * don't have to worry about it later.
 */
/*
 * 这个子程序用于关闭软驱的马达,这样我们进入内核后就能知道它所处的状态,以后就无须担心它了.
 */
kill_motor:
	push dx
	mov dx, #0x3f2						! 软驱控制卡的数字输出寄存器端口,只写.
	xor al, al							! A驱动器,关闭FDC,禁止DMA和中断请求,关闭电动机.
	outb								! 将al中的内容输出到dx指定的端口去.
	pop dx
	ret

sectors:
	.word 0								! 存放当前启动软盘每磁道的扇区数.

msg1:									! 开机调用BIOS中断显示的信息.共9个字符.
	.byte 13,10							! 回车,换行的ASCII码.
	.ascii "Loading"

! 表示下面语句从地址508(0x1FC)开始,所以root_dev在启动扇区的第508开始的两个字节中.
.org 506
swap_dev:
	.word SWAP_DEV						! 这里存放交换系统所在设备号(init/main.c中会用)
root_dev:
	.word ROOT_DEV						! 这里存放根文件系统所在设备号(init/main.c中会用).
! 下面是启动盘且有有效引导扇区的标志.仅供BIOS中的程序加载引导扇区时识别使用.它必须位于引导扇区的最后两个字节中.
boot_flag:
	.word 0xAA55

.text
endtext:
.data
enddata:
.bss
endbss:


