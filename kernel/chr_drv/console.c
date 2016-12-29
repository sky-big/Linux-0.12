/*
 *  linux/kernel/console.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *	console.c
 *
 * This module implements the console io functions
 *	'void con_init(void)'
 *	'void con_write(struct tty_queue * queue)'
 * Hopefully this will be a rather complete VT102 implementation.
 *
 * Beeping thanks to John T Kohl.
 *
 * Virtual Consoles, Screen Blanking, Screen Dumping, Color, Graphics
 *   Chars, and VT100 enhancements by Peter MacDonald.
 */
/*
 *
 *	console.c
 * 该模块实现控制台输入输出功能
 *	'void con_init(void)'
 *	'void con_write(struct tty_queue * queue)'
 * 希望这是一个非常完整的VT102实现.
 *
 * 感谢John T. Kohl实现了蜂鸣指示子程序.
 *
 * 虚拟控制台,屏幕黑屏处理,屏幕拷贝,彩色处理,图形字符显示以及VT100终端增强操作由Peter MacDonald编制.
 */

/*
 *  NOTE!!! We sometimes disable and enable interrupts for a short while
 * (to put a word in video IO), but this will work even for keyboard
 * interrupts. We know interrupts aren't enabled when getting a keyboard
 * interrupt, as we use trap-gates. Hopefully all is well.
 */
/*
 * 注意!!!我们有时短暂地禁止和允许中断(当输出一个字(word)到视频IO),但即使对于键盘中断这也是可以工作的.因为我们使用了陷阱门
 * 所以我们知道在处理一个键盘过程期间中断是被禁止的.希望一切均正常.
 */

/*
 * Code to check for different video-cards mostly by Galen Hunt,
 * <g-hunt@ee.utah.edu>
 */
/*
 * 检测不同显示卡的大多数代码是Galen Hunt编写的.
 * <g-hunt@ee.utah.edu>
 */

#include <linux/sched.h>
#include <linux/tty.h>										// tty头文件,定义有关tty_io,串行通信方面的参数,常数.
//#include <linux/config.h>
//#include <linux/kernel.h>

#include <asm/io.h>											// io头文件.定义硬件端口输入/输出宏汇编语句.
#include <asm/system.h>
#include <asm/segment.h>

//#include <string.h>
#include <errno.h>

// 该符号常量定义终端IO结构的默认数据.其中符号常数请参照include/termios.h文件.
#define DEF_TERMIOS \
(struct termios) { \
	ICRNL, \
	OPOST | ONLCR, \
	0, \
	IXON | ISIG | ICANON | ECHO | ECHOCTL | ECHOKE, \
	0, \
	INIT_C_CC \
}


/*
 * These are set up by the setup-routine at boot-time:
 */
/*
 * 这些是setup程序在引导启动系统时设置的参数:
 */

#define ORIG_X				(*(unsigned char *)0x90000)							// 初始光标列号
#define ORIG_Y				(*(unsigned char *)0x90001)							// 初始光标行号.
#define ORIG_VIDEO_PAGE		(*(unsigned short *)0x90004)						// 初始显示页面.
#define ORIG_VIDEO_MODE		((*(unsigned short *)0x90006) & 0xff)				// 显示模式.
#define ORIG_VIDEO_COLS 	(((*(unsigned short *)0x90006) & 0xff00) >> 8)		// 屏幕列数.
#define ORIG_VIDEO_LINES	((*(unsigned short *)0x9000e) & 0xff)				// 屏幕行数.
#define ORIG_VIDEO_EGA_AX	(*(unsigned short *)0x90008)						//
#define ORIG_VIDEO_EGA_BX	(*(unsigned short *)0x9000a)						// 显示内存大小和色彩模式.
#define ORIG_VIDEO_EGA_CX	(*(unsigned short *)0x9000c)						// 显示卡特性参数.

#define VIDEO_TYPE_MDA		0x10	/* Monochrome Text Display	*/				/* 单色文本 */
#define VIDEO_TYPE_CGA		0x11	/* CGA Display 			*/					/* CGA显示器 */
#define VIDEO_TYPE_EGAM		0x20	/* EGA/VGA in Monochrome Mode	*/			/* EGA/VGA单色 */
#define VIDEO_TYPE_EGAC		0x21	/* EGA/VGA in Color Mode	*/				/* EGA/VGA彩色 */

#define NPAR 16																	// 转义字符序列中最大参数个数.

int NR_CONSOLES = 0;															// 系统实际支持的虚拟控制台数量.

extern void keyboard_interrupt(void);

// 以这些静态变量是本文件函数中使用的一些全局变量.
static unsigned char	video_type;			/* Type of display being used	*/	// 使用的显示类型.
static unsigned long	video_num_columns;	/* Number of text columns	*/		// 屏幕文本列数.
static unsigned long	video_mem_base;		/* Base of video memory		*/		// 物理显示内存基地址.
static unsigned long	video_mem_term;		/* End of video memory		*/		// 物理显示内存末端地址
static unsigned long	video_size_row;		/* Bytes per row		*/			// 屏幕每行使用的字节数.
static unsigned long	video_num_lines;	/* Number of test lines		*/		// 屏幕文本行数.
static unsigned char	video_page;			/* Initial video page		*/		// 初试显示页面.
static unsigned short	video_port_reg;		/* Video register select port	*/	// 显示控制选择寄存器端口.
static unsigned short	video_port_val;		/* Video register value port	*/	// 显示控制数据寄存器端口.
static int can_do_colour = 0;				// 标志: 可使用彩色功能.

// 虚拟控制台结构.其中包含一个虚拟控制台的当前所有信息.其中vc_origin和vc_scr_end是当前正在处理的虚拟控制台执行快速滚屏操作时使用的
// 起始行和末行对应的显示内存位置.vc_video_mem_start和vc_video_meme_end是当前虚拟控制台使用的显示内存区域部分.
static struct {
	unsigned short	vc_video_erase_char;										// 擦除字符属性及字符(0x0720)
	unsigned char	vc_attr;													// 字符属性
	unsigned char	vc_def_attr;												// 默认字符属性.
	int				vc_bold_attr;												// 粗体字符属性.
	unsigned long	vc_ques;													// 问号字符.
	unsigned long	vc_state;													// 处理转义或控制序列的当前状态.
	unsigned long	vc_restate;													// 处理转义或控制序列的下一状态.
	unsigned long	vc_checkin;
	unsigned long	vc_origin;													/* Used for EGA/VGA fast scroll	*/
	unsigned long	vc_scr_end;													/* Used for EGA/VGA fast scroll	*/
	unsigned long	vc_pos;														// 当前光标对应的显示内存位置.
	unsigned long	vc_x, vc_y;													// 当前光标列,行值.
	unsigned long	vc_top, vc_bottom;											// 滚动时顶行行号;底行行号.
	unsigned long	vc_npar, vc_par[NPAR];										// 转义序列参数个数和参数数组.
	unsigned long	vc_video_mem_start;											/* Start of video RAM		*/
	unsigned long	vc_video_mem_end;											/* End of video RAM (sort of)	*/
	unsigned int	vc_saved_x;													// 保存的光标列号.
	unsigned int	vc_saved_y;													// 保存的光标行号.
	unsigned int	vc_iscolor;													// 彩色显示标志.
	char *			vc_translate;												// 使用的字符集.
} vc_cons [MAX_CONSOLES];

// 为人便于引用,以下定义当前正在处理控制台信息的符号.含义同上.其中currcons是使用vc_cons[]结构的函数参数中的当前虚拟终端号.
#define origin					(vc_cons[currcons].vc_origin)					// 快速滚屏操作起始内存位置.
#define scr_end					(vc_cons[currcons].vc_scr_end)					// 快速滚屏操作末端内存位置.
#define pos						(vc_cons[currcons].vc_pos)
#define top						(vc_cons[currcons].vc_top)
#define bottom					(vc_cons[currcons].vc_bottom)
#define x						(vc_cons[currcons].vc_x)
#define y						(vc_cons[currcons].vc_y)
#define state					(vc_cons[currcons].vc_state)
#define restate					(vc_cons[currcons].vc_restate)
#define checkin					(vc_cons[currcons].vc_checkin)
#define npar					(vc_cons[currcons].vc_npar)
#define par						(vc_cons[currcons].vc_par)
#define ques					(vc_cons[currcons].vc_ques)
#define attr					(vc_cons[currcons].vc_attr)
#define saved_x					(vc_cons[currcons].vc_saved_x)
#define saved_y					(vc_cons[currcons].vc_saved_y)
#define translate				(vc_cons[currcons].vc_translate)
#define video_mem_start			(vc_cons[currcons].vc_video_mem_start)			// 使用显存的起始位置.
#define video_mem_end			(vc_cons[currcons].vc_video_mem_end)			// 使用显存的末端位置.
#define def_attr				(vc_cons[currcons].vc_def_attr)
#define video_erase_char  		(vc_cons[currcons].vc_video_erase_char)
#define iscolor					(vc_cons[currcons].vc_iscolor)

int blankinterval = 0;															// 设定的屏幕黑屏间隔时间.
int blankcount = 0;																// 黑屏时间计数.

static void sysbeep(void);														// 系统蜂鸣函数

/*
 * this is what the terminal answers to a ESC-Z or csi0c
 * query (= vt100 response).
 */
/*
 * 下面是终端回应ESC-Z或csi0c请求的应答(=vt100)响应
 */
