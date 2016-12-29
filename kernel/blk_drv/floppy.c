/*
 *  linux/kernel/floppy.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 02.12.91 - Changed to static variables to indicate need for reset
 * and recalibrate. This makes some things easier (output_byte reset
 * checking etc), and means less interrupt jumping in case of errors,
 * so the code is hopefully easier to understand.
 */
/*
 * 02.12.91 - 修改成静态变量,以适应复位和重新校正操作.这使得某些事情做起来较为方便(output_byte复位检查等),并且意味着在出错
 * 时中断跳转要少一些,所以也希望代码能更容易被理解.
 */

/*
 * This file is certainly a mess. I've tried my best to get it working,
 * but I don't like programming floppies, and I have only one anyway.
 * Urgel. I should check for more errors, and do more graceful error
 * recovery. Seems there are problems with several drives. I've tried to
 * correct them. No promises.
 */
/*
 * 这个文件当然比较混乱.我已经尽我所能使其能够工作,但我不喜欢软驱编程,而且我也只有一个软驱.另外,我应该做更多的查错工作,以及改正
 * 更多的错误.对于某些软盘驱动器,本程序好像还存在一些问题.我已经尝试着进行纠正了,但不能保证问题已消失.
 */

/*
 * As with hd.c, all routines within this file can (and will) be called
 * by interrupts, so extreme caution is needed. A hardware interrupt
 * handler may not sleep, or a kernel panic will happen. Thus I cannot
 * call "floppy-on" directly, but have to set a special timer interrupt
 * etc.
 *
 * Also, I'm not certain this works on more than 1 floppy. Bugs may
 * abund.
 */
/*
 * 如同hd.c文件一样,该文件中的所有子程序都能够被中断调用,所以需要特别地小心.硬件中断处理程序是不能睡眠的,否则内核就会傻掉(死机).
 * 因此不能直接调用"floppy-on",而只能设置一个特殊的定时中断等.
 * 另外,我不能保证该程序能在多于1个软驱系统上工作,有可能存在错误.
 */

#include <linux/sched.h>								// 调试程序头文件,定义了任务结构task_struct,任务0数据等.
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/fdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

// 定义软驱主设备号符号常数.在驱动程序中,主设备号必须在包含blk.h文件之前被定义.
// 因为blk.h文件中要乃至这个常数值来确定一些列其他相关符号常数和宏.
#define MAJOR_NR 2										// 软驱的主设备号是2.
#include "blk.h"										// 块设备头文件.定义请求结构,块设备结构和宏函数等信息.

static int recalibrate = 0;								// 标志:1表示需要重新校正磁头位置(碰头归零道).
static int reset = 0;									// 标志:1表示需要进行复位操作.
static int seek = 0;									// 标志:1表示执行寻道操作.

// 当前数字输出寄存器DOR(Digital Output Register),定义在kernel/sched.c.该变量含有软驱操作中的重要标志,包括选择软驱,控制电机启动,
// 启动复位软盘控制器以及允许/禁止DMA和中断请求.
extern unsigned char current_DOR;

// 字节直接输出(嵌入汇编宏).把值val输出到port端口.
#define immoutb_p(val, port) \
__asm__("outb %0,%1\n\tjmp 1f\n1:\tjmp 1f\n1:"::"a" ((char) (val)), "i" (port))

// 这两个宏定义用于计算软驱的设备号.
#define TYPE(x) ((x) >> 2)								// 软驱类型(2--1.2Mb,7--1.44Mb).
#define DRIVE(x) ((x) & 0x03)							// 软驱序号(0--3对应A--D)

/*
 * Note that MAX_ERRORS=8 doesn't imply that we retry every bad read
 * max 8 times - some types of errors increase the errorcount by 2,
 * so we might actually retry only 5-6 times before giving up.
 */
/*
 * 注意,下面定义MAX_ERRORS=8并不表示对每次读错误尝试最多8次 - 有些类型的错误会把出错计数值乘2,所以我们实际上在放弃操作之前只需
 * 尝试5-6遍即可.
 */
#define MAX_ERRORS 8

/*
 * globals used by 'result()'
 */
/* 下面是函数'result()'用的全局变量 */
// 这些状态字节中各位的含义参见include/linux/fdreg.h头文件.
#define MAX_REPLIES 7								// FDC最多返回7字节的结果信息.
static unsigned char reply_buffer[MAX_REPLIES];		// 存放FDC返回的应答结果信息.
#define ST0 (reply_buffer[0])						// 结果状态字节0.
#define ST1 (reply_buffer[1])						// 结果状态字节1.
#define ST2 (reply_buffer[2])						// 结果状态字节2.
#define ST3 (reply_buffer[3])						// 结果状态字节3.

/*
 * This struct defines the different floppy types. Unlike minix
 * linux doesn't have a "search for right type"-type, as the code
 * for that is convoluted and weird. I've got enough problems with
 * this driver as it is.
 *
 * The 'stretch' tells if the tracks need to be boubled for some
 * types (ie 360kB diskette in 1.2MB drive etc). Others should
 * be self-explanatory.
 */
