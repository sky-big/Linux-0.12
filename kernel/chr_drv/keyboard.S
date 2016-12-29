/*
 *  linux/kernel/keyboard.S
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *	Thanks to Alfred Leung for US keyboard patches
 *		Wolfgang Thiel for German keyboard patches
 *		Marc Corsini for the French keyboard
 */
/*
 *      感谢Alfred Leung添加了US键盘补丁程序;
 *         Wolfgang Thiel添加了德语键盘补丁程序;
 *         Marc Corsini添加了法文键盘补丁程序。
 */

/* KBD_FINNISH for Finnish keyboards
 * KBD_US for US-type
 * KBD_GR for German keyboards
 * KBD_FR for Frech keyboard
 */
/* KBD_FINNISH 	是芬兰键盘。
 * KBD_US 		是美式键盘。
 * KBD_GR 		是德式键盘。
 * KBD_FR 		是法式键盘。
 */
//#define KBD_FINNISH   								# 定义使用的键盘类型.用于后面选择采用的字符映射码表.
#define KBD_US

.text
.globl keyboard_interrupt								# 声明为全局变量,用于在初始化时设置键盘中断描述符

/*
 * these are for the keyboard read functions
 */
/*
 * 以下这些用于读键盘操作。
 */
// size是键盘缓冲区(缓冲队列)长度(字节数).
/* 值必须是2的次方！并且与tty_io.c中的值匹配！！！！ */
size	= 1024		/* must be a power of two ! And MUST be the same
			   as in tty_io.c !!!! */
// 以下是键盘缓冲队列数据结构tty_queue中的偏移量(include/1/tty.h).
head = 4												# 缓冲区头指针字段在tty_queue结构中的偏移
tail = 8												# 缓冲区尾指针字段偏移
proc_list = 12											# 等待该缓冲队列的进程字段偏移
buf = 16												# 缓冲区字段偏移


// 在本程序中使用了3个标志字节.mode是键盘特殊键(ctrl,alt或caps)的按下状态标志;
// leds是用于表示键盘指示灯的状态标志.e0是当收到扫描码0xe0或0xe1时设置的标志.
// 每个字节标志中各位的含义见如下说明:
// (1) mode是键盘特殊键的按下状态标志.
// 表示大小写转换键(caps),交换键(alt),控制键(ctrl)和换档键(shift)的状态.
//	位7 caps键按下
// 	位6 caps键的状态(应该与leds中对应caps的标志位一样);
//	位5 右alt键按下;
// 	位4 左alt键按下;
//	位3 右ctrl键按下;
//	位2 左ctrl键按下;
//	位1 右shift键按下;
//	位0 左shift键按下;
// (2) leds是用于表示键盘指示灯的状态标志.即表示数字锁定键(num-lock),大小写转换键(caps-lock)和滚动锁定键(scroll-lock)的
// 发光二极管(LED)状态.
//	位7-3 全0不用;
//	位2 cas-lock;
//	位1 num-lock(初始置1,也即设置数字锁定键(num-lock)发光管为亮);
//	位0 scroll-lock
// (3) 当扫描码是0xe0或0xe1时,置该标志.表示其后还跟随着1个或2个字符扫描码.通常若收到扫描码0xe0则意味着还有一个字符跟随其后;若收
// 扫描码0xe1则表示后面还跟随着2个字符.
//	位1 =1 收到0xe1标志
//	位0 =1 收到0xe0标志

mode:	.byte 0		/* caps, alt, ctrl and shift mode */
leds:	.byte 2		/* num-lock, caps, scroll-lock mode (nom-lock on) */
e0:	.byte 0


/*
 *  con_int is the real interrupt routine that reads the
 *  keyboard scan-code and converts it into the appropriate
 *  ascii character(s).
 */
/*
 *  con_int是实际的中断处理子程序，用于读键盘扫描码并将其转换成相应的ascii字符。
 *  [ 注：这段英文注释已过时。 ]
 */