// csi - 控制序列引导码(Control Sequence Introducer).
// 主机通过发送不带参数或参数是0的设置属性(DA)控制序列('ESC [c'或'ESC [0c']要求终端应答一个设备属性控制序列(ESC Z的作用与此相同)
// 终端则发送以下序列来响应主机.该序列(即'ESC [?1;2c')表示终端是具有高级视频功能的VT100兼容终端.
#define RESPONSE "\033[?1;2c"

// 定义使用的字符集.其中上半部分时普通7位ASCII码,即US字符集.下半部分对应VT100终端设备中的线条字符,即显示图表线条的字符集.
static char * translations[] = {
/* normal 7-bit ascii */
	" !\"#$%&'()*+,-./0123456789:;<=>?"
	"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
	"`abcdefghijklmnopqrstuvwxyz{|}~ ",
/* vt100 graphics */
	" !\"#$%&'()*+,-./0123456789:;<=>?"
	"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^ "
	"\004\261\007\007\007\007\370\361\007\007\275\267\326\323\327\304"
	"\304\304\304\304\307\266\320\322\272\363\362\343\\007\234\007 "
};

#define NORM_TRANS (translations[0])
#define GRAF_TRANS (translations[1])

// 跟踪光标当前位置.
// 参数: currcons - 当前虚拟终端号;new_x - 光标所在列号;new_y - 光标所在行号.
// 更新当前光标位置变量x,y,并修正光标在显示内存中的对应位置pos.该函数会首先检查参数的有效性.如果给定的光标列号超出显示器最大列数,或者
// 光标行号不低于显示的最大行数,则退出.否则就更新当前光标变量和新光标位置对应在显示内存中位置pos.
// 注意,函数中的所有变量实际上是vc_cons[currcons]结构中的相应字段.以下函数相同.
/* NOTE! gotoxy thinks x==video_num_columns is ok */
/* 注意!gotoxy函数认为x==video_num_columns时是正确的 */
static inline void gotoxy(int currcons, int new_x, unsigned int new_y)
{
	if (new_x > video_num_columns || new_y >= video_num_lines)
		return;
	x = new_x;
	y = new_y;
	pos = origin + y * video_size_row + (x << 1);	// 1列用2个字节表示,所以x<<1.
}

// 设置滚屏起始显示内存地址.
static inline void set_origin(int currcons)
{
	// 首先判断显示卡类型.对于EGA/VGA,我们可以指定屏内范围(区域)进行滚屏操作,而MDA单色显示卡只能进行整屏滚屏操作.因此只有EGA/VGA卡才需要设置
	// 滚屏起始行显示内存地址(起始行是origin对应的行).即显示类型如果不是EGA/VGA彩色模式,也不是EGA/VGA单色模式,那么就直接返回.另外,我们只对前
	// 台控制台进行操作,因此当前控制台currocons必须是前台控制台时,我们才需要设置其滚屏起始行对应的内存起点位置.
	if (video_type != VIDEO_TYPE_EGAC && video_type != VIDEO_TYPE_EGAM)
		return;
	if (currcons != fg_console)
		return;
	// 然后向显示寄存器选择端口video_port_reg输出12,即选择显示控制数据寄存器r12,接着写入滚屏起始地址高字节.其中向右移动9位,实际上表示向右移动
	// 8位再除以2(上1个字符用2字节表示).再选择显示控制数据寄存器r13,然后写入滚屏起始地址低字节.向右移动1位表示除以2,同样代表屏幕上1个字符用2字节
	// 表示.输出值相对于默认显示内存起始位置video_mem_base进行操作.
	// 例如对于EGA/VGA彩色模式,viedo_mem_base = 物理内存地址0xb8000.
	cli();
	outb_p(12, video_port_reg);											// 选择数据寄存器r12,输出滚屏起始位置高字节.
	outb_p(0xff & ((origin - video_mem_base) >> 9), video_port_val);
	outb_p(13, video_port_reg);											// 选择数据寄存器r13,输出滚屏起始位置低字节.
	outb_p(0xff & ((origin - video_mem_base) >> 1), video_port_val);
	sti();
}

// 向上卷动上行
// 将屏幕滚动窗口向上移动一行,并在屏幕滚动区域底出现的新行上添加空格字符.滚屏区域必须大于1行.
static void scrup(int currcons)
{
	// 滚屏区域必须至少有2行.如果滚屏区域顶行号大于等于区域底行号,则不满足进行滚行操作的条件.另外,对于EGA/VGA卡,我们可以指定屏内行范围(区域)
	// 进行滚屏操作,而MDA单色显示卡只能进行整屏操作.该函数对EGA和MDA显示类型进行分别处理.如果显示类型是EGA,则还分为整屏窗口移动和区域内窗口移动
	// 这里首先处理显示卡是EGA/VGA显示类型的情况.
	if (bottom <= top)
		return;
	if (video_type == VIDEO_TYPE_EGAC || video_type == VIDEO_TYPE_EGAM)
	{
		// 如果移动起始行top=0,移动最底行bottom = video_num_lines = 25,则表示整屏窗口向下移动,于是把整个屏幕窗口左上角对应的起始内存位置origin
		// 调整为向下移动一行对应的内存位置,同时也跟踪调整当前光标对应的内存位置以及屏幕末行末端字符指针scr_end的位置.最后把新屏幕窗口内存起始位置值
		// origin写入显示控制器中
		if (!top && bottom == video_num_lines) {
			origin += video_size_row;
			pos += video_size_row;
			scr_end += video_size_row;
			// 如果屏幕窗口末端所对应的显示内存指针scr_end超出了实际显示内存末端,则将屏幕内容除第一行以外所有行对应的内存数据移动到显示内存的起始位置video_mem_start
			// 处,并在整屏窗口向下移动出现的新行上填入空格字符.然后根据屏幕内存数据移动后的情况,重新调整当前屏幕对应内存的起始指针,光标位置指针和屏幕末端
			// 对应内存指针scr_end.
			if (scr_end > video_mem_end) {
				// 这段嵌入汇编程序首先将(屏幕字符行数 - 1)行对应的内存数据移动到显示内存起始位置video_mem_start处,然后在随后的内存位置处添加一行空格(擦除)
				// 字符数据.
				// %0 - eax(擦除字符+属性);%1 - ecx((屏幕字符行数-1)*所对应的字符数/2,以长字移动);
				// %2 - edi(显示内存起始位置video_mem_start); %3 - esi(屏幕窗口内存起始位置origin).
				// 移动方向:[edi] -> [esi],移动ecx个长字.
				__asm__("cld\n\t"											// 清方向位
					"rep\n\t"												// 重复操作,将当前屏幕内存数据移动到显示内存起始处
					"movsl\n\t"
					"movl video_num_columns, %1\n\t"
					"rep\n\t"												// 在新行上填入空格字符
					"stosw"
					::"a" (video_erase_char),
					"c" ((video_num_lines - 1) * video_num_columns >> 1),
					"D" (video_mem_start),
					"S" (origin)
					:);
				// 重新设置快速滚屏的末端位置
				scr_end -= origin - video_mem_start;
				// 设置当前显示位置
				pos -= origin - video_mem_start;
				// 重新设置快速滚屏的起始位置
				origin = video_mem_start;
			// 如果调整后的屏幕末端对应的内存指针scr_end没有超出显示内存的末端video_mem_end,则只需在新行上填入擦除字符(空格字符).
			// %0 - eax(擦除字符+属性);%1 - ecx(屏幕行数);%2 - edi(最后1行开始处对应内存位置);
			} else {
				__asm__("cld\n\t"
					"rep\n\t"												// 重复操作,在新出现现上填入擦除字符(空格字符).
					"stosw"
					::"a" (video_erase_char),
					"c" (video_num_columns),
					"D" (scr_end - video_size_row)
					:);
			}
			// 然后把新屏幕滚动窗口内存起始位置值origin写入显示控制器中.
			set_origin(currcons);
		// 否则表示不是整屏移动.即表示从指定行top开始到bottom区域中的所有行向上移动1行,指定行top被删除.此时直接将屏幕从指定行top到屏幕末端
		// 所有行对应的显示内存数据向上移动1行,并在最下面新出现的行上填入擦除字符.
		// %0 - eax(擦除字符+属性);%1 - ecx(top行下1行开始到bottom行所对应的内存长字数);
		// %2 - edi(top行所处的内存位置); %3 - esi(top+1行所处的内存位置).
		} else {
			__asm__("cld\n\t"
				"rep\n\t"													// 循环操作,将top+1到bottom行所对应的内存块移到top行开始处.
				"movsl\n\t"
				"movl video_num_columns, %%ecx\n\t"
				"rep\n\t"													// 在新行上填入擦除字符.
				"stosw"
				::"a" (video_erase_char),
				"c" ((bottom - top - 1) * video_num_columns >> 1),
				"D" (origin + video_size_row * top),
				"S" (origin + video_size_row * (top + 1))
				:);
		}
	}
	// 如果显示类型不是EGA(而是MDA),则执行下面移动操作.因为MDA显示控制卡只能整屏滚动,并且会自动调整超出显示范围的情况,即会自动翻巻指针,所以这里不对与屏幕
	// 内容相对应内存超出显示内存的情况单独处理处理方法与EGA非整屏移动情况完全一样.
	else		/* Not EGA/VGA */
	{
		__asm__("cld\n\t"
			"rep\n\t"
			"movsl\n\t"
			"movl video_num_columns, %%ecx\n\t"
			"rep\n\t"
			"stosw"
			::"a" (video_erase_char),
			"c" ((bottom - top - 1) * video_num_columns >> 1),
			"D" (origin + video_size_row * top),
			"S" (origin + video_size_row * (top + 1))
			:);
	}
}