/*
 * 下面的软盘结构定义了不同的软盘类型.与minix不同的是,Linux没有"搜索正确的类型"-类型,因为对其处理的代码令人费解
 * 且怪怪的.本程序已经让我遇到太多的问题了.
 *
 * 对某些类型的软盘(例如在1.2MB驱动器中的360KB软盘等),"stretch"用于检测磁道是否需要特殊处理.
 */
// 定义软盘结构.软盘参数有:
// size		大小(扇区数);
// sect		每磁道扇区数;
// head		磁头数;
// track	磁道数;
// stretch	对磁道是否要特殊处理(标志);
// gap		扇区间隙长度(字节数);
// rate		数据传输速率;
// spec1	参数(高4位步进速率,低四位磁头卸载时间).
static struct floppy_struct {
	unsigned int size;						// 大小(扇区数)
	unsigned int sect;						// 每磁道扇区数
	unsigned int head;						// 磁头数
	unsigned int track;						// 磁道数
	unsigned int stretch;					// 对磁道是否要特殊处理(标志)
	unsigned char gap;						// 扇区间隙长度(字节数)
	unsigned char rate;						// 数据传输速率
	unsigned char spec1;					// 参数(高4位步进速率,低四位磁头卸载时间)
} floppy_type[] = {
	{    0, 0,0, 0,0,0x00,0x00,0x00 },	/* no testing */
	{  720, 9,2,40,0,0x2A,0x02,0xDF },	/* 360kB PC diskettes */
	{ 2400,15,2,80,0,0x1B,0x00,0xDF },	/* 1.2 MB AT-diskettes */
	{  720, 9,2,40,1,0x2A,0x02,0xDF },	/* 360kB in 720kB drive */
	{ 1440, 9,2,80,0,0x2A,0x02,0xDF },	/* 3.5" 720kB diskette */
	{  720, 9,2,40,1,0x23,0x01,0xDF },	/* 360kB in 1.2MB drive */
	{ 1440, 9,2,80,0,0x23,0x01,0xDF },	/* 720kB in 1.2MB drive */
	{ 2880,18,2,80,0,0x1B,0x00,0xCF },	/* 1.44MB diskette */
};

/*
 * Rate is 0 for 500kb/s, 2 for 300kbps, 1 for 250kbps
 * Spec1 is 0xSH, where S is stepping rate (F=1ms, E=2ms, D=3ms etc),
 * H is head unload time (1=16ms, 2=32ms, etc)
 *
 * Spec2 is (HLD<<1 | ND), where HLD is head load time (1=2ms, 2=4 ms etc)
 * and ND is set means no DMA. Hardcoded to 6 (HLD=6ms, use DMA).
 */
/*
 * 上面速率rate: 0表示500kbps,1表示300kbps,2表示250kbps.
 * 参数spec1是0xSH,其中S是步进速率(F-1ms,E-2ms,D=3ms等),H是磁头卸载时间(1=16ms,2=32ms等)
 *
 * spec2是(HLD<<1 | ND),其中HLD是磁头加载时间(1=2ms,2=4ms等)
 * ND置位表示不使用DMA(No DMA),在程序中硬编码成6(HLD=6ms,使用DMA).
 */
// 注意,上述磁头加载时间的缩写HLD最好写成标准的HLT(Head Load Time).

// floppy_interrupt()是sys_call.s程序中软驱中断处理过程标号.这里将在软盘初始化函数floppy_init()使用它初始化中断陷阱门描述符.
extern void floppy_interrupt(void);
// 这时boot/head.s处定义的临时软盘缓冲区.如果请求项的缓冲区处于内存1MB以上某个地方,则需要将DMA缓冲区设在临时缓冲区域处.因为
// 8237A芯片只能在1MB地址范围内寻址.
extern char tmp_floppy_area[1024];

/*
 * These are global variables, as that's the easiest way to give
 * information to interrupts. They are the data used for the current
 * request.
 */
/*
 * 下面是一些全局变量,因为这是将信息传给中断程序了简单的方式.它们用于当前请求项的数据.
 */
// 这些所谓的"全局变量"是指在软盘中断处理程序中调用的C函数使用的变量.当然这些C函数都在本程序内.
static int cur_spec1 = -1;									// 当前软盘参数spec1.
static int cur_rate = -1;									// 当前软盘转速rate.
static struct floppy_struct * floppy = floppy_type;			// 软盘类型结构数组指针.
static unsigned char current_drive = 0;						// 当前驱动器号.
static unsigned char sector = 0;							// 当前扇区号.
static unsigned char head = 0;								// 当前磁头号.
static unsigned char track = 0;								// 当前磁道号.
static unsigned char seek_track = 0;						// 寻道磁道号.
static unsigned char current_track = 255;					// 当前磁头所在磁道号.
static unsigned char command = 0;							// 读/写命令.
unsigned char selected = 0;									// 软驱已选定标志.在处理请求项之前要首先选定软驱.
struct task_struct * wait_on_floppy_select = NULL;			// 等待选定软驱的任务队列.

