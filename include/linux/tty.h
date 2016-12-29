/*
 * 'tty.h' defines some structures used by tty_io.c and some defines.
 *
 * NOTE! Don't touch this without checking that nothing in rs_io.s or
 * con_io.s breaks. Some constants are hardwired into the system (mainly
 * offsets into 'tty_queue'
 */
/*
 * 'tty.h'中定义了tty_io.c程序使用的某些结构和其他一些定义.
 *
 * 注意!在修改这里的定义时,一定要检查rs_io.s或con_io.s程序中不会出现问题
 * 在系统中有些常量是直接写在程序中的(主要是一些tty_queue中的偏移值).
 */

#ifndef _TTY_H
#define _TTY_H

#define MAX_CONSOLES	8								// 最大虚拟控制台数量.
#define NR_SERIALS		2								// 串行终端数量
#define NR_PTYS			4								// 伪终端数量

extern int NR_CONSOLES;

#include <termios.h>

#define TTY_BUF_SIZE 1024								// tty缓冲区(缓冲队列)大小.

// tty字符缓冲队列数据结构.用于tty_struc结构中的读/写和辅助(规范)缓冲队列.
struct tty_queue {
	unsigned long data;									// 队列缓冲区中含有字符行数值(不是当前字符数).对于串口终端,则存放串行端口地址.
	unsigned long head;									// 缓冲区中数据头指针
	unsigned long tail;									// 缓冲区中数据尾指针
	struct task_struct * proc_list;						// 等待本队列的进程列表.
	char buf[TTY_BUF_SIZE];								// 队列的缓冲区.
};

#define IS_A_CONSOLE(min)			(((min) & 0xC0) == 0x00)	// 是一个控制终端.
#define IS_A_SERIAL(min)			(((min) & 0xC0) == 0x40)	// 是一串行终端.
#define IS_A_PTY(min)				((min) & 0x80)				// 是一个伪终端.
#define IS_A_PTY_MASTER(min)		(((min) & 0xC0) == 0x80)	// 是一个主伪终端.
#define IS_A_PTY_SLAVE(min)			(((min) & 0xC0) == 0xC0)	// 是一个辅伪终端.
#define PTY_OTHER(min)				((min) ^ 0x40)				// 其他伪终端.

// 以下定义了tty等待队列中缓冲区操作宏函数.(tail在前,head在后)
#define INC(a) ((a) = ((a) + 1) & (TTY_BUF_SIZE - 1))           				// a缓冲区指针前移1字节,若已超出缓冲区右侧,则指针循环
#define DEC(a) ((a) = ((a) - 1) & (TTY_BUF_SIZE - 1))           				// a缓冲区指针后退1字节,并循环
#define EMPTY(a) ((a)->head == (a)->tail)                       				// 缓冲区是否为空
#define LEFT(a) (((a)->tail - (a)->head - 1) & (TTY_BUF_SIZE - 1))      		// 缓冲区还可存放字符的长度(空闲区长度)
#define LAST(a) ((a)->buf[(TTY_BUF_SIZE - 1) & ((a)->head - 1)])      			// 缓冲区中最后一个位置
#define FULL(a) (!LEFT(a))                                      				// 缓冲区满(如果为1的话)
#define CHARS(a) (((a)->head - (a)->tail) & (TTY_BUF_SIZE - 1))       			// 缓冲区中已存放字符的长度(字符数)
// 从queue队列项缓冲区中取一字符(从tail处,并且tail+=1)
#define GETCH(queue, c) \
(void)({c = (queue)->buf[(queue)->tail]; INC((queue)->tail);})
// 往queue队列项缓冲区中放置一字符(在head处,并且head+=1)
#define PUTCH(c, queue) \
(void)({(queue)->buf[(queue)->head] = (c); INC((queue)->head);})

// 判断终端键盘字符类型
#define INTR_CHAR(tty) ((tty)->termios.c_cc[VINTR])             			// 中断符.发中断信号SIGINT
#define QUIT_CHAR(tty) ((tty)->termios.c_cc[VQUIT])     					// 退出符.发退出信号SIGQUIT
#define ERASE_CHAR(tty) ((tty)->termios.c_cc[VERASE])   					// 删除符.擦除一个字符
#define KILL_CHAR(tty) ((tty)->termios.c_cc[VKILL])             			// 删除行.删除一行字符
#define EOF_CHAR(tty) ((tty)->termios.c_cc[VEOF])       					// 文件结束符
#define START_CHAR(tty) ((tty)->termios.c_cc[VSTART])   					// 开始符.恢复输出
#define STOP_CHAR(tty) ((tty)->termios.c_cc[VSTOP])     					// 停止符.停止输出
#define SUSPEND_CHAR(tty) ((tty)->termios.c_cc[VSUSP])  					// 挂起符.发挂起信号SIGTSTP