// 向下卷动一行
// 将屏幕滚动窗口向上移动一行,相应屏幕滚动区域内容向下移动1行.并在移动开始行的上方出现一新行.处理方法与scrup()相似,只是为了在
// 移动显示内存数据时不会出现数据覆盖的问题,复制操作是以逆向进行的,即先从屏幕倒数第2行的最后一个字符开始复制到最后一行,再将倒数第3行
// 复制到倒数第2行,等等.因为此时对EGA/VGA显示类型和MDA类型的处理过程完全一样,所以该函数实际上没有必要写两段相同的代码.即这里if和
// else语句块中的操作完全一样.
static void scrdown(int currcons)
{
	// 同样,滚屏区域必须至少有2行.如果滚屏区域顶行号大于等于区域底行号,则不满足进行滚行操作的条件.另外,对于EGA/VGA卡,我们可以指定屏内行范围(区域)
	// 进行滚屏操作,而MDA单色显示卡只能进行整屏操作.由于窗口向上移动最多移动以当前控制台实际显示内存末端的情况,所以这里只需要处理普通的内存数据
	// 移动情况.
	if (bottom <= top)
		return;
	if (video_type == VIDEO_TYPE_EGAC || video_type == VIDEO_TYPE_EGAM)
	{
		// %0 - eax(擦除字符+属性);%1 - ecx(top行到bottom-1行所对应的内存长字数);
		// %2 - edi(窗口右下角最后一个字长位置); %3 - esi(窗口倒数第2行最后一个长字位置).
		__asm__("std\n\t"										// 置方向位!!
			"rep\n\t"											// 重复操作,向下移动从top行到bottom-1行对应的内存数据
			"movsl\n\t"
			"addl $2, %%edi\n\t"								/* %edi has been decremented by 4 */ /* %edi已减4,因也是反向填擦除字符 */
			"movl video_num_columns, %%ecx\n\t"
			"rep\n\t"											// 将擦除字符填入上方新行中.
			"stosw"
			::"a" (video_erase_char),
			"c" ((bottom - top - 1) * video_num_columns >> 1),
			"D" (origin + video_size_row * bottom - 4),
			"S" (origin + video_size_row * (bottom - 1) - 4)
			:);
	}
	// 如果不是EGA显示类型,则执行以下操作(与上面完成一样).
	else														/* Not EGA/VGA */
	{
		__asm__("std\n\t"
			"rep\n\t"
			"movsl\n\t"
			"addl $2, %%edi\n\t"									/* %edi has been decremented by 4 */
			"movl video_num_columns, %%ecx\n\t"
			"rep\n\t"
			"stosw"
			::"a" (video_erase_char),
			"c" ((bottom - top - 1) * video_num_columns >> 1),
			"D" (origin + video_size_row * bottom - 4),
			"S" (origin + video_size_row * (bottom - 1) - 4)
			:);
	}
}

// 光标在同列位置下移一行.
// 如果光标没有处在最后一行,则直接修改光标当前行变量y++,并调整光标对应显示内存位置pos(加上一行字符所对应的内存长度).否则
// 需要将屏幕窗口内容上移一行.
// 函数名称lf(line feed 换行)是指处理控制字符LF.
static void lf(int currcons)
{
	if (y + 1 < bottom) {
		y++;
		pos += video_size_row;							// 加上屏幕一行占用内存的字节数.
		return;
	}
	scrup(currcons);									// 将屏幕窗口内容上移一行.
}

// 光标在同列上移一行
// 如果光标不在屏幕第一行上,则直接修改光标当前标量y--,并调整光标对应显示内存位置pos,减去屏幕上一行字符所对应的内存长度字节数.
// 否则需要将屏幕窗口内容下移一行.
// 函数名称ri(reverse index 反向索引)是指控制字符RI或转义序列"ESC M".
static void ri(int currcons)
{
	if (y > top) {
		y--;											// 减去屏幕一行占用内存的字节数
		pos -= video_size_row;
		return;
	}
	scrdown(currcons);									// 将屏幕窗口内容下移一行
}

// 光标回到第1列(0列).
// 调整光标对应内存位置pos.光标所在列号*2即是0列到光标所在列对应的内存字节长度.
// 函数名称cr(carriage return回车)指明处理的控制字符的回车.
static void cr(int currcons)
{
	pos -= x << 1;										// 减去0列到光标处占用的内存字节数.
	x = 0;
}

// 擦除光标前一字符(用空格替代)(del -delete 删除)
// 如果光标没有处在0列,则将光标对应内存位置pos后退2字节(对应屏幕上一个字符),然后将当前光标变量列值减1,并将光标所在位置处字符符擦除
static void del(int currcons)
{
	if (x) {
		pos -= 2;
		x--;
		*(unsigned short *)pos = video_erase_char;
	}
}

// 删除屏幕上与光标位置相关的部分
// ANSI控制序列: 'ESC [ Ps J'(Ps = 0 - 删除光标处到屏幕底端;1 - 删除屏幕开始到光标处;2 - 整屏删除).本函数根据指定的控制序列
// 具体参数值,执行与光标位置的删除操作,并且在擦除字符或行时光标位置不变.
// 函数名称csi_J(CSI - Control Sequence Introducer,即控制序列引导码)指明对控制序列"CSI Ps J"进行处理.
// 参数:vpar - 对应上面控制序列中Ps的值.
static void csi_J(int currcons, int vpar)
{
	long count;
	long start;

	// 首先根据三种情况分别设置需要删除的字符数和删除开始的显示内存位置.
	switch (vpar) {
		case 0:												/* erase from cursor to end of display */
			count = (scr_end - pos) >> 1;					/* 擦除光标到屏幕底端所有字符 */
			start = pos;
			break;
		case 1:												/* erase from start to cursor */
			count = (pos - origin) >> 1;					/* 删除从屏幕开始到光标处的字符 */
			start = origin;
			break;
		case 2: 											/* erase whole display */
			count = video_num_columns * video_num_lines;	/* 删除整个屏幕上的所有字符 */
			start = origin;
			break;
		default:
			return;
	}
	// 然后使用擦除字符填写被删除字符的地方.
	// %0 - ecx(删除的字符数count);%1 - edi(删除操作开始的地址);%2 - eax(填入的擦除字符).
	__asm__("cld\n\t"
		"rep\n\t"
		"stosw\n\t"
		::"c" (count),
		"D" (start),"a" (video_erase_char)
		:);
}

// 删除上一行上与光标位置相关的部分.
// ANSI转义字符序列:'ESC [ Ps K'(Ps = 0删除到行尾;1 从开始删除;2 整行都删除).本函数根据参数擦除光标所在行的部分或所有字符.擦除操作从屏幕上
// 移走字符但不影响其他字符.擦除的字符被丢弃.在擦除字符或行时光标位置不变.
// 参数:par - 对应上面控制序列中Ps的值.
static void csi_K(int currcons, int vpar)
{
	long count;
	long start;

	// 首先根据三种情况分别设置需要删除的字符数和删除开始的显示内存位置.
	switch (vpar) {
		case 0:													/* erase from cursor to end of line */
			if (x >= video_num_columns)							/* 删除光标到行尾所有字符 */
				return;
			count = video_num_columns - x;
			start = pos;
			break;
		case 1:													/* erase from start of line to cursor */
			start = pos - (x << 1);								/* 删除从行开始到光标处 */
			count = (x < video_num_columns) ? x : video_num_columns;
			break;
		case 2: 												/* erase whole line */
			start = pos - (x << 1);								/* 将整行字符全删除 */
			count = video_num_columns;
			break;
		default:
			return;
	}
	// 然后使用擦除字符填写删除字符的地方.
	// %0 - ecx(删除字符数count);%1 - edi(删除操作开始地址);%2 - eax(填入的擦除字符).
	__asm__("cld\n\t"
		"rep\n\t"
		"stosw\n\t"
		::"c" (count),
		"D" (start),"a" (video_erase_char)
		:);
}

