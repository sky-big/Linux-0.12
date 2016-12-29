#ifndef _TERMIOS_H
#define _TERMIOS_H

#include <sys/types.h>

#define TTY_BUF_SIZE 1024               // tty中的缓冲区长度。

/* 0x54 is just a magic number to make these relatively uniqe ('T') */
/* 0x54 只是一个魔数，目的是为了使这些常数唯一（'T'）*/

// tty设备的ioctl调用命令集。ioctl将命令编码在低位字中。
// 下面名称TC[*]的含义是tty控制命令。
// 取相应终端termios结构中的信息（参见tcgetattr()）。
#define TCGETS		0x5401
// 设置相应终端termios结构中的信息（参见tcsetattr()，TCSANOW）。
#define TCSETS		0x5402
// 在设置相应终端termios的信息之前，需要先等待输出队列中所有数据处理完（耗尽）。对于修改参数会影响输出的情况，
// 就需要使用这种形式（参见tcsetattr()，TCSADRAIN选项）。
#define TCSETSW		0x5403
// 在设置termios的信息之前，需要先等待输出队列中所有数据处理完，并且刷新（清空）输入队列，再设置（参见
// tcsetattr()，TCSAFLUSH选项）。
#define TCSETSF		0x5404
// 取相应终端termio结构听信息（参见tcgetattr()）。
#define TCGETA		0x5405
// 设置相应终端termio结构中的信息（参见tcsetattr()，TCSANOW选项）。
#define TCSETA		0x5406
// 在设置终端termio的信息之前，需要先等待输出队列中所有数据处理完（耗尽）。对于修改参数会影响输出的情况，就需要
// 使用这种形式（参见tcsetattr()，TCSADRAIN选项）。
#define TCSETAW		0x5407
// 在设置termio的信息之前，需要先等待输出队列中所有数据处理完，并且刷新（清空）输入队列，再设置（参见tcsetattr(),
// TCSAFLUSH选项）。
#define TCSETAF		0x5408
// 等待输出队列处理完毕（空），若参数值是0,则发送一个break（参见tcsendbreak()，tcdrain()）。
#define TCSBRK		0x5409
// 开始/停止控制。如果参数值是0,则挂起输出；如果是1,则重新开启挂起的输出；如果是2,则挂起输入；如果是3,则重新开启挂
// 起的输入（参见tcflow()）。
#define TCXONC		0x540A
// 刷新已写输出但还没发送或已收但还没有读数据。如果参数是0,则刷新（清空）输入队列；如果是1,则刷新输出队列；如果是2,
// 则刷新输入和输出队列（参见tcflush()）。
#define TCFLSH		0x540B
// 下面名称TIOC[*]的含义是tty输入输出控制命令。
// 设置终端串行线路专用模式。
#define TIOCEXCL	0x540C
// 复位终端串行线路专用模式。
#define TIOCNXCL	0x540D
// 设置tty为控制终端。（TIOCNOTTY - 禁止tty为控制终端）。
#define TIOCSCTTY	0x540E
// 读取指定终端设备的组id，参见tcgetpgrp()。该常数符号名称是“Terminal IO Control Get PGRP”的缩写。读取前台进程
// 组ID。
#define TIOCGPGRP	0x540F
// 设置指定终端设备进程的组id（参见tcsetpgrp()）。
#define TIOCSPGRP	0x5410
// 返回输出队列中还未送出的字符数。
#define TIOCOUTQ	0x5411
// 模拟终端输入。该命令以一个指向字符的指针作为参数，并假装该字符是在终端上键入的。用户必须在该控制终端上具有超级用户权限
// 或具有读许可权限。
#define TIOCSTI		0x5412
// 读取终端设备窗口大小信息（参见winsize结构）。
#define TIOCGWINSZ	0x5413
// 设置终端设备窗口大小信息（参见winsize结构）。
#define TIOCSWINSZ	0x5414
// 返回modem状态控制引线的当前状态比特位标志集。
#define TIOCMGET	0x5415
// 设置单个modem状态控制引线的状态（true或false）（Individual control line Set）。
#define TIOCMBIS	0x5416
// 复位单个modem状态控制引线的状态（Individual control line clear）。
#define TIOCMBIC	0x5417
// 设置modem状态引线的状态。如果某一比特位置位，则modem对应的状态引线将置为有效。
#define TIOCMSET	0x5418
// 读取软件载波检测标志（1 - 开启；0 - 关闭）。
// 对于本地连接的终端或其他设置，软件载波标志是开启的，对于使用modem线路的终端或设备则是关闭的。为了能使用这两个ioctl调用，
// tty线路应该是以O_NDELAY方式打开的，这样open()就不会等待载波。
#define TIOCGSOFTCAR	0x5419
// 设置软件载波检测标志（1 - 开启；0 - 关闭）。
#define TIOCSSOFTCAR	0x541A
// 返回输入队列中还未取走字符的数目。
#define FIONREAD	0x541B
#define TIOCINQ		FIONREAD