// 键盘中断处理程序入口点.
// 接收到用户的一个按键操作时,就会向中断控制器发出一个键盘中断信号当键盘控制器IRQ1.当CPU响应该请求时就会执行键盘中断处理程序.该中断处理
// 程序会从键盘控制器相应端口(0x60)读入按键扫描码,并调用对应的扫描码子程序进行处理.
// 首先从端口0x60读取当前按键的扫描码.然后判断该扫描码是否是0xe0或0xe1,如果是的话就立刻对键盘控制器作出应答,并向中断控制器发送中断结束(EOI)
// 信号,以允许键盘控制器能继续产生中断信号,从而让我们来接收后续的字符.如果接收到的扫描码不是这两个特殊扫描码,我们就根据扫描码值调用按键跳转
// 表key_table中相应按键处理子程序,把扫描码对应的字符放入读字符缓冲队列read_q中,然后,在对键盘控制器作出应答并发送EOI信号之后,调用函数
// do_tty_interrupt()(实际上是调用copy_to_cooked())把read_q中的字符经过处理后放到secondary辅助队列中.
keyboard_interrupt:
	pushl %eax
	pushl %ebx
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	movl $0x10, %eax								# 将ds,es段寄存器置为内核数据段.
	mov %ax, %ds
	mov %ax, %es
	movl blankinterval, %eax						# 预置黑屏时间计数值为blankcount(滴答数)
	movl %eax, blankcount
	xorl %eax, %eax									/* %eax is scan code */     /* eax中是扫描码 */
	inb $0x60, %al									# 读取扫描码->al
	cmpb $0xe0, %al									# 扫描码是0xe0码?若是则跳转到设置e0标志代码处.
	je set_e0
	cmpb $0xe1, %al									# 扫描码是0xe1吗?若是则跳转到设置e1标志代码处.
	je set_e1
	call *key_table(, %eax, 4)						# 调用键处理程序key_table + eax*4
	movb $0, e0										# 返回之后复位e0标志.

# 下面这段代码针对使用8255A的PC标准键盘电路进行硬件复位处理.端口0x61是8255A输出口B的地址,该输出端口的第7位(PB7)用于禁止
# 和允许对键盘数据的处理.
# 这段程序用于对收到的扫描码做出应答.方法是首先禁止键盘,然后立刻重新允许键盘工作
e0_e1:	inb $0x61, %al								# 取PPI端口B状态,其位7用于允许/禁止(0/1)键盘.
	jmp 1f											# 延迟一会
1:	jmp 1f
1:	orb $0x80, %al									# al位7置位(禁止键盘工作)
	jmp 1f
1:	jmp 1f
1:	outb %al, $0x61									# 使PPI PB7位置位
	jmp 1f
1:	jmp 1f
1:	andb $0x7F, %al									# al位7复位
	outb %al, $0x61									# 使PPT PB7位复位(允许键盘工作).
	movb $0x20, %al									# 向8259中断芯片发送EOI(中断结束)信号.
	outb %al, $0x20
	pushl $0										# 控制台tty号 = 0,作为参数入栈.
	call do_tty_interrupt							# 将收到数据转换成规范模式并存放在规范字符缓冲队列中.
	addl $4, %esp									# 丢弃入栈的参数,弹出保留的寄存器,并中断返回.
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax
	iret
set_e0:	movb $1, e0									# 由到扫描前导码0xe0时,设置e0标志(位0).
	jmp e0_e1
set_e1:	movb $2, e0									# 收到扫描前导码0xe1时,设置e1标	志(位1).
	jmp e0_e1

/*
 * This routine fills the buffer with max 8 bytes, taken from
 * %ebx:%eax. (%edx is high). The bytes are written in the
 * order %al,%ah,%eal,%eah,%bl,%bh ... until %eax is zero.
 */
/*
 * 下面子程序把ebx:eax中的最多8个字符添入缓冲队列中。(ebx是高字)所写入字符的顺序是al,ah,eal,eah,bl,bh...
 * 直到eax等于0.
 */
# 首先从缓冲队列地址表table_list(tty_io.c)取控制台的读缓冲队列read_q地址.然后把al寄存器中的字符复制到读队列头指针处并把头指针前移
# 1字节位置.若头指针移出读缓冲区的末端,就让其回绕到缓冲区开始处.然后再看看此时缓冲队列是否已满,即比较一下队列头指针是否与尾指针相等(相等
# 表示满).如果已满,就把ebx:eax中可能还有的其余字符全部抛弃掉.如果缓冲区还未满,就把ebx:eax中数据联合右移8位(即把ah值移到al,bl->ah,
# bh->bl),然后重复上面对al的处理过程.直到所有都处理完后,就保存当前头指针,再检查一下是否有进程等待着读队列,如果有则唤醒之.
put_queue:
	pushl %ecx
	pushl %edx
	movl table_list, %edx							# read-queue for console	# 取控制台tty结构中读缓冲队列指针
	movl head(%edx), %ecx							# 取队列头指针->ecx.