// 取消选定软驱.
// 如果函数参数指定的软驱nr当前并没有被选定,则显示警告信息.然后复位软驱已选定标志selected,并唤醒等待选择该软驱的任务.数字输出
// 寄存器(DOR)的低2位用于指定选择的软驱(0-3对应A-D).
void floppy_deselect(unsigned int nr)
{
	if (nr != (current_DOR & 3))
		printk("floppy_deselect: drive not selected\n\r");
	selected = 0;											// 复位软驱已选定标志.
	wake_up(&wait_on_floppy_select);						// 唤醒等待的任务.
}

/*
 * floppy-change is never called from an interrupt, so we can relax a bit
 * here, sleep etc. Note that floppy-on tries to set current_DOR to point
 * to the desired drive, but it will probably not survive the sleep if
 * several floppies are used at the same time: thus the loop.
 */
/*
 * floppy-change()不是从中断程序中调用的,所以这里我们可以轻松一下,睡眠等.
 * 注意floppy-on()会尝试设置current_DOR指向所需的驱动器,但当同时使用几个软盘时不能睡眠:因此此时只能使用循环方式.
 */
// 检测指定软驱中软盘更换情况.
// 参数nr是软驱号.如果软盘更换了则返回1,否则返回0.
// 该函数首先选定参数指定的软驱nr,然后测试软盘控制器的数字输入寄存器DIR的值,以判断驱动器中的软盘是否被更换过.该函数由程序
// fs/buffer.c中的check_disk_change()函数调用.
int floppy_change(unsigned int nr)
{
	// 首先要让软驱中软盘旋转起来并达到正常工作转速.这需要花费一定时间.采用的方法是利用kernel/sched.c中软盘定时函数do_floppy_timer()
	// 进行一定的延时处理.floppy_on()函数则用于判断延时是否到(mon_timer[nr]==0?),若没有到则让当前进程继续睡眠等待.若延时到则
	// do_floppy_timer()会唤醒当前进程.
repeat:
	floppy_on(nr);										// 启动并等待指定软驱nr(kernel/sched.c)
	// 在软盘启动(旋转)之后,我们来查看一下当前选择的软驱是不是函数参数指定的软驱nr.
	// 如果当前选择的软驱不是指定的软驱nr,并且已经选定了其他软驱,则让当前任务进入可中断等待状态,以等待其他软驱被取消选定.参见上面
	// floppy_deselect().如果当前没有选择其他软驱或者其他软驱被取消选定而使当前任务被唤醒时,当前软驱仍然不是指定的软驱nr,则跳转到
	// 函数开始处重新循环等待.
	while ((current_DOR & 3) != nr && selected)
		sleep_on(&wait_on_floppy_select);
	if ((current_DOR & 3) != nr)
		goto repeat;
	// 现在软盘控制器已经选定我们指定的软驱nr.于是取数字输入寄存器DIR的值,如果其最高位(位7)置位,则表示软盘已更换,此时即可关闭马达并
	// 返回1退出.否则关闭马达返回0退出.表示磁盘没有被更换.
	if (inb(FD_DIR) & 0x80) {
		floppy_off(nr);
		return 1;
	}
	floppy_off(nr);
	return 0;
}

// 复制内存缓冲块,共1024字节.
// 从内存地址from处复制1024字节数据到地址to处.
#define copy_buffer(from, to) \
__asm__("cld ; rep ; movsl" \
	::"c" (BLOCK_SIZE/4), "S" ((long)(from)), "D" ((long)(to)) \
	:)