// 窗口大小（Window size）属性结构。在窗口环境中可用于基于屏幕的应用程序。
// ioctls中的TIOCGWINSZ和TIOCSWINSZ可用来读取或设置这些信息。
struct winsize {
	unsigned short ws_row;          // 窗口字符行数。
	unsigned short ws_col;          // 窗口字符列数。
	unsigned short ws_xpixel;       // 窗口宽度，像素值。
	unsigned short ws_ypixel;       // 窗口高度，像素值。
};

// AT&T系统V的termio结构。
#define NCC 8                           // termio结构中控制字符数组的长度。
struct termio {
	unsigned short c_iflag;		/* input mode flags */   // 输入模式标志。
	unsigned short c_oflag;		/* output mode flags */  // 输出模式标志。
	unsigned short c_cflag;		/* control mode flags */ // 控制模式标志。
	unsigned short c_lflag;		/* local mode flags */   // 本地模式标志。
	unsigned char c_line;		/* line discipline */    // 线路规程（速率）。
	unsigned char c_cc[NCC];	/* control characters */ // 控制字符数组。
};

// POSIX的termios结构
#define NCCS 17		// termios结构中控制字符数组长度.
struct termios {
	tcflag_t c_iflag;		/* input mode flags */	// 输入模式标志
	tcflag_t c_oflag;		/* output mode flags */	// 输出模式标志
	tcflag_t c_cflag;		/* control mode flags */	// 控制模式标志.
	tcflag_t c_lflag;		/* local mode flags */		// 本地模式标志.
	cc_t c_line;			/* line discipline */		// 线路规程(速率).
	cc_t c_cc[NCCS];		/* control characters */	// 控制字符数组.
};

// 以下是控制字符数组c_cc[]中项的索引值.该数组初始定义在include/linux/tty.h中.程序可以更改这个数组中的值.如果定义了_POSIX_VDISABLE(\0)
// 那么当数组某一项值等于_POSIX_VDISABLE的值时,表示禁止使用数组中相应的特殊字符.
/* c_cc characters */	/* c_cc数组中的字符 */
#define VINTR 0		// c_cc[VINTR] = INTR	(^C),\003,中断字符.
#define VQUIT 1		// c_cc[VQUIT] = QUIT	(^\),\034,退出字符.
#define VERASE 2	// c_cc[VERASE] = ERASE	(^H),\177,擦除字符.
#define VKILL 3		// c_cc[VKILL] = KILL	(^U),\025,终止字符(删除行).
#define VEOF 4		// c_cc[VEOF] = EOF	(^D),\004,文件结束字符.
#define VTIME 5		// c_cc[VTIME] = TIME	(\0),\0,定时器值.
#define VMIN 6		// c_cc[VMIN] = MIN	(\1),\1,定时器值.
#define VSWTC 7		// c_cc[VSWTC] = SWTC	(\0),\0,交换字符.
#define VSTART 8	// c_cc[VSTART] = START	(^Q),\021,开始字符.
#define VSTOP 9		// c_cc[VSTOP] = STOP	(^S),\023,停止字符.
#define VSUSP 10	// c_cc[VSUSP] = SUSP	(^Z),\032,挂起字符.
#define VEOL 11		// c_cc[VEOL] = EOL	(\0),\0,行结束字符.
#define VREPRINT 12	// c_cc[VREPRINT] = REPRINT	(^$),\022,重显示字符.
#define VDISCARD 13	// c_cc[VDISCARD] = DISCARD	(^O),\017,丢弃字符.
#define VWERASE 14	// c_cc[VWERASE] = WERASE	(^W),\027,单词擦除字符.
#define VLNEXT 15	// c_cc[VLNEXT] = LNEXT	(^V),\026,下一行字符.
#define VEOL2 16	// c_cc[VEOL2] = EOL2	(\0),\0,行结束字符2.