1:	movb %al, buf(%edx, %ecx)						# 将al中的字符放入头指针位置处.
	incl %ecx										# 头指针前移1字节
	andl $size - 1, %ecx							# 调整头指针.若超出缓冲区末端则绕回开始处.
	cmpl tail(%edx), %ecx							# buffer full - discard everything
                                        			# 头指针==尾指针吗?(即缓冲队列满了吗?)
	je 3f											# 如果已满,则后面未放入的字符全抛弃
	shrdl $8, %ebx, %eax							# 将ebx中8个位右移8位到eax中,ebx不变
	je 2f											# 还有字符吗?若没有(等于0)则跳转
	shrl $8, %ebx									# 将ebx值右移8位,并跳转到标号1继续操作.
	jmp 1b
2:	movl %ecx, head(%edx)							# 若已将所有字符都放入队列,则保存头指针.
	movl proc_list(%edx), %ecx						# 该队列的等待进程指针
	testl %ecx, %ecx								# 检测是否有等待该队列的进程
	je 3f											# 无,则跳转
	movl $0, (%ecx)									# 有,则唤醒进程(置该进程为就绪状态)
3:	popl %edx
	popl %ecx
	ret

# 从这里开始是键跳转表key_table中指针对应的各个按键(或松健)处理子程序.
# 下面这段代码根据ctrl或alt的扫描码,分别设置模式标志mode中相应位.如果在该扫描码之前收到过0xe0扫描码(e0标志置位),则说明按下的
# 是键盘右边的ctrl或alt键,则对应设置ctrl或alt在模式标志mode中的位.
ctrl:
	movb $0x04, %al									# 0x4是mode中左ctrl键对应的位(位2)
	jmp 1f
alt:
	movb $0x10, %al									# 0x10是mode中左alt键对应的位(位4)
1:	cmpb $0, e0										# e0置位了吗(按下右边的ctrl/alt键吗)?
	je 2f											# 不是则转.
	addb %al, %al									# 是,则改成置相应右键标志位(位3或位5)
2:	orb %al, mode									# 设置mode标志中对应的位
	ret

# 这段代码处理ctrl或alt松开时的扫描码,复位模式标志mode中的对应位.在处理时要根据e0标志是否置位来判断是否键盘右边的ctrl或alt键.
unctrl:
	movb $0x04, %al									# mode中左ctrl键对应的位(位2)
	jmp 1f
unalt:
	movb $0x10, %al									# 0x10是mode中左alt键对应的位(位4)
1:	cmpb $0, e0										# e0置位了码(释放的是右边的ctrl/alt键吗)?
	je 2f											# 不是,则转
	addb %al, %al									# 是,则改成复位相应右键的标志位(位3或位5).
2:	notb %al										# 复位mode标志中对应的位.
	andb %al, mode
	ret


# 这段代码处理左,右shift键按下和松开时的扫描码,分别设置和复位mode中的相应位.
lshift:
	orb $0x01, mode									# 在左shift键按下,设置mode中位0.
	ret
unlshift:
	andb $0xfe, mode								# 是左shift键松开,复位mode中位0.
	ret
rshift:
	orb $0x02, mode									# 是右shift键按下,设置mode中位1.
	ret
unrshift:
	andb $0xfd, mode								# 是右shift键松开,复位mode中位1.
	ret


# 这段代码对收到caps键扫描码进行处理.通过mode中位7可以知道caps键当前是否正处于在按下状态.若是则返回,否则就翻转mode标志中caps键按下的
# 位(位6)和leds标志中caps-lock位(位2),设置mode标志中caps键已按下标志位(位7).
caps:
	testb $0x80, mode								# 测试mode中位7是否置位(即在按下状态).
	jne 1f											# 如果已处于按下状态,则返回
	xorb $4, leds									# 翻转leds标志中caps-lock位(位2)
	xorb $0x40, mode								# 翻转mode标志中caps键按下的位(位6)
	orb $0x80, mode									# 设置mode标志中caps键已按下标志位(位7)