// 设置(初始化)软盘DMA通道.
// 软盘中数据读写操作是使用DMA进行的.因此在每次进行数据传输之前需要设置DMA芯片专门上用于软驱的通道2.
static void setup_DMA(void)
{
	long addr = (long) CURRENT->buffer;				// 当前请求项缓冲区所处内存地址.

	// 首先检测请求项的缓冲区所在位置.如果缓冲区处于内存1MB以上的某个地方,则需要将DMA缓冲区设在临时缓冲区域(tmp_floppy_area)处.因为
	// 8237A芯片只能在1MB地址范围内寻址.如果是写盘命令,则还需要把数据从请求项缓冲区复制到该临时区域.
	cli();
	if (addr >= 0x100000) {
		addr = (long) tmp_floppy_area;
		if (command == FD_WRITE)
			copy_buffer(CURRENT->buffer,tmp_floppy_area);
	}
	// 接下来我们开始设置DMA通道2.在开始设置之前需要先屏蔽该通道.单通道屏蔽寄存器端口为0x0A.位0-1指定DMA通道(0--3),位2:1表示屏蔽,0
	// 表示允许请求.然后向DMA控制器端口D12和11写入方式字(读盘是0x46,写盘是0x4A).再写入传输使用缓冲区地址addr和需要传输的字节数0x3ff
	// (0--1023).最后复位对DMA通道2的屏蔽,开放DMA2请求DREQ信号.
	/* mask DMA 2 */	/* 屏蔽DMA通道2 */
	immoutb_p(4 | 2,10);
	/* output command byte. I don't know why, but everyone (minix, */
	/* sanches & canton) output this twice, first to 12 then to 11 */
	/* 输出命令字节.我是不知道为什么,但是每个(minix,*/
	/* sanches和canton)都输出两次,首先是12口,然后是11口 */
	// 下面嵌入汇编代码向DMA控制器的"清除先后触发器"端口12和方式寄存器端口11写入方式字(读盘时是0x46,写盘是0x4A).
	// 由于各通道的地址和计数寄存器都是16位的,因此在设置他们时都需要分2次进行操作.一次访问低字节,另一次访问高字节.而实际在写哪个字节则
	// 由先后触发器的状态一.当触发器为0时,则访问低字节;当字节触发器为1时,则访问高字节.每访问一次,该触发器的状态就变化一次.而写端口12就可
	// 以将触发顺置成0状态,从而对16位寄存器的设置从低字节开始.
 	__asm__("outb %%al,$12\n\tjmp 1f\n1:\tjmp 1f\n1:\t"
	"outb %%al,$11\n\tjmp 1f\n1:\tjmp 1f\n1:"::
	"a" ((char) ((command == FD_READ)?DMA_READ:DMA_WRITE)));
	/* 8 low bits of addr */	/* 地址氏0-7位 */
	// 向DMA通道2写入基/当前地址寄存器(端口4).
	immoutb_p(addr, 4);
	addr >>= 8;
	/* bits 8-15 of addr */		/* 地址高8-15位 */
	immoutb_p(addr, 4);
	addr >>= 8;
	/* bits 16-19 of addr */	/* 地址16-19位 */
	// DMA只可以在1MB内存空间内寻址,基高16-19位地址需放入页面寄存器(端口0x81).
	immoutb_p(addr, 0x81);
	/* low 8 bits of count-1 (1024-1=0x3ff) */	/* 计数器低8位(1024-1 = 0x3ff) */
	// 向DMA通道2写入基/当前字节计数值(端口5).
	immoutb_p(0xff, 5);
	/* high 8 bits of count-1 */	/* 计数器高8位 */
	// 一次共传输1024字节(两个扇区).
	immoutb_p(3, 5);
	/* activate DMA 2 */	/* 开启DMA通道2的请求 */
	immoutb_p(0 | 2, 10);
	sti();
}

// 向软驱控制器输出一个字节命令或参数.
// 在向控制器发送一个字节之前,控制器需要处于准备好状态,并且数据传输方向必须设置成从CPU到FDC,因此函数需要首先读取控制器状态信息.
// 这里使用了循环查询方式,以作适当延时.若出错,则会设置复位标志reset.
static void output_byte(char byte)
{
	int counter;
	unsigned char status;

	// 循环读取主状态控制器FD_STATUS(0x3f4)的状态.如果所读状态是STATUS_READY并且方向位STATUS_DIR = 0(CPU ->FDC),则向数据端口输出
	// 指定字节.
	if (reset)
		return;
	for(counter = 0 ; counter < 10000 ; counter++) {
		status = inb_p(FD_STATUS) & (STATUS_READY | STATUS_DIR);
		if (status == STATUS_READY) {
			outb(byte,FD_DATA);
			return;
		}
	}
	// 如果到循环1万次结束还不能发送,则置复位标志,并打印出错信息.
	reset = 1;
	printk("Unable to send byte to FDC\n\r");
}

// 读取FDC执行的结果信息.
// 结果信息最多7个字节,存放在数组reply_buffer[]中.返回读入的结果字节数,若返回值=-1,则表示出错.程序处理方式与上面函数类似.
static int result(void)
{
	int i = 0, counter, status;

	// 若复位标志已置位,则立刻退出.去执行后续程序中的复位操作.否则循环读取主状态控制器FD_STATUS(0x3f4)的状态.如果读取的控制器状态
	// 是READY,表示已经没有数据可取,则返回已读取的字节数i.如果控制器状态是方向标志置位(CPU <-FDC),已准备好,忙,表示有数据可读取.
	// 于是把控制器中的结果数据读入到应答结果数组中.最多读取MAX_REPLIES(7)个字节.
	if (reset)
		return -1;
	for (counter = 0 ; counter < 10000 ; counter++) {
		status = inb_p(FD_STATUS)&(STATUS_DIR|STATUS_READY|STATUS_BUSY);
		if (status == STATUS_READY)
			return i;
		if (status == (STATUS_DIR|STATUS_READY|STATUS_BUSY)) {
			if (i >= MAX_REPLIES)
				break;
			reply_buffer[i++] = inb_p(FD_DATA);
		}
	}
	// 如果到循环1万次结束还不能发送,则置复位标志,并打印出错信息.
	reset = 1;
	printk("Getstatus times out\n\r");
	return -1;
}