// termios结构输入模式字段c_iflag各种标志的符号常数.
/* c_iflag bits */	/* c_iflag比特位 */
#define IGNBRK	0000001		// 输入时忽略BREAK条件
#define BRKINT	0000002		// 在BREAK时产生SIGINT信号
#define IGNPAR	0000004		// 忽略奇偶校验出错的字符
#define PARMRK	0000010		// 标记奇偶校验错
#define INPCK	0000020		// 允许输入奇偶校验
#define ISTRIP	0000040		// 屏蔽字符第8位
#define INLCR	0000100		// 输入时将换行符NL映射成回车符CR
#define IGNCR	0000200		// 忽略回车符CR
#define ICRNL	0000400		// 在输入时将回车符映射成换行符NL
#define IUCLC	0001000		// 在输入时将大写字符转换成小写字符
#define IXON	0002000		// 允许开始/停止(XON/XOFF)输出控制
#define IXANY	0004000		// 允许任何字符重启输出
#define IXOFF	0010000		// 允许开始/停止(XON/XOFF)输入控制
#define IMAXBEL	0020000		// 输入队列满时响铃

// termios结构中输出模式字段c_olag各种标志的符号常数.
/* c_oflag bits */	/* c_oflag比特位 */
#define OPOST	0000001		// 执行输出处理
#define OLCUC	0000002		// 在输出时将小写字符转换成大写字符
#define ONLCR	0000004		// 在输出时将换行符NL映射成回车-换行符CR-NL
#define OCRNL	0000010		// 在输出时将回车符CR映射成换行符NL
#define ONOCR	0000020		// 在0列不输出回车符CR
#define ONLRET	0000040		// 换行符NL执行回车符的功能
#define OFILL	0000100		// 延迟时使用填充字符而不使用时间延迟
#define OFDEL	0000200		// 填充字符是ASCII码DEL.如果未设置,则使用ASCII NULL
#define NLDLY	0000400		// 选择换行延迟
#define   NL0	0000000		// 换行延迟类型0
#define   NL1	0000400		// 换行延迟类型1
#define CRDLY	0003000		// 选择回车延迟
#define   CR0	0000000		// 回车延迟类型0
#define   CR1	0001000		// 回车延迟类型1
#define   CR2	0002000		// 回车延迟类型2
#define   CR3	0003000		// 回车延迟类型3
#define TABDLY	0014000		// 选择水平制表延迟
#define   TAB0	0000000		// 水平制表延迟类型0
#define   TAB1	0004000		// 水平制表延迟类型1
#define   TAB2	0010000		// 水平制表延迟类型2
#define   TAB3	0014000		// 水平制表延迟类型3
#define   XTABS	0014000		// 将制表符TAB换成空格,该值表示空格数
#define BSDLY	0020000		// 选择退格延迟
#define   BS0	0000000		// 退格延迟类型0
#define   BS1	0020000		// 退格延迟类型1
#define VTDLY	0040000		// 纵向制表延迟
#define   VT0	0000000		// 纵向制表延迟类型0
#define   VT1	0040000		// 纵向制表延迟类型1
#define FFDLY	0040000		// 选择换页延迟
#define   FF0	0000000		// 换页延迟类型0
#define   FF1	0040000		// 换页延迟类型1