# 这段代码根据leds标志,开启或关闭LED指示器.
set_leds:
	call kb_wait									# 等待键盘控制器输入缓冲空
	movb $0xed, %al									/* set leds command */
	outb %al, $0x60									# 发送键盘命令0xed到0x60端口
	call kb_wait
	movb leds, %al									# 取leds标志,作为参数.
	outb %al, $0x60									# 发送该参数
	ret

uncaps:	andb $0x7f, mode							# caps键松开,则复位mode中的对应位(位7)
	ret

scroll:
	testb $0x03, mode								# 若此时shift键也同时按下,则
	je 1f
	call show_mem									# 显示内存状态信息(mm/memory.c)
	jmp 2f
1:	call show_state									# 否则显示进程状态信息(kernel/sched.c)
2:	xorb $1, leds									# scroll键按下,则翻转leds中对应位(位0)
	jmp set_leds									# 根据leds标志重新开启或关闭LED指示器
num:
	xorb $2, leds									# num键按下,则翻转leds中的对应位(位1)
	jmp set_leds									# 根据leds标志重新开启或关闭LED指示器.

/*
 *  curosr-key/numeric keypad cursor keys are handled here.
 *  checking for numeric keypad etc.
 */
/*
 *  这里处理方向键/数字小键盘方向键，检测数字小键盘等。
 */
cursor:
	subb $0x47, %al									# 扫描码是是数字键盘上的键(其扫描码>=0x47)发出的?
	jb 1f											# 如果小于则不处理,返回
	cmpb $12, %al									# 如果扫描码>0x53(0x53-0x47 = 12),则
	ja 1f											# 表示扫描码值超过83(0x53),不处理,返回.
	# 若等于12,说明del键已被按下,则继续判断ctrl和alt是否也被同时按下.
	jne cur2										/* check for ctrl-alt-del */
	testb $0x0c, mode								# 有ctrl键按下了码?无,则跳转
	je cur2
	testb $0x30, mode								# 有alt键按下吗?
	jne reboot										# 有,则跳转到重启动处理

cur2:
	cmpb $0x01, e0									/* e0 forces cursor movement */     # e0标志置位了吗?
	je cur											# 置位了,则跳转光标移动处理cur
	testb $0x02, leds								/* not num-lock forces cursor */    # 测试leds中标志num-lock键标志是否置位
	je cur											# 若没有置位(num的LED不亮),则也处理光标移动.
	testb $0x03, mode								/* shift forces cursor */           # 测试模式标志mode中shift按下标志
	jne cur											# 如果有shift键按下,则也进行光标移动处理
	xorl %ebx, %ebx									# 否则查询小数字表,取键的数字ASCII吗
	movb num_table(%eax), %al						# 以eax作为索引值,取对应数字字符->al
	jmp put_queue									# 字符放入缓冲队列中.由于要放入队列的字符数<=4,在因此在执行put_pueue前需把ebx清零.
1:	ret

# 这段代码处理光标移动或插入删除按键.
cur:
	movb cur_table(%eax), %al						# 取光标字符表中相应键的代表字符->al
	cmpb $'9, %al                    				# 若字符<='9'(5,6,2或3),说明是上一页,下一页,插入或删除键,则功能字符序列中要添入字符'~'.
	ja ok_cur                       				# 不过本内核并没有对它们进行识别和处理.
	movb $'~, %ah

ok_cur:
	shll $16, %eax									# 将ax中内容移到eax高字中
	movw $0x5b1b, %ax								# 把'esc ['放入ax,与eax高字中字符组成移动序列.
	xorl %ebx, %ebx									# 由于只需把eax中字符放入队列,因此需要把ebx清零.
	jmp put_queue									# 将该字符放入缓冲队列中.

#if defined(KBD_FR)
num_table:
	.ascii "789 456 1230."  						# 数字小键盘上对应的数字ASCII码表。
#else
num_table:
	.ascii "789 456 1230,"
#endif
cur_table:
	.ascii "HA5 DGC YB623"							# 小键盘上方向键或插入删除键对应的移动表示字符表.

/*
 * this routine handles function keys
 */
/*
 * 下面子程序处理功能键。
 */