// tty数据结构
struct tty_struct {
	struct termios termios;						// 终端io属性和控制字符数据结构.
	int pgrp;									// 所属进程组.
	int session;								// 会话号.
	int stopped;								// 停止标志.
	void (*write)(struct tty_struct * tty);		// tty写函数指针.
	struct tty_queue *read_q;					// tty读队列.
	struct tty_queue *write_q;					// tty写队列.
	struct tty_queue *secondary;				// tty辅助队列(存放规范模式字符序列).可称为规范(熟)模式队列.
	};

extern struct tty_struct tty_table[];			// tty结构数组.
extern int fg_console;							// 前台控制台号

// 根据终端类型在tty_table[]中取对应终端号nr的tty结构指针.第73行后半部分用于根据子设备号dev在tty_table[]表中选择对应的tty结构.
// 如果dev = 0,表示正在使用前台终端,因此直接使用终端号fg_console作为tty_table[]项索引取tty结构.如果dev大于0,那么就要分两种情况
// 考虑:1,dev是虚拟终端号;2,dev是串行终端号或者伪终端号.对于虚拟终端其tty结构在tty_table[] 索引项是dev-1(0 -- 63).对于其他类型
// 终端,则它们在tty结构索引项就是dev.例如，如果dev = 64，表示是一个串行终端，则其tty结构就是tty_table[dev]。如果dev = 1,则对应
// 终端的tty结构是tty_table[0].
#define TTY_TABLE(nr) \
(tty_table + ((nr) ? (((nr) < 64)? (nr) - 1 : (nr))	: fg_console))

// 这里给出了终端termios结构中可更改的特殊字符数组c_cc[]的初始值.该termios结构定义在include/termios.h中.POSIX.1定义了11个
// 特殊字符,但是Linux系统还另外定义了SVR4使用的6个特殊字符.如果定义了_POSIX_VDISABLE(\0),那么当某一项值等于_POSIX_VDISABLE
// 的值时,表示禁止使用相应的特殊字符.[8进制值]
/*	intr=^C		quit=^|		erase=del	kill=^U
	eof=^D		vtime=\0	vmin=\1		sxtc=\0
	start=^Q	stop=^S		susp=^Z		eol=\0
	reprint=^R	discard=^U	werase=^W	lnext=^V
	eol2=\0
*/
/*	中断intr=^C	退出quit=^|	删除erase=del	终止kill=^U
	文件结束eof=^D	vtime=\0	vmin=\1		sxtc=\0
	开始start=^Q	停止stop=^S	挂起susp=^Z	行结束eol=\0
	重显reprint=^R	丢弃discard=^U	werase=^W	lnext=^V
	行结束eol2=\0
*/
#define INIT_C_CC "\003\034\177\025\004\0\1\0\021\023\032\0\022\017\027\026\0"

void rs_init(void);     // 异步串行通信初始化。（kernel/chr_drv/serial.c）
void con_init(void);	// 控制终端初始化.(kernel/chr_drv/console.c)
void tty_init(void);	// tty初始化.(kernel/chr_drv/tty_io.c)

int tty_read(unsigned c, char * buf, int n);    // （kernel/chr_drv/tty_io.c）
int tty_write(unsigned c, char * buf, int n);   // （kernel/chr_drv/tty_io.c）

void con_write(struct tty_struct * tty);	// (kernel/chr_drv/console.c)
void rs_write(struct tty_struct * tty);         //（kernel/chr_drv/serial.c）
void mpty_write(struct tty_struct * tty);       //（kernel/chr_drv/pty.c）
void spty_write(struct tty_struct * tty);       //（kernel/chr_drv/pty.c）

void copy_to_cooked(struct tty_struct * tty);   //（kernel/chr_drv/tty_io.c）

void update_screen(void);	// (kernel/chr_drv/console.c)

#endif