// termios结构中控制模式标志字段c_cflag标志的符号常数(8进制数).
/* c_cflag bit meaning */	/* c_cflag位的含义 */
#define CBAUD	0000017		// 传输速率位屏蔽码
#define  B0		0000000		/* hang up */	/* 挂断线路 */
#define  B50	0000001		// 波特率 50
#define  B75	0000002		// 波特率 75
#define  B110	0000003		// 波特率 110
#define  B134	0000004		// 波特率 134
#define  B150	0000005		// 波特率 150
#define  B200	0000006		// 波特率 200
#define  B300	0000007		// 波特率 300
#define  B600	0000010		// 波特率 600
#define  B1200	0000011		// 波特率 1200
#define  B1800	0000012		// 波特率 1800
#define  B2400	0000013		// 波特率 2400
#define  B4800	0000014		// 波特率 4800
#define  B9600	0000015		// 波特率 9600
#define  B19200	0000016		// 波特率 19200
#define  B38400	0000017		// 波特率 38400
#define  EXTA B19200		// 扩展波特率A
#define  EXTB B38400		// 扩展波物率B
#define CSIZE	0000060		// 字符位宽度屏蔽码
#define   CS5	0000000		// 每字符5位
#define   CS6	0000020		// 每字符6位
#define   CS7	0000040		// 每字符7位
#define   CS8	0000060		// 每字符8位
#define CSTOPB	0000100		// 设置两个停止位,而不是1个
#define CREAD	0000200		// 允许接收
#define PARENB	0000400		// 开启输出时产生奇偶位,输入时进行奇偶校验
#define PARODD	0001000		// 输入/输出校验是奇校验
#define HUPCL	0002000		// 最后进程关闭挂断
#define CLOCAL	0004000		// 忽略调制解调器(modem)控制线路
#define CIBAUD	03600000		/* input baud rate (not used) */	/* 输入波特率(未使用) */
#define CRTSCTS	020000000000		/* flow control */	/* 流控制 */

// termios结构中本地模式标志字段c_lflag的符号常数.
/* c_lflag bits */	/* c_lflag位 */
#define ISIG	0000001		// 当收到字符INTR,QUIT,SUSP或DSUSP,产生相应的信号
#define ICANON	0000002		// 开启规范模式(熟模式)
#define XCASE	0000004		// 若设置了ICANON,则终端是大写字符的
#define ECHO	0000010		// 回显输入字符
#define ECHOE	0000020		// 若设置了ICANON,则ERASE/WERASE将擦除前一字符/单词
#define ECHOK	0000040		// 若设置了ICANON,则KILL字符将擦除当前行
#define ECHONL	0000100		// 若设置了ICANON,则即使ECHO没有开启也回显NL字符
#define NOFLSH	0000200		// 当生成SIGINT和SIGOUIT信号时不刷新输入输出队列,当生成SIGSUSP信号时,刷新输入队列
#define TOSTOP	0000400		// 发送SIGTTOU信号到后台进程的进程组,该后台进程试图写自己的控制终端.
#define ECHOCTL	0001000		// 若设置了ECHO,则除TAB,NL,START和STOP以外的ASCII控制信号将被回显成像^X式样,X值是控制符+0x40
#define ECHOPRT	0002000		// 若设置了ICANON和IECHO,则字符在擦除时将显示
#define ECHOKE	0004000		// 若设置了ICANON,则KILL通过擦除行上的所有字符被回显
#define FLUSHO	0010000		// 输出被刷新.通过键入DISCARD字符,该标志被翻转
#define PENDIN	0040000		// 当下一个字符是读时,输入队列中的所有字符将被重显
#define IEXTEN	0100000		// 开启实现时定义的输入处理