// 把功能键扫描码变换成转义字符序列并存放到读队列中。
func:
	subb $0x3B, %al									# 键'F1'的扫描码是0x3B,因此al中是功能键索引号
	jb end_func										# 如果扫描码小于0x3b,则不处理,返回
	cmpb $9, %al									# 功能键是F1~F10
	jbe ok_func										# 是,则跳转
	subb $18, %al									# 是功能键F11,F12吗?F11,F12扫描码是0x57,0x58.
	cmpb $10, %al									# 是功能键F11?
	jb end_func										# 不是,则不处理,返回.
	cmpb $11, %al									# 是功能键F12?
	ja end_func										# 不是,则不处理,返回

ok_func:
	testb $0x10, mode								# 左alt键也同时按下了吗?
	jne alt_func									# 是则跳转处理更换虚拟控制终端.
	cmpl $4, %ecx									/* check that there is enough room */
	jl end_func										# 需要放入4个字符,如果放不下,则返回
	movl func_table(, %eax, 4), %eax				# 取功能键对应字符序列.
	xorl %ebx, %ebx									# 因要放入队列字符=4,因此执行put_queue之前需把ebx清零.
	jmp put_queue

# 处理alt + Fn组合键,改变虚拟控制终端.此时eax中是功能键索引号(F1~0),对应虚拟控制终端号.
alt_func:
	pushl %eax										# 虚拟控制终端号入栈,作为参数.
	call change_console								# 更改当前虚拟控制终端(chr_dev/tty_io.c).
	popl %eax										# 丢弃参数.
end_func:
	ret

/*
 * function keys send F1:'esc [ [ A' F2:'esc [ [ B' etc.
 */
/*
 * 功能键发送的扫描码，F1键为：'esc [ [ A'，F2键为'esc [ [ B'等。
 */
func_table:
	.long 0x415b5b1b,0x425b5b1b,0x435b5b1b,0x445b5b1b
	.long 0x455b5b1b,0x465b5b1b,0x475b5b1b,0x485b5b1b
	.long 0x495b5b1b,0x4a5b5b1b,0x4b5b5b1b,0x4c5b5b1b

# 扫描码-ASCII字符映射表
# 根据前面定义的键盘类型(FINNISH,US,GERMEN,FRANCH),将相应键的扫描码映射到ASCII字符.
#if	defined(KBD_FINNISH)    /* 以上是芬兰语键盘的扫描码映射表. /*
key_map:
	.byte 0,27                  # 扫描码0x00,0x01对应的ASCII码；
	.ascii "1234567890+'"       # 扫描码0x02,...0x0c,0x0d对应的ASCII码，以下类似。
	.byte 127,9
	.ascii "qwertyuiop}"
	.byte 0,13,0
	.ascii "asdfghjkl|{"
	.byte 0,0
	.ascii "'zxcvbnm,.-"
	.byte 0,'*,0,32		/* 36-39 */     /* 扫描码0x36~0x39对应的ASCII码 */
	.fill 16,1,0		/* 3A-49 */     /* 扫描码0x3A~0x49对应的ASCII码 */
	.byte '-,0,0,0,'+	/* 4A-4E */     /* 扫描码0x4A~0x4E对应的ASCII码 */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */     /* 扫描码0x4F~0x55对应的ASCII码 */
	.byte '<
	.fill 10,1,0

shift_map:                          # Shift键同时按下时的映射表。
	.byte 0,27
	.ascii "!\"#$%&/()=?`"
	.byte 127,9
	.ascii "QWERTYUIOP]^"
	.byte 13,0
	.ascii "ASDFGHJKL\\["
	.byte 0,0
	.ascii "*ZXCVBNM;:_"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '>
	.fill 10,1,0

alt_map:                            # Alt键同时按下时的映射表。
	.byte 0,0
	.ascii "\0@\0$\0\0{[]}\\\0"
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte '~,13,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0,0,0		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte 0,0,0,0,0		/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '|
	.fill 10,1,0

#elif defined(KBD_US)               /* 以下是美式键盘的扫描码映射表 */

key_map:
	.byte 0,27
	.ascii "1234567890-="
	.byte 127,9
	.ascii "qwertyuiop[]"
	.byte 13,0
	.ascii "asdfghjkl;'"
	.byte '`,0
	.ascii "\\zxcvbnm,./"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '<
	.fill 10,1,0