// 设置显示字符属性
// ANSI转义序列:'ESC [ Ps;PS m'.Ps = 0 - 默认属性;1 - 粗体并增亮;4 - 下划线;5 - 闪烁;7 - 反显;22 - 非粗体;24 - 无下划线;
// 25 - 无闪烁;27 - 正显;30~38 - 设置前景色彩;39 - 默认前景色(White);40~48 - 设置背景色彩;49 - 默认背景色(Black).
// 该控制序列根据参数设置字符显示属性.以后所有发送到终端的字符都将使用这里指定的属性,直到再次执行本控制序列重新设置字符显示的属性.
void csi_m(int currcons)
{
	int i;

	// 一个序列中可以带有多个不同参数.参数存储在数组par[]中.下面就根据接收到的参数个数npar,循环处理各个参数Ps.
	// 如果Ps = 0,则把当前虚拟控制台随后显示的字符属性设置为默认属性def_attr.初始化时def_attr已被设置成0x07(黑底白字).
	// 如果Ps = 1,则把当前虚拟控制台随后显示的字符属性设置为粗体或增亮显示.如果是彩色显示,则把字符属性或上0x08让字符高亮度显示;如果是单色显示,
	// 则让人带下划线显示.
	// 如果ps = 4,则对彩色和单色显示进行不同的处理.若此时不是彩色显示方式,则让字符带下划线显示.如果是彩色显示,那么若原来vc_bold_attr不等于-1
	// 时就复位其背景色;否则的话就把背景色取反.若取反后前景色与背景色相同,就把前景色增1而取另一种颜色.
	for (i = 0; i <= npar; i++)
		switch (par[i]) {
			case 0:
				attr = def_attr; break;  									/* default */
			case 1:
				attr = (iscolor ? attr | 0x08 : attr | 0x0f); break;  		/* bold */
			/*case 4: attr=attr|0x01;break;*/  /* underline */
			case 4: 														/* bold */
			  if (!iscolor)
			    attr |= 0x01;												// 单色则带下划线显示.
			  else
			  { 															/* check if forground == background */
			    if (vc_cons[currcons].vc_bold_attr != -1)
			      attr = (vc_cons[currcons].vc_bold_attr & 0x0f) | (0xf0 & (attr));
			    else
			    {
			    	short newattr = (attr&0xf0)|(0xf&(~attr));
			      	attr = ((newattr&0xf)==((attr>>4)&0xf)?
			        (attr&0xf0)|(((attr&0xf)+1)%0xf):
			        newattr);
			    }
			  }
			  break;
			// 如果Ps = 5,则把当前虚拟控制台随后显示的字符设置为闪烁,即把属性字节位7置1.
			case 5: attr = attr | 0x80; break;  							/* blinking */
			// 如果Ps = 7,则把当前虚拟控制台随后显示的字符设置为反显,即把前景色和背景色交换.
			case 7: attr = (attr << 4) | (attr >> 4); break;  				/* negative */
			// 如果Ps = 22,则取消随后字符的高亮度显示(取消粗体显示).
			case 22: attr = attr & 0xf7; break; 							/* not bold */
			// 如果Ps = 24,则对于单色显示是取消随后字符的下划线显示,对于彩色显示取是取消绿色.
			case 24: attr = attr & 0xfe; break;  							/* not underline */
			// 如果Ps = 25,则取消随后字符的闪烁显示.
			case 25: attr = attr & 0x7f; break;  							/* not blinking */
			// 如果Ps = 27,则取消随后字符的反显.
			case 27: attr = def_attr; break; 								/* positive image */
			// 如果Ps = 39,则复位随后字符的前景色为默认前景色(白色).
			case 39: attr = (attr & 0xf0) | (def_attr & 0x0f); break;
			// 如果Ps = 49,则复位随后字符的背景色为默认背景色(黑色).
			case 49: attr = (attr & 0x0f) | (def_attr & 0xf0); break;
			// 当Ps(par[i])为其他值时,则是设置指定的前景色或背景色.如果Ps = 30..37,则是设置前景色;如果Ps=40..47,则是设置背景色.
			default:
			  if (!can_do_colour)
			    break;
			  iscolor = 1;
			  if ((par[i] >= 30) && (par[i] <= 38))		 					// 设置前景色.
			    attr = (attr & 0xf0) | (par[i] - 30);
			  else  														/* Background color */			 // 设置背景色.
			    if ((par[i] >= 40) && (par[i] <= 48))
			      attr = (attr & 0x0f) | ((par[i] - 40) << 4);
			    else
					break;
		}
}

// 设置显示光标.
// 根据光标对应显示内存位置pos,设置显示控制器光标的显示位置.
static inline void set_cursor(int currcons)
{
	// 既然我们需要设置显示光标,说明有键盘操作,因此需要恢复进行黑屏操作的延时计数值.
	// 另外,显示光标的控制台必须是当前控制台,因此若当前处理的台号currocons不是前台控制台就立刻返回.
	blankcount = blankinterval;						// 复位黑屏操作的计数值.
	if (currcons != fg_console)
		return;
	// 然后使用索引寄存器端口选择显示控制数据寄存器r14(光标当前显示位置高字节),接着写入光标当前位置高字节(向右移动9位表示高字节移到低字节再除以2),
	// 相对于默认显示内存操作的.再使用索引寄存器选择r15,并将光标当前位置低字节写入其中.
	cli();
	outb_p(14, video_port_reg);
	outb_p(0xff & ((pos - video_mem_base) >> 9), video_port_val);
	outb_p(15, video_port_reg);
	outb_p(0xff & ((pos - video_mem_base) >> 1), video_port_val);
	sti();
}

// 隐藏光标
// 把光标设置到当前虚拟控制台窗口的末端，起到隐藏光标的作用。
static inline void hide_cursor(int currcons)
{
	// 首先使用索引寄存器端口选择控制数据寄存器r14（光标当前显示位置高字节），然后写入光标当前位置高字节（向右移动9位表示高字节移到低字节再除以2），
	// 是相对于默认显示内存操作的。再使用索引寄存器选择r15，并将光标当前位置低字节写入其中。
	outb_p(14, video_port_reg);
	outb_p(0xff & ((scr_end - video_mem_base) >> 9), video_port_val);
	outb_p(15, video_port_reg);
	outb_p(0xff & ((scr_end - video_mem_base) >> 1), video_port_val);
}

// 发送对VT100的响应序列.
// 即为响应主机请求终端向主机发送设备属性(DA).主机通过发送不带参数或参数是0的DA控制序列('ESC [ 0c'或'ESC Z']要求终端发送一个设备属性(DA)控制
// 序列,终端则发送第85行上定义的应答序列(即'ESC [?];2c']来主机的序列,该序列告诉主机本终端是具有高级视频功能的VT100兼容终端.处理过程是将应答序列
// 放入读缓冲队列中,并使用copy_to_cooked()函数处理后放入辅助队列中.
static void respond(int currcons, struct tty_struct * tty)
{
	char * p = RESPONSE;

	cli();
	while (*p) {									// 将应答序列放入读队列.
		PUTCH(*p, tty->read_q);						// 逐字符放入.include/linux/tty.h
		p++;
	}
	sti();
	copy_to_cooked(tty);							// 转换成规范模式(放入辅助队列中).tty_io.c
}

// 在光标处插入一空格字符.
// 把光标开始处的所有字符右移一格,并将擦除字符插入在光标所在处.
static void insert_char(int currcons)
{
	int i = x;
	unsigned short tmp, old = video_erase_char;		// 擦除字符(加属性)
	unsigned short * p = (unsigned short *) pos;	// 光标对应内存位置.

	while (i++ < video_num_columns) {
		tmp = *p;
		*p = old;
		old = tmp;
		p++;
	}
}

// 在光标处插入一行.
// 将屏幕窗口从光标所在行到窗口底的内容向下卷动一行.光标将处在新的空行上.
static void insert_line(int currcons)
{
	int oldtop, oldbottom;

	// 首先保存屏幕窗口卷动开始行top和最后行bottom值,然后从光标所在行让屏幕内容向下滚动一行.最后恢复屏幕窗口卷动开始行top和最后行bottom
	// 的原来值.
	oldtop = top;
	oldbottom = bottom;
	top = y;										// 设置屏幕卷动开始行和结束行.
	bottom = video_num_lines;
	scrdown(currcons);								// 从光标开始处,屏幕内容向下滚动一行.
	top = oldtop;
	bottom = oldbottom;
}

// 删除一个字符
// 删除光标处的一个字符,光标右边的所有字符左移一格.
static void delete_char(int currcons)
{
	int i;
	unsigned short * p = (unsigned short *) pos;

	// 如果光标的当前列位置x走出屏幕最右列,则返回.否则从光标右一个字符开始到行末所有字符左移一格.然后在最后一个字符处填入擦除字符.
	if (x >= video_num_columns)
		return;
	i = x;
	while (++i < video_num_columns) {				// 光标右所有字符左移1格.
		*p = *(p + 1);
		p++;
	}
	*p = video_erase_char;							// 最后填入擦除字符.
}

// 删除光标所在行
// 删除光标所在的一行,并从光标所在行开始屏幕内容上巻一行.
static void delete_line(int currcons)
{
	int oldtop, oldbottom;

	// 首先保存屏幕窗口卷动开始行top和最后行bottom值,然后从光标所在行让屏幕内容向上滚动一行.最后恢复屏幕窗口卷动开始行top和最后行bottom
	// 的原来值.
	oldtop = top;
	oldbottom = bottom;
	top = y;										// 设置屏幕卷动开始行和最后行.
	bottom = video_num_lines;
	scrup(currcons);								// 从光标开始处,屏幕内容向上滚动一行.
	top = oldtop;
	bottom = oldbottom;
}

// 在光标处插入nr个字符
// ANSI转义字符序列:'ESC [ Pn @'.在当前光标处插入1个或多个安全空格字符.Pn是插入的字符数.默认是1.光标将仍然处于第1个插入的空格字符处.在光标与右边界
// 的字符将右移.超过右边界的字符将被丢失.
// 参数 nr = 转义字符序列中的参数Pn.
static void csi_at(int currcons, unsigned int nr)
{
	// 如果插入的字符数大于一行字符数,则截为一行字符数;若插入字符数nr为0,则插入1个字符.然后循环插入指定个空格字符.
	if (nr > video_num_columns)
		nr = video_num_columns;
	else if (!nr)
		nr = 1;
	while (nr--)
		insert_char(currcons);
}

// 在光标位置处插入nr行.
// ANSI转义字符序列: 'ESC [ Pn L'.该控制序列在光标处插入1行或多行空行.操作完成后光标位置不变.当空行被插入时,光标以下滚动区域内的行向下移动.滚动超出显示页的
// 行就丢失.
// 参数nr = 转义字符序列中的参数Pn.
static void csi_L(int currcons, unsigned int nr)
{
	// 如果插入的行数大于屏最多行数,则截为屏幕显示行数;若插入行数nr为0,则插入1行.然后循环插入指定行数nr的空行.
	if (nr > video_num_lines)
		nr = video_num_lines;
	else if (!nr)
		nr = 1;
	while (nr--)
		insert_line(currcons);
}