/* modem lines */       /* modem线路信号符号常数 */
#define TIOCM_LE	0x001           // 线路允许（Line Enable）。
#define TIOCM_DTR	0x002           // 数据终端就绪（Data erminal ready）。
#define TIOCM_RTS	0x004           // 请求发送（Request to Send）。
#define TIOCM_ST	0x008           // 串行数据发送（Serial transfer）。
#define TIOCM_SR	0x010           // 串行数据接收（Serial Receive）。
#define TIOCM_CTS	0x020           // 清除发送（Clear To Send）。
#define TIOCM_CAR	0x040           // 载波监测（Carrier Detect）。
#define TIOCM_RNG	0x080           // 响铃指示（Ring indicate）。
#define TIOCM_DSR	0x100           // 数据设备就绪（Data Set Ready）。
#define TIOCM_CD	TIOCM_CAR
#define TIOCM_RI	TIOCM_RNG

/* tcflow() and TCXONC use these */     /* tcflow()和TCXONC使用这些符号 */
#define	TCOOFF		0       // 挂起输出（是“Terminal Control Output OFF”的缩写）。
#define	TCOON		1       // 重启被挂起的输出。
#define	TCIOFF		2       // 系统传输一个STOP字符，使设备停止向系统传输数据。
#define	TCION		3       // 系统传输一个START字符，使设备开始高系统传输数据。

/* tcflush() and TCFLSH use these */    /* tcflush()和TCFLSH使用这些符号常数 */
#define	TCIFLUSH	0       // 清接收到的数据但不读。
#define	TCOFLUSH	1       // 清已写的数据但不传送。
#define	TCIOFLUSH	2       // 清接收到的数据但不读。清已写的数据但不传送。

/* tcsetattr uses these */      /* tcsetattr()使用这些符号常数 */
#define	TCSANOW		0       // 改变立即发生。
#define	TCSADRAIN	1       // 改变在所有已写的输出被传输之后发生。
#define	TCSAFLUSH	2       // 改变在所有已写的输出被传输之后并且在所有接收到但还没有读取的数据
                                // 被丢弃之后发生。

// 以下这些函数在编译环境的函数库libc.a中实现，内核中没有。在函数库实现中，这些函数通过调用系统调用ioctl()来实现。有关
// ioctl()系统调用，请参见fs/ioctl.c程序。
// 返回termios_p所指termios结构中的接收波特率。
extern speed_t cfgetispeed(struct termios *termios_p);
// 返回termios_p所指termios结构中的发送波特率。
extern speed_t cfgetospeed(struct termios *termios_p);
// 将termios_p所指termios结构中的接收波特率设置为speed。
extern int cfsetispeed(struct termios *termios_p, speed_t speed);
// 将termios_p所指termios结构中的发送波特率设置为speed。
extern int cfsetospeed(struct termios *termios_p, speed_t speed);
// 等待fildes所指对象已写输出数据被传送出去。
extern int tcdrain(int fildes);
// 挂起/重启fildes所指对象数据的接收和发送。
extern int tcflow(int fildes, int action);
// 丢弃fildes指定对象所有已写但还没传送以及所有已收到但还没有读取的数据。
extern int tcflush(int fildes, int queue_selector);
// 获取与句柄fildes对应对象的参数，并将其保存在termios_p所指地地方。
extern int tcgetattr(int fildes, struct termios *termios_p);
// 如果终端使用异步串行数据传输，则在一定时间内连续传输一系列0值比特位。
extern int tcsendbreak(int fildes, int duration);
// 使用termios结构指针termios_p所指的数据，设置与终端相关的参数。
extern int tcsetattr(int fildes, int optional_actions,
	struct termios *termios_p);

#endif