shift_map:
	.byte 0,27
	.ascii "!@#$%^&*()_+"
	.byte 127,9
	.ascii "QWERTYUIOP{}"
	.byte 13,0
	.ascii "ASDFGHJKL:\""
	.byte '~,0
	.ascii "|ZXCVBNM<>?"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '>
	.fill 10,1,0

alt_map:
	.byte 0,0
	.ascii "\0@\0$\0\0{[]}\\\0"
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte '~,13,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0,0,0		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte 0,0,0,0,0		/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '|
	.fill 10,1,0

#elif defined(KBD_GR)               /* 以下是德语键盘的扫描码映射表 */

key_map:
	.byte 0,27
	.ascii "1234567890\\'"
	.byte 127,9
	.ascii "qwertzuiop@+"
	.byte 13,0
	.ascii "asdfghjkl[]^"
	.byte 0,'#
	.ascii "yxcvbnm,.-"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '<
	.fill 10,1,0


shift_map:
	.byte 0,27
	.ascii "!\"#$%&/()=?`"
	.byte 127,9
	.ascii "QWERTZUIOP\\*"
	.byte 13,0
	.ascii "ASDFGHJKL{}~"
	.byte 0,''
	.ascii "YXCVBNM;:_"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '>
	.fill 10,1,0

alt_map:
	.byte 0,0
	.ascii "\0@\0$\0\0{[]}\\\0"
	.byte 0,0
	.byte '@,0,0,0,0,0,0,0,0,0,0
	.byte '~,13,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0,0,0		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte 0,0,0,0,0		/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '|
	.fill 10,1,0


#elif defined(KBD_FR)               /* 以下是法语键盘的扫描码映射表 */

key_map:
	.byte 0,27
	.ascii "&{\"'(-}_/@)="
	.byte 127,9
	.ascii "azertyuiop^$"
	.byte 13,0
	.ascii "qsdfghjklm|"
	.byte '`,0,42		/* coin sup gauche, don't know, [*|mu] */
	.ascii "wxcvbn,;:!"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '<
	.fill 10,1,0

shift_map:
	.byte 0,27
	.ascii "1234567890]+"
	.byte 127,9
	.ascii "AZERTYUIOP<>"
	.byte 13,0
	.ascii "QSDFGHJKLM%"
	.byte '~,0,'#
	.ascii "WXCVBN?./\\"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '>
	.fill 10,1,0

alt_map:
	.byte 0,0
	.ascii "\0~#{[|`\\^@]}"
	.byte 0,0
	.byte '@,0,0,0,0,0,0,0,0,0,0
	.byte '~,13,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0,0,0		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte 0,0,0,0,0		/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '|
	.fill 10,1,0

#else
#error "KBD-type not defined"
#endif
/*
 * do_self handles "normal" keys, ie keys that don't change meaning
 * and which have just one character returns.
 */
/*
 * do_self用于处理“普通”键，也即含义没有变化并且只有一个字符返回的键。
 */
# 首先根据mode标志选择alt_map,shift_map或key_map映射表之一.
do_self:
	lea alt_map, %ebx								# 取alt键同时按下时的映射表基址alt_map.
	testb $0x20, mode								/* alt-gr */    /* 右alt键同时按下了? */
	jne 1f                  						# 是，则向前跳转到标号1处。
	lea shift_map, %ebx								# 取shift键同时按下时的映射表基址hift_mapt
	testb $0x03, mode								# 右shift键同时按下吗?
	jne 1f											# 有,则向前跳转到标号1处去映射字符
	lea key_map, %ebx								# 否则使用普通映射表Key_map.
# 然后根据扫描码取映射表中对应的ASCII字符.若没有对应字符,则返回(转none).
1:	movb (%ebx, %eax), %al							# 将扫描码作为索引值,取对应的ASCII码->al
	orb %al, %al									# 检测看是否有对应的ASCII码
	je none											# 若没有(对应的ASCII码=0),则返回.
	# 若ctrl键已按下或caps键锁定,并且字符在'a'--'}'(0x61--0x7D)范围内,则将其转成大写字符(0x41~0x5D).
	testb $0x4c, mode								/* ctrl or caps */  /* 控制键已按下或caps亮? */
	je 2f											# 没有,则向前跳转到标号2处
	cmpb $'a, %al									# 将al中的字符与'a'比较
	jb 2f											# 若al值<'a',则转标号2处
	cmpb $'}, %al									# 将al中的字符与'}'比较
	ja 2f											# 若al值>'}',则转标号2处
	subb $32, %al									# 将al转换为大写字符(减0x20).