// 软盘读写出错处理函数
// 该函数根据软盘读写出错次数来确定需要采取的进一步行动.如果当前处理的请求项出错次数大于规定的最大出错次数MAX_ERRORS(8次),则
// 不再对当前请求项作进一步的操作尝试.如果读/写出错次数已经超过MAX_ERRORS/2,则需要对软驱作复位处理,于是设置复位标志reset.否则
// 若出错次数还不到最大值的一半,则只需重新校正一下磁头位置,于是设置重新校正标志recalibrate.真正的复位和重新校正处理会在后续的
// 程序中进行.
static void bad_flp_intr(void)
{
	// 首先把当前请求项出错次数增1.如果当前请求项出错次数大于最大允许出错次数,则取消选定当前软驱,并结束该请求项(缓冲区内容没有被更新).
	CURRENT->errors++;
	if (CURRENT->errors > MAX_ERRORS) {
		floppy_deselect(current_drive);
		end_request(0);
	}
	// 如果当前请求项出错次数大于在允许出错次数的一半,则置复位标志,需对软驱进行复位操作,然后再试.否则软驱需重新校正一下再试.
	if (CURRENT->errors > MAX_ERRORS / 2)
		reset = 1;
	else
		recalibrate = 1;
}

/*
 * Ok, this interrupt is called after a DMA read/write has succeeded,
 * so we check the results, and copy any buffers.
 */
/*
 * OK,下面的中断处理函数是在DMA读/写成功后调用的,这样我们就可以检查执行结果,并复制缓冲区中的数据.
 */
// 软盘读写中断调用函数.
// 该函数在软驱控制器操作结束后引发的中断处理过程中被调用.函数首先读取操作结果状态信息,据此判断操作是否出现问题并作相应处理.如果
// 读/写操作成功,那么若请求项是读操作并且其缓冲区在内存1MB以上位置,则需要把数据从软盘临时缓冲区复制到请求项的缓冲区.
static void rw_interrupt(void)
{
	// 读取FDC执行的结果信息.如果返回字节数不等于7,或者状态字节0,1或2中存在出错标志,那么若是写保护就显示出错信息,释放当前驱动器,并
	// 结束当前请求项.否则就执行出错计数处理.然后继续执行软盘请求项操作.以下状态的含义参见fdreg.h文件.
	if (result() != 7 || (ST0 & 0xd8) || (ST1 & 0xbf) || (ST2 & 0x73)) {    // 把0xf8修改0xd8
		if (ST1 & 0x02) {
			printk("Drive %d is write protected\n\r",current_drive);
			floppy_deselect(current_drive);
			end_request(0);
		} else
			bad_flp_intr();
		do_fd_request();
		return;
	}
	// 如果当前请求项的缓冲区位于1MB地址以上,则说明此次软盘读操作的内容还放在临时缓冲区内,需要复制到当前请求项的缓冲区中(因为DMA只能在
	// 1MB地址范围寻址).最后释放当前软驱(取消选定),执行当前请求项结束处理:唤醒等待该请求项的进程,唤醒等待空闲请求项的进程(若有的话),从软驱
	// 设备请求项链表中删除本请求项.再继续执行其他软盘请求项操作.
	if (command == FD_READ && (unsigned long)(CURRENT->buffer) >= 0x100000)
		copy_buffer(tmp_floppy_area,CURRENT->buffer);
	floppy_deselect(current_drive);
	end_request(1);
	do_fd_request();
}

// 设置DMA通道2并向软盘控制器输出命令和参数(输出1字节命令+0~7字节参数).
// 若reset标志没有置位,那么在该函数退出并且软盘控制器执行完相应读/写操作后就会产生一个软盘中断请求,并开始执行软盘中断处理程序.
void setup_rw_floppy(void)
{
	setup_DMA();										// 初始化软盘DMA通道.
	do_floppy = rw_interrupt;							// 置软盘中断调用函数指针.
	output_byte(command);								// 发送命令字节.
	output_byte(head<<2 | current_drive);				// 参数:磁头号 + 驱动器号.
	output_byte(track);									// 参数:磁道号.
	output_byte(head);									// 参数:磁头号.
	output_byte(sector);								// 参数:起始扇区号.
	output_byte(2);										/* sector size = 512 */	// 参数:(N=2)512字节.
	output_byte(floppy->sect);							// 参数:每磁道扇区数.
	output_byte(floppy->gap);							// 参数:扇区间隔长度.
	output_byte(0xFF);									/* sector size (0xff when n!=0 ?) */ // 参数:当N=0时,扇区定义的字节长度,这里无用.
	// 若上述任何一个output_byte()操作出错,则会设置复位标志reset.此时即会立刻去执行do_fd_request()中的复位处理代码.
	if (reset)
		do_fd_request();
}

/*
 * This is the routine called after every seek (or recalibrate) interrupt
 * from the floppy controller. Note that the "unexpected interrupt" routine
 * also does a recalibrate, but doesn't come here.
 */
/*
 * 该子程序是在每次软盘控制器寻道(或重新校正)中断中被调用的.注意"unexpected interrupt"(意外中断)子程序也会执行重新校正
 * 操作,但不在此地.
 */