// 删除光标处的nr个字符.
// ANSI转义序列:'ESC [ Pn P'.该控制序列从光标处删除Pn个字符.当一个字符被删除时,光标右所有字符都左移,这会在右边界处产生一个空字符.其属性应该与最后一个左移字符
// 相同,但这里作了简化处理,仅使用字符的默认属性(黑底白字空格0x0720)来设置空字符.
// 参数nr = 转义字符序列中的参数Pn.
static void csi_P(int currcons, unsigned int nr)
{
	// 如果删除的字符数大于一行字符数,则截为一行字符数;若删除字符数nr为0,则删除1个字符.然后循环删除光标处指定字符数nr.
	if (nr > video_num_columns)
		nr = video_num_columns;
	else if (!nr)
		nr = 1;
	while (nr--)
		delete_char(currcons);
}

// 删除光标处的nr行.
// ANSI转义序列: 'ESC [ Pn M'.该控制序列在滚动区域内,从光标所在行开始删除1行或多行.当行被删除时,滚动区域内的被删行以下的行会向上移动,并且会在最底行添加1空行.若
// Pn大于显示页上剩余行数,则本序列仅删除这些剩余行,并对滚动区域处不起作用.
// 参数nr = 转义字符序列中的参数Pn.
static void csi_M(int currcons, unsigned int nr)
{
	// 如果删除的行数大于屏幕最大行数,则截为屏幕显示行数;若欲删除的行数nr为0,则删除1行.然后循环删除指定行数nr.
	if (nr > video_num_lines)
		nr = video_num_lines;
	else if (!nr)
		nr=1;
	while (nr--)
		delete_line(currcons);
}

//// 保存当前光标位置
static void save_cur(int currcons)
{
	saved_x = x;
	saved_y = y;
}

// 恢复保存的光标位置
static void restore_cur(int currcons)
{
	gotoxy(currcons, saved_x, saved_y);
}

// 这个枚举定义用于下面con_write()函数中处理转义序列或控制序列的解析.ESnormal是初始进入状态,也是转义或控制序列处理完毕时的状态.
// ESnormal  - 表示处于初始正常状态.此时若接收到的是普通显示字符,则把字符直接显示在屏幕上;若接收到的是控制字符(例如回车字符),则对光
//             标位置进行设置.当刚处理完一个转义或控制序列,程序也会返回到本状态.
// ESesc     - 表示接收到转义序列引导字符ESC(0x1b = 033 = 27);如果在此状态下接收到一个'['字符,则说明转义序列引导码,于是跳转到
//             ESsquare去处理.否则就把接收到的字符作为转义序列来处理.对于选择字符集转义序列'ESC ('和'ESC )',使用单独的状态ESsetgraph
//             来处理;对于设备控制字符串序列'ESC P',使用单独的状态ESsetterm来处理.
// ESsquare  - 表示已经接收到一个控制序列引导码('ESC ['),表示接收到的是一个控制序列.于是本状态执行参数数组par[]清零初始化工作.如果
//             此时接收到的又是一个'['字符,则示收到了'ESC [['序列.该序列是键盘功能键发出的序列,于是跳转到ESfunckey去处理.否则
//             我们需要准备接收控制序列的参数,于是置状态ESgetparts并直接进入该状态去接收并保存序列的参数字符.
// ESgetparts- 该状态表示此时要接收控制序列的参数值.参数用十进制数表示,把接收到的数字字符转换成数值并保存到par[]数组中.如果收到一个分号';',
//             则还是维持在本状态,并把接收到的参数值保存在数据par[]下一项中.若不是数字字符或分号,说明已取得所有参数,那么就转移到状态
//             ESgotparts去处理.
// ESgotparts- 表示已经接收到一个完整的控制序列.此时可以根据本状态接收到的结尾字符对相应控制序列进行处理.不过在处理之前,如果在ESsquare状态
//             收到过'?',说明这个序列是终端设备私有序列.本内核不对支持对这种序列的处理,于是直接恢复到ESnormal状态.否则就去执行相应控制序列.
//             待序列处理完后就把状态恢复到ESnormal.
// ESfunckey - 表示接收到了键盘上功能键发出的一个序列,不用显示.于是恢复到正常状态ESnormal.
// ESsetterm - 表示处于设备控制字符串序列状态(DCS).此时收到字符'S',则恢复初始的显示字符属性.若收到的字符是'L'或'l',则开启或关折行显示方式.
// ESsetgraph- 表示收到设置字符转移序列'ESC ('或'ESC )'.它们分别用于指定G0和G1所用的字符集.此时若收到字符'0',则选择图形字符集作为G0和G1,
//             若收到的字符是'B',这选择普通ASCII字符集作为G0和G1的字符集.
enum { ESnormal, ESesc, ESsquare, ESgetpars, ESgotpars, ESfunckey,
	ESsetterm, ESsetgraph };