# 若ctrl键已按下,并且字符在'`'--'_'(0x40--0x5f)之间,即是大写字符,则将其转换为控制字符(0x00--0x1F).
2:	testb $0x0c, mode								/* ctrl */  /* ctrl键同时按下了吗? */
	je 3f											# 若没有则转标号3
	cmpb $64, %al									# 将al与'@'(64)字符比较,即判断字符所属范围
	jb 3f											# 若值<'@',则转标号3
	cmpb $64 + 32, %al								# 将al与'`'(96)字符比较,即判断字符所属范围
	jae 3f											# 若值>='`',则转标号3
	subb $64, %al									# 否则al减0x40,转换为0x00--0x1f的控制字符.
# 若左alt键同时按下,则将字符的位7置位.即此时值大于0x7f的扩展字符集中的字符.
3:	testb $0x10, mode								/* left alt */  /* 左alt键同时按下? */
	je 4f											# 没有,则转标号4.
	orb $0x80, %al									# 字符的位7置位
# 将al中的字符放入读缓冲队列中.
4:	andl $0xff, %eax								# 清eax的高字和ah.
	xorl %ebx, %ebx									# 由于放入队列字符数<=4,因此需把ebx清零.
	call put_queue									# 将字符放入缓冲队列中.
none:	ret

/*
 * minus has a routine of it's own, as a 'E0h' before
 * the scan code for minus means that the numeric keypad
 * slash was pushed.
 */
/*
 * 减号有它自己的处理子程序，因为在减号扫描码之前的0xe0意味着按下了数字小键盘的斜杠键。
 */
# 注意,对于芬兰语和德语键盘,扫描码0x35对应的是'-'键.
minus:	cmpb $1,e0		# e0标志置位了吗?
	jne do_self		# 没有,则调用do_self对减号符进行普通处理.
	movl $'/,%eax		# 否则用'/'替换减号'-'->al.
	xorl %ebx,%ebx		# 由于放入队列字符数<=4,因此需把ebx清零.
	jmp put_queue		# 并将字符放入缓冲队列中.

/*
 * This table decides which routine to call when a scan-code has been
 * gotten. Most routines just call do_self, or none, depending if
 * they are make or break.
 */