// 寻道处理结束后中断过程中调用的C函数.
// 首先发送检测中断状态命令,获得状态信息ST0和磁头所在磁道信息.若出错则执行错误计数检测处理或取消本次软盘操作请求项.否则根据
// 状态信息设置当前磁道变量,然后调用函数setup_rw_floppy()设置DMA并输出软盘读写命令和参数.
static void seek_interrupt(void)
{
	// 首先发送检测中断状态命令,以获取寻道操作执行的结果.该命令不带参数.返回结果信息是两个字节:ST0和磁头当前磁道号.一读取FDC执行的
	// 结果信息.如果返回结果字节数不等于2,或者ST0不为寻道结束,或者磁头所在磁道(ST1)不等于设定磁道,则说明发生了错误.于是执行检测错误
	// 计数处理,然后继续执行软盘请求项或执行复位处理.
	/* sense drive status */	/* 检测驱动器状态 */
	output_byte(FD_SENSEI);
	if (result() != 2 || (ST0 & 0xF8) != 0x20 || ST1 != seek_track) {
		bad_flp_intr();
		do_fd_request();
		return;
	}
	// 若寻道操作成功,则继续执行当前请求项的软盘操作,即向软盘控制器发送命令和参数.
	current_track = ST1;							// 设置当前磁道.
	setup_rw_floppy();								// 设置DMA并输出软盘操作命令和参数.
}

/*
 * This routine is called when everything should be correctly set up
 * for the transfer (ie floppy motor is on and the correct floppy is
 * selected).
 */
/*
 * 该函数是在传输操作的所有信息都正确设置好后被调用的(即软驱马达已开启并且已选择了正确的软盘(软驱).
 */
// 读写数据传输函数.
static void transfer(void)
{
	// 首先检查当前驱动器参数是否就是指定驱动器的参数.若不是就发送设置驱动器参数命令及相应参数(参数1:高4位步进速率,低4位磁头卸载时间;
	// 参数2:磁头加载时间).然后判断当前数据传输速率是否与指定驱动器的一致,若不是就发送指定软驱的速率值到数据传输速率控制寄存器(FD_DCR).
	if (cur_spec1 != floppy->spec1) {				// 检测当前参数.
		cur_spec1 = floppy->spec1;
		output_byte(FD_SPECIFY);					// 发送设置磁盘参数命令.
		output_byte(cur_spec1);						/* hut etc */	// 发送参数.
		output_byte(6);								/* Head load time =6ms, DMA */
	}
	if (cur_rate != floppy->rate)					// 检测当前速率.
		outb_p(cur_rate = floppy->rate,FD_DCR);
	// 若上面任何一个output_byte()操作执行出错,则复位标志reset就会被置位.因此这里我们需要检测一下reset标志.若reset真的被置位了,就立刻
	// 去执行do_fd_requst()中的复位处理代码.
	if (reset) {
		do_fd_request();
		return;
	}
	// 如果此时寻道标志为零(即不需要寻道),则设置DMA并向软盘控制器发送相应操作命令和参数后返回.否则就执行寻道处理,于是首先置软盘中断处理
	// 调用函数为寻道中断函数.如果起始磁道号不等于零则发送磁头寻道命令和参数.所使用的参数即是第112-121行上设置的全局变量值.如果起始磁道
	// 号seek_track为0,则执行重新校正命令让磁头归零位.
	if (!seek) {
		setup_rw_floppy();							// 发送命令参数块.
		return;
	}
	do_floppy = seek_interrupt;						// 寻道中断调用的C函数.
	if (seek_track) {								// 起始磁道号.
		output_byte(FD_SEEK);						// 发送磁头寻道命令.
		output_byte(head<<2 | current_drive);		// 发送参数:磁头号+当前软驱号.
		output_byte(seek_track);					// 发送参数:磁道号.
	} else {
		output_byte(FD_RECALIBRATE);				// 发送重新校正命令(磁头归零).
		output_byte(head<<2 | current_drive);		// 发送参数:磁头号+当前软驱号.
	}
	// 同样地,若上面任何一个output_byte()操作执行出错,则复位标志reset就会被置位.若reset真的被置位了,就立刻去执行do_fd_requet()中复位
	// 处理代码.
	if (reset)
		do_fd_request();
}

/*
 * Special case - used after a unexpected interrupt (or reset)
 */
/*
 * 特殊情况 - 用于意外中断(或复位)处理后.
 */
// 软驱重新校正中断调用函数.
// 首先发送检测中断状态命令(无参数),如果返回结果表明出错,则置复位标志.否则重新校正标志清零.然后再次执行软盘请求项处理函数
// 作相应操作.
static void recal_interrupt(void)
{
	output_byte(FD_SENSEI);							// 发送检测中断状态命令.
	if (result() != 2 || (ST0 & 0xE0) == 0x60)		// 如果返回结果字节数不等于2或命令异常结束,则置复位标志.
		reset = 1;
	else
		recalibrate = 0;							// 否则复位重新校正标志
	do_fd_request();								// 作相应处理.
}

// 意外软盘中断请求引发的软盘中断处理程序中调用的函数.
// 首先发送检测中断状态命令(无参数),如果返回结果表明出错,则置复位标志,否则置重校正标志.
void unexpected_floppy_interrupt(void)
{
	output_byte(FD_SENSEI);							// 发送检测中断状态命令.
	if (result()!=2 || (ST0 & 0xE0) == 0x60)		// 如果返回结果字节数不等于2或命令异常结束,则置复位标志.
		reset = 1;
	else
		recalibrate = 1;							// 否则置重新校正标志.
}