// 控制台写函数
// 从终端对应的tty写缓冲队列中取字符针对每个字符进行分析.若是控制字符或转义或控制序列,则进行光标定位,字符删除等的控制处理;对于普通字符就直接在光标处
// 显示.
// 参数:tty是当前控制台使用的tty结构指针.
void con_write(struct tty_struct * tty)
{
	int nr;
	char c;
	int currcons;

	// 该函数首先根据当前控制台使用的tty在tty表的项位置取得对应控制台号currcons,然后计算出(CHARS())目前tty写队列中含有的字符数nr,并循环取出其中的每个
	// 字符进行处理.不过如果当前控制台由于接收键盘或发出的暂停命令(如按键Ctrl-S)而处于停止状态,那么本函数就停止处理写队列中的字符,退出函数.另外,如果取出的
	// 是控制字符CAN(24)或SUB(6),那么若是在转义或控制序列期间收到的,则序列不会执行而立刻终止,同时显示随后的字符.注意,con_write()函数只处理取队列字符数
	// 时写队列中当前含有的字符.这有可能在一个序列被放到写队列期间读取字符数,因此本函数前一次退出时state有可能处于处理转义或控制序列的其他状态上.
	currcons = tty - tty_table;
	if ((currcons >= MAX_CONSOLES) || (currcons < 0))
		panic("con_write: illegal tty");

	nr = CHARS(tty->write_q);										// 取写队列中字符数,在tty.h文件中
	while (nr--) {
		if (tty->stopped)
			break;
		GETCH(tty->write_q, c);										// 取1字符到c中
		if (c == 24 || c == 26)										// 控制字符CAN,SUB - 取消,替换
			state = ESnormal;
		switch(state) {
			// ESnormal:表示处于初始正常状态.此时若接收到的是普通显示字符,则把字符直接显示在屏幕上;若接收到的是控制字符(例如回车字符),则对光
			//          标位置进行设置.当刚处理完一个转义或控制序列,程序也会返回到本状态.
			// 如果从写队列中取出的字符是普通显示字符代码,就直接从当前映射字符集中取出对应的显示字符,并放到当前光标所处的显示内存位置处,即直接显示该字符.然后把光标
			// 位置右移一个字符位置.具体地,如果字符不是控制字符也不是扩展字符,即(31<c<127),那么,若当前光标处在行末端或末端以外,则将光标移到下行头列.并调整光标位置
			// 对应的内存指针pos.然后将字符c写到显示内存中pos处,并将光标右移1列,同时也将pos对应地移动2个字节.
			case ESnormal:
				if (c > 31 && c < 127) {							// 是普通显示字符
					if (x >= video_num_columns) {					// 要换行?
						x -= video_num_columns;
						pos -= video_size_row;
						lf(currcons);
					}
					__asm__("movb %2, %%ah\n\t"						// 写字符
						"movw %%ax, %1\n\t"
						::"a" (translate[c - 32]),
						"m" (*(short *)pos),
						"m" (attr)
						:);
					pos += 2;
					x++;
				// 如果字符c是转义字符ESC,则转换状态state到ESesc
				} else if (c == 27)									// ESC - 转义控制字符
					state = ESesc;
				// 如果c是换行符LF(10),或垂直制表符VT(11),或换页符FF(12),则光标移动到下1行.
				else if (c == 10 || c == 11 || c == 12)
					lf(currcons);
				// 如果c是回车符CR(13),则将光标移动到头列(0列)
				else if (c == 13)									// CR - 回车
					cr(currcons);
				// 如果c是DEL(127),则将光标左边字符擦除(用空格字符替代),并将光标移到被擦除位置.
				else if (c == ERASE_CHAR(tty))
					del(currcons);
				// 如果c是BS(backspace,8),则将光标左移1格,并相应调整光标对应内存位置指针pos.
				else if (c == 8) {									// BS - 后退.
					if (x) {
						x--;
						pos -= 2;
					}
				// 如果字符c是水平制表符HT(9),则将光标移到8的位数列上.若此时光标列数超出屏幕最大列数,则将光标移到下一行上.
				} else if (c == 9) {								// HT - 水平制表
					c = 8 - (x & 7);
					x += c;
					pos += c << 1;
					if (x > video_num_columns) {
						x -= video_num_columns;
						pos -= video_size_row;
						lf(currcons);
					}
					c = 9;
				// 如果字符c是响铃符BEL(7),则调用蜂鸣函数,使扬声器发声.
				} else if (c == 7)									// BEL - 响铃
					sysbeep();
				// 如果c是控制字符SO(14)或SI(15),则相应选择字符集G1或G0作为显示字符集.
			  	else if (c == 14)									// SO - 换出,使用G1.
			  		translate = GRAF_TRANS;
			  	else if (c == 15)									// SI - 换进,使用G0.
					translate = NORM_TRANS;
				break;
			// ESesc:表示接收到转义序列引导字符ESC(0x1b = 033 = 27);如果在此状态下接收到一个'['字符,则说明转义序列引导码,于是跳转到
			//       ESsquare去处理.否则就把接收到的字符作为转义序列来处理.对于选择字符集转义序列'ESC ('和'ESC )',使用单独的状态ESsetgraph
			//       来处理;对于设备控制字符串序列'ESC P',使用单独的状态ESsetterm来处理
			// 如果在ESnormal状态收到转义字符ESC(0x1b = 033 = 27),则转到本状态处理.该状态对C1中控制字符或转义字符进行处理.处理完后默认的
			// 状态将是ESnormal.
			case ESesc:
				state = ESnormal;
				switch (c)
				{
				  case '[':											// ESC [ - 是CSI序列.
					state = ESsquare;
					break;
				  case 'E':											// ESC E - 光标下移1行回0列.
					gotoxy(currcons, 0, y + 1);
					break;
				  case 'M':											// ESC M - 光标上移一行.
					ri(currcons);
					break;
				  case 'D':											// ESC D - 光标下移一行
					lf(currcons);
					break;
				  case 'Z':											// ESC Z - 设备属性查询
					respond(currcons, tty);
					break;
				  case '7':											// ESC 7 - 保存光标位置
					save_cur(currcons);
					break;
				  case '8':											// ESC 8 - 恢复光标位置
					restore_cur(currcons);
					break;
				  case '(':  case ')':								// ESC(,ESC) - 选择字符集
				    	state = ESsetgraph;
					break;
				  case 'P':											// ESC P - 设置终端参数
				    	state = ESsetterm;
				    	break;
				  case '#':											// ESC # - 修改整行属性
				  	state = -1;
				  	break;
				  case 'c':											// ESC c - 复位到终端初始设置
					tty->termios = DEF_TERMIOS;
				  	state = restate = ESnormal;
					checkin = 0;
					top = 0;
					bottom = video_num_lines;
					break;
				 /* case '>':   Numeric keypad */
				 /* case '=':   Appl. keypad */
				}
				break;
			// ESsquare:表示已经接收到一个控制序列引导码('ESC ['),表示接收到的是一个控制序列.于是本状态执行参数数组par[]清零初始化工作.如果
			//          此时接收到的又是一个'['字符,则示收到了'ESC [['序列.该序列是键盘功能键发出的序列,于是跳转到ESfunckey去处理.否则
			//          我们需要准备接收控制序列的参数,于是置状态ESgetparts并直接进入该状态去接收并保存序列的参数字符.
			// 如果在状态ESesc(是转义字符ESC)时收到的字符'[',则表明是CSI控制序列,于是转到状态Essequare来处理.首先对ESC转义序列保存参数数组par[]清零,
			// 索引变量npar指向首项,并且设置开始处于参数状态ESgetpars.如果接收到的字符不是'?',则直接转到状态ESgetpars去处理,若接收到的字符是'?',说明这
			// 个序列是终端设备私有序列,后面会有一个功能字符.于是去读下一字符,再到状态ESgetpars去处理代码处.如果此时接收到字符还是'[',那么表明收到了键盘功能
			// 键发出的序列,于是设置下一状态为ESfunckey.否则直接进入ESgetpars状态继续处理.
			case ESsquare:
				for(npar = 0; npar < NPAR; npar++)					// 初始化参数数组.
					par[npar] = 0;
				npar = 0;
				state = ESgetpars;
				if (c == '[')  										/* Function key */	// 'ESC [['是功能键.
				{
					state = ESfunckey;
					break;
				}
				if (ques = (c == '?'))
					break;
			// ESgetpars: 该状态表示此时要接收控制序列的参数值.参数用十进制数表示,把接收到的数字字符转换成数值并保存到par[]数组中.如果收到一个分号';',
			//            则还是维持在本状态,并把接收到的参数值保存在数据par[]下一项中.若不是数字字符或分号,说明已取得所有参数,那么就转移到状态
			//            ESgotparts去处理.
			// 该状态表示此时要接收控制序列的参数值.参数用十进制数表示,把接收到的数字字符转换成数值并保存到par[]数组中.如果收到一个分号';',则还是维持在本状态,并
			// 把接收到的参数值保存在数据par[]下一项中.若不是数字字符或分号,说明已取得所有参数,那么就转移到状态ESgotpars去处理.
			case ESgetpars:
				if (c == ';' && npar < NPAR - 1) {
					npar++;
					break;
				} else if (c >= '0' && c <= '9') {
					par[npar] = 10 * par[npar] + c - '0';
					break;
				} else state = ESgotpars;
			// ESgotpars:表示已经接收到一个完整的控制序列.此时可以根据本状态接收到的结尾字符对相应控制序列进行处理.不过在处理之前,如果在ESsquare状态
			//           收到过'?',说明这个序列是终端设备私有序列.本内核不对支持对这种序列的处理,于是直接恢复到ESnormal状态.否则就去执行相应控制序列.
			//           待序列处理完后就把状态恢复到ESnormal.
			// ESgotpars状态表示我们已经接收到一个完整的控制序列.此时可以根据本状态接收到的结尾字符对相应控制序列进行处理.不过在处理之前,如果在ESsquare状态收到过'?',
			// 说明这个序列是终端设备私有序列.本内核不支持对这种序列的处理,于是直接恢复到ESnormal状态.否则就去执行相应控制序列.待序列处理完后就把状态恢复到ESnormal.
			case ESgotpars:
				state = ESnormal;
				if (ques)
				{ ques =0;
				  break;
				}
				switch(c) {
					// 如果c是字符'G'或'`',则par[]中第1个参数代表列号,若列号不为零,则将光标左移1格.
					case 'G': case '`':							// CSI Pn G - 光标水平移动.
						if (par[0]) par[0]--;
						gotoxy(currcons, par[0], y);
						break;
					// 如果c是'A',则第1个参数代表光标上移的行数.若参数为0则上移1行.
					case 'A':									// CSI Pn A - 光标上移.
						if (!par[0]) par[0]++;
						gotoxy(currcons, x, y - par[0]);
						break;
					// 如果c是'B'或'e',则第1个代表光标右移的格数.若参数为0则下移一行.
					case 'B': case 'e':							// CSI Pn B - 光标下移.
						if (!par[0]) par[0]++;
						gotoxy(currcons, x, y + par[0]);
						break;
					// 如果c是'C'或'a',则第1个参数代表光标右移的格数.若参数为0则右移1格.
					case 'C': case 'a':							// CSI Pn C - 光标右移.
						if (!par[0]) par[0]++;
						gotoxy(currcons, x + par[0], y);
						break;
					// 如果c是'D',则第1个参数代表光标左移的格数.若参数为0则左移1格.
					case 'D':									// CSI Pn D - 光标左移.
						if (!par[0]) par[0]++;
						gotoxy(currcons, x - par[0], y);
						break;
					// 如果c是'E',则第1个参数代表光标向下移动的行数,并回到0列.若参数为0则下移1行.
					case 'E':									// CSI Pn E - 光标下移回0列
						if (!par[0]) par[0]++;
						gotoxy(currcons, 0, y + par[0]);
						break;
					// 如果c是'F',则第1个参数代表光标向上移动的行数,并回到0列.若参数为0则上移1行.
					case 'F':									// CSI Pn F - 光标上移回0列.
						if (!par[0]) par[0]++;
						gotoxy(currcons, 0, y - par[0]);
						break;
					// 如果c是'd',则第1个参数代表光标所需在的行号(从0计数).
					case 'd':									// CSI Pn d - 在当前列置行位置
						if (par[0]) par[0]--;
						gotoxy(currcons, x, par[0]);
						break;
					// 如果c是'H'或'f',则第1个参数代表光标移到的行号,第2个参数代表光标移到的列号.
					case 'H': case 'f':							// CSI Pn H - 光标定位.
						if (par[0]) par[0]--;
						if (par[1]) par[1]--;
						gotoxy(currcons, par[1], par[0]);
						break;
					// 如果字符c是'J',则第1个参数代表以光标所处位置清屏的方式:
					// 序列: 'ESC [ Ps J'(Ps=0删除光标到屏幕底端;Ps=1删除屏幕开始到光标处;Ps=2整屏删除).
					case 'J':									// CSI Pn J - 屏幕擦除字符.
						csi_J(currcons, par[0]);
						break;
					// 如果字符c是'K',则第1个参数代表以光标所在位置对行中字符进行删除处理的方式:
					// 序列: 'ESC [ Ps K'(Ps=0删除到行尾;Ps=1从开始删除;Ps=2整行都删除).
					case 'K':									// CSI Pn K - 行内擦除字符.
						csi_K(currcons,par[0]);
						break;
					// 如果字符c是'L',表示在光标位置处插入n行(控制序列 'ESC [ Pn L')
					case 'L':									// CSI Pn L - 插入行.
						csi_L(currcons, par[0]);
						break;
					// 如果字符c是'M',表示在光标位置处删除n行(控制序列 'ESC [ Pn M')
					case 'M':									// 删除行
						csi_M(currcons, par[0]);
						break;
					// 如果字符c是'P',表示在光标位置处删除n个字符(控制序列 'ESC [ Pn P')
					case 'P':									// 删除字符.
						csi_P(currcons, par[0]);
						break;
					// 如果字符c是'@',表示在光标位置处插入n个字符(控制序列 'ESC [ Pn @')
					case '@':									// 插入字符.
						csi_at(currcons, par[0]);
						break;
					// 如果字符c是'm',表示改变光标处字符的显示属性,比如加粗,加下划线,闪烁,反显等.
					// 转义序列: 'ESC [ Pn m'.n=0正常显示;1加粗;4加下划线;7反显;27正常显示等.
					case 'm':									// CSI Ps m - 设置显示字符属性.
						csi_m(currcons);
						break;
					// 如果字符c是'r',则表示两个参数设置滚屏的起始行号和终止行号.
					case 'r':									// CSI Pn r - 设置滚屏上下界.
						if (par[0]) par[0]--;
						if (!par[1]) par[1] = video_num_lines;
						if (par[0] < par[1] &&
						    par[1] <= video_num_lines) {
							top = par[0];
							bottom = par[1];
						}
						break;
					// 如果字符c是's',则表示保存当前光标所在位置.
					case 's':									// CSI s - 保存光标位置.
						save_cur(currcons);
						break;
					// 如果字符c是'u',则表示恢复光标到原保存的位置处.
					case 'u':									// CSI u - 恢复保存的光标位置.
						restore_cur(currcons);
						break;
					// 如果字符c是'l'或'b',则分别表示设置屏幕黑屏间隔时间和设置粗体字符显示.此时参数数组中par[1]和par[2]是特征值,它们分别必须par[1]=par[0]+13;
					// par[2]=par[0]+17.在这个条件下,如果c是字符'l',那么par[0]中是开始黑屏时延迟的分钟数;如果c是字符'b',那么par[0]中是设置的粗体字符属性值.
					case 'l': 									/* blank interval */
					case 'b': 									/* bold attribute */
						  if (!((npar >= 2) &&
						  ((par[1] - 13) == par[0]) &&
						  ((par[2] - 17) == par[0])))
						    break;
						if ((c == 'l') && (par[0] >= 0) && (par[0] <= 60))
						{
						  blankinterval = HZ * 60 * par[0];
						  blankcount = blankinterval;
						}
						if (c == 'b')
						  vc_cons[currcons].vc_bold_attr = par[0];
				}
				break;
			// ESfunckey:表示接收到了键盘上功能键发出的一个序列,不用显示.于是恢复到正常状态ESnormal.
			// 状态ESfunckey表示接收到了键盘上功能键发出的一个序列,不用显示.于是恢复到正常状态ESnormal.
			case ESfunckey:									// 键盘功能键码.
				state = ESnormal;
				break;
			// ESsetterm:表示处于设备控制字符串序列状态(DCS).此时收到字符'S',则恢复初始的显示字符属性.若收到的字符是'L'或'l',则开启或关折行显示方式.
			// 状态ESsetterm表示处于设备控制字符串序列状态(DCS).此时若收到字符'S',则恢复初始的显示字符属性.若收到的字符是'L'或'l',则开启或关闭折行显示方式.
			case ESsetterm:  								/* Setterm functions. */
				state = ESnormal;
				if (c == 'S') {
					def_attr = attr;
					video_erase_char = (video_erase_char & 0x0ff) | (def_attr << 8);
				} else if (c == 'L')
					; 										/*linewrap on*/
				else if (c == 'l')
					; 										/*linewrap off*/
				break;
			// ESsetgraph:表示收到设置字符转移序列'ESC ('或'ESC )'.它们分别用于指定G0和G1所用的字符集.此时若收到字符'0',则选择图形字符集作为G0和G1,
			//            若收到的字符是'B',这选择普通ASCII字符集作为G0和G1的字符集.
			// 状态ESsetgraph表示收到设置字符集转移序列'ESC ('或'ESC )'.它们分别用于指定G0和G1所用的字符集.此时若收到字符'0',则选择图形字符集作为G0和G1,若收
			// 到的字符是'B',则选择普通ASCII字符集作为G0和G1的字符集.
			case ESsetgraph:								// 'CSI ( 0'或'CSI ( B' - 选择字符集
				state = ESnormal;
				if (c == '0')
					translate = GRAF_TRANS;
				else if (c == 'B')
					translate = NORM_TRANS;
				break;
			default:
				state = ESnormal;
        }
    }
	set_cursor(currcons);									// 最后根据上面设置的光标位置,设置显示控制器中光标位置.
}