key_table:
	.long none,do_self,do_self,do_self	/* 00-03 s0 esc 1 2 */
	.long do_self,do_self,do_self,do_self	/* 04-07 3 4 5 6 */
	.long do_self,do_self,do_self,do_self	/* 08-0B 7 8 9 0 */
	.long do_self,do_self,do_self,do_self	/* 0C-0F + ' bs tab */
	.long do_self,do_self,do_self,do_self	/* 10-13 q w e r */
	.long do_self,do_self,do_self,do_self	/* 14-17 t y u i */
	.long do_self,do_self,do_self,do_self	/* 18-1B o p } ^ */
	.long do_self,ctrl,do_self,do_self	/* 1C-1F enter ctrl a s */
	.long do_self,do_self,do_self,do_self	/* 20-23 d f g h */
	.long do_self,do_self,do_self,do_self	/* 24-27 j k l | */
	.long do_self,do_self,lshift,do_self	/* 28-2B { para lshift , */
	.long do_self,do_self,do_self,do_self	/* 2C-2F z x c v */
	.long do_self,do_self,do_self,do_self	/* 30-33 b n m , */
	.long do_self,minus,rshift,do_self	/* 34-37 . - rshift * */
	.long alt,do_self,caps,func		/* 38-3B alt sp caps f1 */
	.long func,func,func,func		/* 3C-3F f2 f3 f4 f5 */
	.long func,func,func,func		/* 40-43 f6 f7 f8 f9 */
	.long func,num,scroll,cursor		/* 44-47 f10 num scr home */
	.long cursor,cursor,do_self,cursor	/* 48-4B up pgup - left */
	.long cursor,cursor,do_self,cursor	/* 4C-4F n5 right + end */
	.long cursor,cursor,cursor,cursor	/* 50-53 dn pgdn ins del */
	.long none,none,do_self,func		/* 54-57 sysreq ? < f11 */
	.long func,none,none,none		/* 58-5B f12 ? ? ? */
	.long none,none,none,none		/* 5C-5F ? ? ? ? */
	.long none,none,none,none		/* 60-63 ? ? ? ? */
	.long none,none,none,none		/* 64-67 ? ? ? ? */
	.long none,none,none,none		/* 68-6B ? ? ? ? */
	.long none,none,none,none		/* 6C-6F ? ? ? ? */
	.long none,none,none,none		/* 70-73 ? ? ? ? */
	.long none,none,none,none		/* 74-77 ? ? ? ? */
	.long none,none,none,none		/* 78-7B ? ? ? ? */
	.long none,none,none,none		/* 7C-7F ? ? ? ? */
	.long none,none,none,none		/* 80-83 ? br br br */
	.long none,none,none,none		/* 84-87 br br br br */
	.long none,none,none,none		/* 88-8B br br br br */
	.long none,none,none,none		/* 8C-8F br br br br */
	.long none,none,none,none		/* 90-93 br br br br */
	.long none,none,none,none		/* 94-97 br br br br */
	.long none,none,none,none		/* 98-9B br br br br */
	.long none,unctrl,none,none		/* 9C-9F br unctrl br br */
	.long none,none,none,none		/* A0-A3 br br br br */
	.long none,none,none,none		/* A4-A7 br br br br */
	.long none,none,unlshift,none		/* A8-AB br br unlshift br */
	.long none,none,none,none		/* AC-AF br br br br */
	.long none,none,none,none		/* B0-B3 br br br br */
	.long none,none,unrshift,none		/* B4-B7 br br unrshift br */
	.long unalt,none,uncaps,none		/* B8-BB unalt br uncaps br */
	.long none,none,none,none		/* BC-BF br br br br */
	.long none,none,none,none		/* C0-C3 br br br br */
	.long none,none,none,none		/* C4-C7 br br br br */
	.long none,none,none,none		/* C8-CB br br br br */
	.long none,none,none,none		/* CC-CF br br br br */
	.long none,none,none,none		/* D0-D3 br br br br */
	.long none,none,none,none		/* D4-D7 br br br br */
	.long none,none,none,none		/* D8-DB br ? ? ? */
	.long none,none,none,none		/* DC-DF ? ? ? ? */
	.long none,none,none,none		/* E0-E3 e0 e1 ? ? */
	.long none,none,none,none		/* E4-E7 ? ? ? ? */
	.long none,none,none,none		/* E8-EB ? ? ? ? */
	.long none,none,none,none		/* EC-EF ? ? ? ? */
	.long none,none,none,none		/* F0-F3 ? ? ? ? */
	.long none,none,none,none		/* F4-F7 ? ? ? ? */
	.long none,none,none,none		/* F8-FB ? ? ? ? */
	.long none,none,none,none		/* FC-FF ? ? ? ? */

/*
 * kb_wait waits for the keyboard controller buffer to empty.
 * there is no timeout - if the buffer doesn't empty, we hang.
 */
/*
 * 子程序kb_wait用于等待键盘控制器缓冲空。不存在超时处理 - 如果缓冲永远不空的话，程序就会永远等待（死掉）。
 */
kb_wait:
	pushl %eax
1:	inb $0x64, %al								# 读键盘控制器状态
	testb $0x02, %al							# 测试输入缓冲器是否为空(等于0).
	jne 1b										# 若不空,则跳转循环等待.
	popl %eax
	ret
/*
 * This routine reboots the machine by asking the keyboard
 * controller to pulse the reset-line low.
 */
/*
 * 该子程序通过设置键盘控制器，向复位线输出负脉冲，使系统复位重启（reboot）。
 */
# 该子程序往物理内存地址0x472处写值0x1234.该位置是启动模式(reboot_mode)标志字.
# 在启动过程中ROM BIOS 读取该启动模式标志值并根据其值来指导下一步的执行.如果该值是0x1234,则BIOS就会跳过内存检测过程而执行
# 热启动(Warm-boot)过程.如果若该值为0,则执行冷启动(Cold-boot)过程.
reboot:
	call kb_wait								# 先等待键盘控制器输入缓冲器空.
	movw $0x1234, 0x472							/* don't do memory check */     /* 不检测内存条 */
	movb $0xfc, %al								/* pulse reset and A20 low */
	outb %al, $0x64								# 向系统复位引脚和A20线输出负脉冲.
die:	jmp die									# 停机