// 软盘重新校正处理函数.
// 向软盘控制器FDC发送重新校正命令和参数,并复位重新校正标志.当软盘控制器执行完重新校正命令就会再次引发的软盘中断调用
// recal_interrupt()函数.
static void recalibrate_floppy(void)
{
	recalibrate = 0;								// 复位重新校正标志.
	current_track = 0;								// 当前磁道号归零.
	do_floppy = recal_interrupt;					// 指向重新校正中断调用的C函数.
	output_byte(FD_RECALIBRATE);					// 命令:重新校正.
	output_byte(head<<2 | current_drive);			// 参数:磁头号 + 当前驱动器号.
	// 若上任何一个output_byte()操作执行出错,则复位标志reset就会被置位.因此这里我们需要检测一下reset标志.若reset真的被
	// 置位了,就立刻去执行do_fd_requeset()中的复位处理代码.
	if (reset)
		do_fd_request();
}

// 软盘控制器FDC复位中断调用函数.
// 该函数会在向控制器发送了复位操作命令后引发的软盘中断处理程序中被调用.
// 首先发送检测中断状态命令(无参数),然后读出返回的结果字节.接着发送设定软驱参数命令和相关参数,最后再次调用请求项处理函数
// do_fd_request()去执行重新校正工作.但由于执行output_byte()操作函数出错时复位标志又会被置位,因此也可能再次去执行复位处理.
static void reset_interrupt(void)
{
	output_byte(FD_SENSEI);							// 发送检测中断状态命令.
	(void) result();								// 读取命令执行结果字节.
	output_byte(FD_SPECIFY);						// 发送设定软驱参数命令.
	output_byte(cur_spec1);							/* hut etc */	// 发送参数
	output_byte(6);									/* Head load time =6ms, DMA */
	do_fd_request();                				// 调用执行软盘请求.
}

/*
 * reset is done by pulling bit 2 of DOR low for a while.
 */
/* FDC复位是通过将数字输出寄存器(DOR)位2置0一会儿实现的 */
// 复位软盘控制器.
// 该函数首先设置参数和标志,把复位标志清0,然后把软驱变量cur_spec1和cur_rate置为无效.因为复位操作后,这两个参数就需要重新设置.接着
// 需要重新校正标志,并设置FDC执行复位操作后引发的软盘中断中调用的C函数reset_interrupt().最后把DOR寄存器位2置0一会儿以对软驱执行
// 复位操作.当前数字输出寄存器DOR的位2是启动/复位软驱位.
static void reset_floppy(void)
{
	int i;

	reset = 0;										// 复位标志置0.
	cur_spec1 = -1;									// 使无效.
	cur_rate = -1;
	recalibrate = 1;								// 重新校正标志置位.
	printk("Reset-floppy called\n\r");				// 显示执行软盘复位操作信息.
	cli();											// 关中断.
	do_floppy = reset_interrupt;					// 设置在中断处理程序中调用的函数.
	outb_p(current_DOR & ~0x04,FD_DOR);				// 对软盘控制器FDC执行复位操作.
	for (i = 0 ; i < 100 ; i++)						// 空操作,延迟.
		__asm__("nop");
	outb(current_DOR,FD_DOR);						// 再启动软盘控制器.
	sti();											// 开中断.
}

// 软驱启动定时中断调用函数.
// 在执行一个请求项要求的操作之前,为了等待指定软驱马达旋转起来到达正常的工作转速,do_fd_request()函数为准备好的当前请求项添加了一个延时
// 定时器.本函数即是该定时器到期时调用的函数.它首先检查数字输出寄存器(DOR),使其选择当前指定的驱动.然后调用执行软盘读写传输函数transfer().
static void floppy_on_interrupt(void)
{
	/* We cannot do a floppy-select, as that might sleep. We just force it */
	/* 我们不能任意设置选择的软驱,因为这可能会引起进程睡眠.我们只能迫使它自己选择 */
	// 如果当前驱动器号与数字输出寄存器DOR中的不同,则需要重新设置DOR为当前驱动器.在向数字输出寄存器输出当前DOR以后,使用定时器延迟2个滴答
	// 时间,以让命令得到执行.然后调用软盘读写传输函数transfer().若当前驱动器与DOR中的相符,那么就可以直接调用软盘读写传输函数.
	selected = 1;									// 置已选定当前驱动器标志.
	if (current_drive != (current_DOR & 3)) {
		current_DOR &= 0xFC;
		current_DOR |= current_drive;
		outb(current_DOR,FD_DOR);					// 向数字输出寄存器输出当前DOR.
		add_timer(2,&transfer);						// 添加定时器并执行传输函数.
	} else
		transfer();									// 执行软盘读写传输函数.
}