/*
 *  void con_init(void);
 *
 * This routine initalizes console interrupts, and does nothing
 * else. If you want the screen to clear, call tty_write with
 * the appropriate escape-sequece.
 *
 * Reads the information preserved by setup.s to determine the current display
 * type and sets everything accordingly.
 */
/*
 * void con_init(void);
 *
 * 这个子程序初始化控制台中断,其他什么都不做.如果你想让屏幕干净的话,就使用适当的转义字符序列调用tty_write()函数.
 * 读取setup.s程序保存的信息,用以确定当前显示器类型,并且设置所有相关参数.
 */
void con_init(void)
{
	register unsigned char a;
	char *display_desc = "????";
	char *display_ptr;
	int currcons = 0;								// 当前虚拟控制台号.
	long base, term;
	long video_memory;

	// 初始化屏幕的列数
	video_num_columns = ORIG_VIDEO_COLS;
	// 屏幕每行的字节数等于屏幕列数乘以2，因为一个显示字节需要一个控制字节
	video_size_row = video_num_columns * 2;
	// 初始化屏幕的行数
	video_num_lines = ORIG_VIDEO_LINES;
	// 初始化显示页数
	video_page = ORIG_VIDEO_PAGE;
	// 设置此时第0个(currcons)显示终端的擦除字符属性及字符
	video_erase_char = 0x0720;
	// 初始化黑屏时间计数
	blankcount = blankinterval;

	// 然后根据显示模式是单色还是彩色,分别设置所使用的显示内存起始位置以及显示寄存器索引端口号和显示寄存器数据端口号.如果获得的BIOS显示方式等于7,
	// 则表示是单色显示卡.
	if (ORIG_VIDEO_MODE == 7)					/* Is this a monochrome display? */
	{
		video_mem_base = 0xb0000;				// 设置单显映像内存起始地址.
		video_port_reg = 0x3b4;					// 设置单显索引寄存器端口.
		video_port_val = 0x3b5;					// 设置单显数据寄存器端口.

		// 接着根据BIOS中断int 0x10功能0x12获得的显示模式信息,判断显示卡是单色显示卡还是彩色显示卡.若用上述中断功能所得到的BX寄存器返回值不等于
		// 0x10,则说明是EGA卡.因此初始显示类型为EGA单色.虽然EGA卡上有较多显示内存,但在单色方式下最多只能利用地址范围在0xb0000~xb8000之间的显示内存.
		// 然后置显示器描述字符串为'EGAm'.
		// 并会在系统初始化期间显示器描述符字符串将显示在屏幕的右上角.
		// 注意,这里使用了bx在调用中断int 0x10前后是否被改变的方法来判断卡的类型.若BL在中断调用后值被改变,表示显示卡支持ah=12h功能调用,是EGA或后推
		// 出来的VGA等类型显示卡.若中断调用返回值末变,表示显示卡不支持这个功能,则说明是一般单色显示卡.
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
		{
			video_type = VIDEO_TYPE_EGAM;		// 设置显示类型(EGA单色).
			video_mem_term = 0xb8000;			// 设置显示内存末端地址.
			display_desc = "EGAm";				// 设置显示描述字符串.
		}
		// 如果BX寄存器的值等于0x10,则说明是单色显示卡MDA,仅有8KB显示内存.
		else
		{
			video_type = VIDEO_TYPE_MDA;		// 设置显示类型(MDA单色).
			video_mem_term = 0xb2000;			// 设置显示内存末端地址.
			display_desc = "*MDA";				// 设置显示描述字符串.
		}
	}
	// 如果显示方式不为7,说明是彩色显示卡.此时文本方式下所用显示内存起始地址为0xb8000;显示控制索引寄存器端口地址为0x3d4;数据寄存器端口地址为0x3d5.
	else										/* If not, it is color. */
	{
		can_do_colour = 1;						// 设置彩色显示标志.
		video_mem_base = 0xb8000;				// 显示内存起始地址.
		video_port_reg	= 0x3d4;				// 设置彩色显示索引寄存器端口.
		video_port_val	= 0x3d5;				// 设置彩色显示数据寄存器端口.
		// 再判断显示卡类别.如果BX不等于0x10,则说明是EGA显示卡,此时共有32KB显示内存可用(0xb8000~0xc0000).否则说明是CGA显示卡,只能使用8KB显示内存(
		// 0xb8000~0xba000).
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
		{
			video_type = VIDEO_TYPE_EGAC;		// 设置显示类型(EGA彩色).
			video_mem_term = 0xc0000;			// 设置显示内存末端地址.
			display_desc = "EGAc";				// 设置显示描述字符串.
		}
		else
		{
			video_type = VIDEO_TYPE_CGA;		// 设置显示类型(CGA).
			video_mem_term = 0xba000;			// 设置显示内存末端地址.
			display_desc = "*CGA";				// 设置显示描述字符串.
		}
	}
	// 现在来计算当前显示卡内存上可以开设的虚拟控制台数量.硬件允许的虚拟控制台数量等于总显示内存量video_memory除以每个虚拟控制台占用的
	// 字节数.每个虚拟控制台占用的显示内存数等于屏幕显示数video_num_lines乘上每行字符占有的字节数video_size_row.
	// 如果硬件允许开设的虚拟控制台数量大于系统既定的数量MAX_CONSOLES,就把虚拟控制台数量设置为MAX_CONSOLES.若这样计算出的虚拟控制台
	// 数量为0,则设置为1.
	// 最后用显示内存数除以判断出的虚拟控制台数即得到每个虚拟控制台占用显示内存字节数.
	video_memory = video_mem_term - video_mem_base;
	// 根据实际的显示内存的大小计算显示控制终端的实际数量
	NR_CONSOLES = video_memory / (video_num_lines * video_size_row);
	// 显示终端的最大数量是MAX_CONSOLES,设置在tty.h头文件中
	if (NR_CONSOLES > MAX_CONSOLES)
		NR_CONSOLES = MAX_CONSOLES;
	// 如果计算出来的显示终端数量为0，则将显示终端数量设置为1
	if (!NR_CONSOLES)
		NR_CONSOLES = 1;
	video_memory /= NR_CONSOLES;				// 每个虚拟控制台占用显示内存字节数.

	/* Let the user known what kind of display driver we are using */

	// 然后我们在屏幕的右上角显示描述字符串.采用的方法是直接将字符串写到显示内存的相应位置处.首先将显示指针display_ptr指到屏幕第1行右端差
	// 4个字符处(每个字符需2个字节,因此减8),然后循环复制字符串的字符,并且每复制1个字符都空开1个属性字节.
	display_ptr = ((char *)video_mem_base) + video_size_row - 8;
	while (*display_desc)
	{
		*display_ptr++ = *display_desc++;
		display_ptr++;
	}

	/* Initialize the variables used for scrolling (mostly EGA/VGA)	*/
	/* 初始化用于滚屏的变量(主要用于EGA/VGA) */

	// 注意,此时当前虚拟控制台号curcons已经被初始化0.因此下面实际上是初始化0号虚拟控制台的结构vc_cons[0]中的所有字段值.下面首先设置0号控制台
	// 的默认滚屏开始位置video_mem_start和默认滚屏末行内存位置,实际上它们也就是0号控制台占用的部分显示内存区域.然后初始化设置0号虚拟控制台的
	// 其它属性和标志值.
	base = origin = video_mem_start = video_mem_base;						// 默认滚屏开始内存位置.
	term = video_mem_end = base + video_memory;								// 0号屏幕内存末端位置.
	scr_end	= video_mem_start + video_num_lines * video_size_row;			// 滚屏末端位置.
	top	= 0;																// 初始设置滚动时顶行行号.
	bottom	= video_num_lines;												// 初始设置滚动时底行行号.
	attr = 0x07;															// 初始设置显示字符属性(黑底白字).
	def_attr = 0x07;														// 设置默认显示字符属性.
	restate = state = ESnormal;												// 初始化转义序列操作的前和下一状态.
	checkin = 0;
	ques = 0;																// 收到问号字符标志.
	iscolor = 0;															// 彩色显示标志.
	translate = NORM_TRANS;													// 使用的字符集(普通ASCII码表).
	vc_cons[0].vc_bold_attr = -1;											// 粗体字符属性标志(-1表示不用).

	// 在设置了0号控制台当前光标所有位置和光标对应的内存位置pos后,循环设置其余的几个虚拟控制台结构的参数值.除了各自占用的显示内存开始和结束位置不同,
	// 它们的初始值基本上都与0号控制台相同.
	gotoxy(currcons, ORIG_X, ORIG_Y);
  	for (currcons = 1; currcons < NR_CONSOLES; currcons++) {
		vc_cons[currcons] = vc_cons[0];         							// 复制0号结构的参数.
		origin = video_mem_start = (base += video_memory);
		scr_end = origin + video_num_lines * video_size_row;
		video_mem_end = (term += video_memory);
		gotoxy(currcons, 0, 0);                           					// 光标都初始化在屏幕左上角位置.
	}
	// 最后设置当前前台控制台的屏幕原点(左上角)位置和显示控制器中光标显示位置,并设置键盘中断0x21陷阱门描述符(&keyboard_inierrupt是键盘中断处理过程
	// 地址).然后取消中断控制芯片8259A中对键盘中断的屏蔽,允许响应键盘发出的IRQ1请求信号.最后复位键盘控制器以允许键盘开始正常工作.
	update_screen();														// 更新前台原点来设置光标位置.
	set_trap_gate(0x21, &keyboard_interrupt);								// 参见system.h,设置键盘的系统中断门
	outb_p(inb_p(0x21) & 0xfd, 0x21);										// 取消对键盘中断的屏蔽,允许IRQ1.
	a = inb_p(0x61);														// 读取键盘端口0x61(8255A端口PB).
	outb_p(a | 0x80, 0x61);													// 设置禁止键盘工作(位7置位).
	outb_p(a, 0x61);														// 再允许键盘工作,用以复位键盘.
}