// 软盘读写请求项处理函数
// 该函数是软盘驱动程序中最主要的函数.主要作用是:1处理有复位标志或重新校正标志置位情况;2利用请求项中的设备号计算取得请求项指定软驱的
// 参数块;3利用内核定时器启动软盘读/写操作.
void do_fd_request(void)
{
	unsigned int block;

	// 首先检查是否有复位标志或重校正标志置位,若有则本函数仅执行相关标志的处理功能后就返回.如果复位标志已置位,则执行软盘复位操作并返回.
	// 如果重新校正标志已置位,则执行软盘重新校正操作并返回.
	seek = 0;										// 清寻道标志.
	if (reset) {									// 复位标志已置位.
		reset_floppy();
		return;
	}
	if (recalibrate) {								// 重新校正标志已置位.
		recalibrate_floppy();
		return;
	}
	// 本函数的真正功能从这里开始.首先利用blk.h文件中的INIT_REQUEST宏来检测请求项的合法性,如果已没有请求项则退出.然后利用请求项中的设备
	// 号取得请求项指定软驱的参数块.这个参数块将在下面用于设置软盘操作使用的全局变量参数块.请求项设备号中的软盘类型(MINOR(CURRENT->dev)>>2)
	// 被用作磁盘类型数组floppy_type[]的索引值来取得指定软驱的参数块.
	INIT_REQUEST;
	floppy = (MINOR(CURRENT->dev) >> 2) + floppy_type;
	// 下面开始设置全局变量值.如果当前驱动器号current_drive不是请求项中指定的驱动器号,则置标志seek,表示在执行读/写操作之前需要先让驱动
	// 器执行寻道处理.然后把当前驱动器号设置为请求项中指定的驱动器号.
	if (current_drive != CURRENT_DEV)				// CURRENT_DEV是请求项中指定的软驱号.
		seek = 1;
	current_drive = CURRENT_DEV;
	// 设置读写起始扇区block.因为每次读写是以块为单位(1块为2个扇区),所以起始扇区需要起码比磁盘总扇区数小2个扇区.否则说明这个请求 参数无效,
	// 结束该次软盘请求项去执行下一个请求项.
	block = CURRENT->sector;						// 取当前软盘请求项中起始扇区号.
	if (block + 2 > floppy->size) {					// 如果block + 2大于磁盘扇区总数,则结束本人软盘请求项.
		end_request(0);
		goto repeat;
	}
	// 再求对应在磁道上的扇区号,磁头号,磁道号,搜寻磁道号(对于软驱读不同格式的盘).
	sector = block % floppy->sect;					// 起始扇区对每磁道扇区数取模,得磁道上扇区号.
	block /= floppy->sect;							// 起始扇区对每磁道扇区数取整,得起始磁道数.
	head = block % floppy->head;					// 起始磁道数对磁头数取模,得操作的磁头号.
	track = block / floppy->head;					// 起始磁道数对磁头数取整,得操作的磁道号.
	seek_track = track << floppy->stretch;			// 相应于软驱中盘类型进行调整,得寻道号.
	// 再看看是否还需要首先执行寻道操作.如果寻道号与当前磁头所在磁道号不同,则需要进行寻道操作,于是置需要寻道标志seek.最后我们设置执行的软盘
	// 命令command.
	if (seek_track != current_track)
		seek = 1;
	sector++;										// 磁盘上实际扇区计数是从1算起.
	if (CURRENT->cmd == READ)						// 如果请求项是读操作,则置读命令码.
		command = FD_READ;
	else if (CURRENT->cmd == WRITE)					// 如果请求项是写操作,则置写命令码.
		command = FD_WRITE;
	else
		panic("do_fd_request: unknown command");
	// 在上面设置好所有全局变量值之后,我们可以开始执行请求项操作了.该操作利用定时器来启动.因为为了能对软驱进行读写操作,需要首先启动驱动器马达
	// 并达到正常运转速度.而这需要一定的时间.因此这里利用ticks_to_floppy_on()来计算启动延时时间,然后使用该延时设定一个定时器.当时间到时就调用
	// 函数floppy_on_interrupt().
	add_timer(ticks_to_floppy_on(current_drive), &floppy_on_interrupt);
}

// 各种类型软驱磁盘有的数据块总数.
static int floppy_sizes[] ={
	   0,   0,   0,   0,
	 360, 360, 360, 360,
	1200, 1200, 1200, 1200,
	 360, 360, 360, 360,
	 720, 720, 720, 720,
	 360, 360, 360, 360,
	 720, 720, 720, 720,
	1440, 1440, 1440, 1440
};

// 软盘系统初始化.
// 设置软盘块设备请求项的处理函数do_fd_request(),并设置软盘中断门(int 0x26,对应硬件中断请求信号IRQ6).然后取消对该中断信号的屏蔽,以
// 允许软盘控制器FDC发送中断请求信号.中断描述符表IDT中陷阱门描述符设置宏set_trap_gate()定义在头文件include/asm/system.h中.
void floppy_init(void)
{
	// 设置软盘中断门描述符。floppy_interrup(kernel/sys_call.s)是其中断处理过程。
	blk_size[MAJOR_NR] = floppy_sizes;
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;  						// = do_fd_request()。
	set_trap_gate(0x26, &floppy_interrupt);          						// 设置陷阱门描述符。
	outb(inb_p(0x21) & ~0x40, 0x21);                   						// 复位软盘中断请求屏蔽位。
}