// 更新当前控制台.
// 把前台控制台转换为fg_console指定的虚拟控制台.fg_console是设置的前台虚拟控制台号.
// fg_console变量在tty.h头文件中定义，用来启动后默认使用的显示终端
void update_screen(void)
{
	set_origin(fg_console);													// 设置滚屏起始显示内存地址.
	set_cursor(fg_console);													// 设置显示控制器中光标显示内存位置.
}

/* from bsd-net-2: */

// 停止蜂鸣
// 复位8255A PB端口的位1和位0.
void sysbeepstop(void)
{
	/* disable counter 2 */		/* 禁止定时器2 */
	outb(inb_p(0x61)&0xFC, 0x61);
}

int beepcount = 0;		// 蜂鸣时间滴答计数.

// 开通蜂鸣
// 8255A芯片PB端口的位1用作扬声器的开门信号;位0用作8253定时器2门信号,该定时器的输出脉冲送往扬声器,作为扬声器发声频率.因此要使扬声器
// 发声,需要两步:首先开启PB端口(0x61)位1和位0(置位),然后设置定时器2通道发送一定的定时频率即可.
static void sysbeep(void)
{
	/* enable counter 2 */		/* 开启定时器2 */
	outb_p(inb_p(0x61)|3, 0x61);
	/* set command for counter 2, 2 byte write */	/* 送设置定时器2命令 */
	outb_p(0xB6, 0x43);		// 定时器芯片控制字寄存器端口.
	/* send 0x637 for 750 HZ */	/* 设置频率为720Hz,因此送定时值0x637 */
	outb_p(0x37, 0x42);		// 通过2数据端口分别送计数高低字节
	outb(0x06, 0x42);
	/* 1/8 second */		/* 蜂鸣时间为1/8s */
	beepcount = HZ / 8;
}

// 拷贝屏幕
// 把屏幕内容复制到参数指定的用户缓冲区arg中。
// 参数arg有两个用途：一是用于传递控制台号，二是作为用户缓冲区指针。
int do_screendump(int arg)
{
	char *sptr, *buf = (char *)arg;
	int currcons, l;

	// 函数首先验证用户提供的缓冲区容量，若不够则进行适当扩展。然后从其开始处取出控制台号currcons.
	// 在判断控制台号有效后，就把该控制台屏幕的所有内存内容复制到用户缓冲区中。
	verify_area(buf, video_num_columns * video_num_lines);
	currcons = get_fs_byte(buf);
	if ((currcons < 1) || (currcons > NR_CONSOLES))
		return -EIO;
	currcons--;
	sptr = (char *) origin;
	for (l = video_num_lines * video_num_columns; l > 0 ; l--)
		put_fs_byte(*sptr++, buf++);
	return(0);
}

// 黑屏处理
// 当用户在blankInterval时间间隔内没有按任何按键时就让屏幕黑屏,以保护屏幕.
void blank_screen()
{
	if (video_type != VIDEO_TYPE_EGAC && video_type != VIDEO_TYPE_EGAM)
		return;
	/* blank here. I can't find out how to do it, though */
}

// 恢复黑屏的屏幕
// 当用户按下任何按键时,就恢复处于黑屏状态的屏幕显示内容.
void unblank_screen()
{
	if (video_type != VIDEO_TYPE_EGAC && video_type != VIDEO_TYPE_EGAM)
		return;
	/* unblank here */
}

// 控制台显示函数
// 该函数仅用于内核显示函数printk()(kernel/printk.c),用于在当前前台控制台上显示内核信息.
// 处理方法是循环取出缓冲区中的字符,并根据字符的特性控制光标移动或直接显示在屏幕上.
// 参数b是null结尾的字符串缓冲区指针。
void console_print(const char * b)
{
	int currcons = fg_console;
	char c;

	// 循环读取缓冲区b中的字符。
	while (c = *(b++)) {
		// 如果当前字符c是换行符，则对光标执行回车换行操作
		if (c == 10) {
			// 光标回到当前行的第0列
			cr(currcons);
			// 将光标从当前列移动到下一行
			lf(currcons);
			continue;
		}
		// 如果是回车符，就直接执行回车动作。然后去处理下一个字符。
		if (c == 13) {
			cr(currcons);
			continue;
		}
		// 在读取了一个不是回车或换行字符后，如果发现当前光标列位置x已经到达屏幕右末端，则让光标折返到下一行开始处。
		// 然后把字符放到光标所处显示内存位置处，即在屏幕上显示出来。再把光标右移一格位置，为显示下一个字符作准备。
		if (x >= video_num_columns) {
			x -= video_num_columns;
			pos -= video_size_row;
			lf(currcons);
		}
		// 寄存器al中是需要显示的字符，这里把属性字节放到ah中，然后把ax内容存储到光标内存位置pos处，即在光标处显示字符。
		__asm__("movb %2, %%ah\n\t"              				// 属性字节放到ah中。
			"movw %%ax, %1\n\t"              					// ax内容放到pos处。
			::"a" (c),
			"m" (*(short *)pos),
			"m" (attr)
			:);
		pos += 2;
		x++;
	}
	set_cursor(currcons);           							// 最后设置的光标内存位置，设置显示控制器中光标位置。
}


void del_col(int i){
	int currcons = fg_console;
	gotoxy(currcons, x - i, y);
	csi_P(currcons, i);
}
