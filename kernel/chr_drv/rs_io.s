/*
 *  linux/kernel/rs_io.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *	rs_io.s
 *
 * This module implements the rs232 io interrupts.
 */
/*
 * 该模块实现rs232输入输出中断处理程序。
 */

.text
.globl rs1_interrupt,rs2_interrupt

# size是读写队列缓冲区的字节长度。该值必须是2的次方，并且必须与tty_io.c中的匹配。
size	= 1024				/* must be power of two !
							   and must match the value
							   in tty_io.c!!! */

/* these are the offsets into the read/write buffer structures */
/* 以下这些是读写缓冲队列结构中的偏移量 */
rs_addr = 0             										# 串行端口号字段偏移（端口是0x3f8或0x2f8）。
head = 4                										# 缓冲区中头指针字段偏移。
tail = 8                										# 缓冲区中尾指针字段偏移。
proc_list = 12          										# 等待该缓冲的进程字段偏移。
buf = 16                										# 缓冲区字段偏移。

# 当一个写缓冲队列满后，内核就会把要往写队列填字符的进程设置为等待状态。当写缓冲队列中还剩余最多256个字符时，中断处理程序
# 就可以唤醒这些等待进程继续往写队列中放字符。
startup	= 256													/* chars left in write queue when we restart it */
                        										/* 当我们重新开始写时，队列里最多还剩余字符个数。 */
/*
 * These are the actual interrupt routines. They look where
 * the interrupt is coming from, and take appropriate action.
 */
/*
 * 这些是实际的中断处理程序。程序首先检查中断的来源，然后执行相应的处理。
 */
## 串行端口1中断处理程序入口点。
# 初始化时rs1_interrupt地址被放入中断描述符0x24中，对应8259A的中断请求IRQ4引脚。
# 这里首先把tty表中串行终端1（串口1）读写缓冲队列指针的地址入栈（tty_io.c），然后跳转到rs_int继续处理。这样做可以
# 让串口1和串口2的处理代码公用。字符缓冲队列结构tty_queue格式请参见include/linux/tty.h。
.align 4
rs1_interrupt:
	pushl $table_list + 8         								# tty表中串口1读写缓冲队列指针地址入栈。
	jmp rs_int
.align 4
rs2_interrupt:
	pushl $table_list + 16        								# tty表中串口2读写缓冲队列指针地址入栈。

# 这段代码首先让段寄存器ds、es指向内核数据段，然后从对应读写缓冲队列data字段取出串行端口基地址。该地址加2即是中断标识
# 寄存器IIR的端口地址。若位0 = 0，表示有需要处理的中断。于是根据位2、位1使用指针跳转表调用相应中断源类型处理子程序。在
# 每个子程序中会在处理完后复位UART的相应中断源。在子程序返回后这段代码会循环判断是否还有其他中断源（位0 = 0？）。如果
# 本次中断还有其他中断源，则IIR的位0仍然是0。于是中断处理程序会再调用相应中断源子程序继续处理。直到引起本次中断的所有中
# 断源都被处理并复位，此时UART会自动地设置IIR的位0 = 1,表示已无待处理的中断，于是中断处理程序即可退出。
rs_int:
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	push %es
	push %ds													/* as this is an interrupt, we cannot */
	pushl $0x10													/* know that bs is ok. Load it */
	pop %ds                 									/* 由于这是一个中断程序，我们不知道ds是否正确， */
	pushl $0x10             									/* 所以加载它们（让ds、es指向内核数据段） */
	pop %es
	movl 24(%esp), %edx      									# 取上面35或39行入栈的相应串口缓冲队列指针地址。
	movl (%edx), %edx        									# 取读缓冲队列结构指针（地址）->edx。
	movl rs_addr(%edx), %edx 									# 取串口1（或串口2）端口基地址->edx。
	addl $2, %edx												/* interrupt ident. reg */  /* 指向中断标识寄存器 */
                                								# 中断标识寄存器端口地址是0x3fa（0x2fa）。
rep_int:
	xorl %eax, %eax
	inb %dx, %al             									# 取中断标识字节，以判断中断来源（有4种中断情况）。
	testb $1, %al            									# 首先判断有无待处理中断（位0 = 0有中断）。
	jne end                 									# 若无待处理中断，则跳转至退出处理处end。
	cmpb $6, %al												/* this shouldn't happen, but ... */    /* 这不会发生，但... */
	ja end                  									# al值大于6，则跳转至end（没有这种状态）。
	movl 24(%esp), %ecx      									# 调用子程序之前所缓冲队列指针地址放入ecx。
	pushl %edx              									# 临时保存中断标识寄存器端口地址。
	subl $2, %edx            									# edx中恢复串口基地址值0x3f8（0x2f8）。
	call *jmp_table(, %eax, 2)									/* NOTE! not *4, bit0 is 0 already */
	# 上面语句是指，当有待处理中断时，al中位0=0,位2、位1是中断类型，因此相当于已经将中断类型乘了2，这里再乘2，获得跳转表对应各
	# 中断类型地址，并跳转到那里去作相应处理。中断来源有4种：modem状态发生变化；要写（发送）字符；要读（接收）字符；线路状态发生
	# 变化。允许发送字符中断通过设置发送保持寄存器标志实现。在serial.c程序中，当写缓冲队列中有数据时，rs_write()函数就会修改
	# 中断允许寄存器内容，添加上发送保持寄存器中断允许标志，从而在系统需要发送字符时引起串行中断发生。
	popl %edx               									# 恢复中断标识寄存器端口地址0x3fa（0x2fa）。
	jmp rep_int             									# 跳转，继续判断有无待处理中断并作相应处理。
end:
	movb $0x20, %al
	outb %al, $0x20												/* EOI */
	pop %ds
	pop %es
	popl %eax
	popl %ebx
	popl %ecx
	popl %edx
	addl $4, %esp												# jump over _table_list entry   # 丢弃队列指针地址。
	iret

# 各中断类型处理子程序地址跳转表，共有4种中断来源：
# modem状态变化中断，写字符中断，读字符中断，线路状态有问题中断。
jmp_table:
	.long modem_status, write_char, read_char, line_status

# 由于mode状态发生变化而引发此次中断。通过读modem状态寄存器MSR对其进行复位操作。
.align 4
modem_status:
	addl $6, %edx												/* clear intr by reading modem status reg */
	inb %dx, %al             									/* 通过读modem状态寄存器进行复位（0x3fe） */
	ret

# 由于线路状态发生变化而引起这次串行中断。通过读线路状态寄存器LSR对其进行复位操作。
.align 4
line_status:
	addl $5, %edx												/* clear intr by reading line status reg. */
	inb %dx, %al             									/* 通过读线路状态寄存器进行复位（0x3fd） */
	ret

# 由于UART芯片接收到字符而引起这次中断。对接收缓冲寄存器执行读操作可复位该中断源。
# 这个子程序将接收到的字符放到读缓冲队列read_q头指针（head）处，并且让该指针前移一个字符位置。若head指针已经到达缓冲区末端，
# 则让其折返到缓冲区开始处。最后调用C函数do_tty_interrupt()（即copy_to_cooked()），把读入的字符经过处理放入规范模式缓冲
# 队列（辅助缓冲队列secondary）中。
.align 4
read_char:
	inb %dx, %al                     							# 读取接收缓冲寄存器RBR中字符->al。
	movl %ecx, %edx                  							# 当前串口缓冲队列指针地址->edx。
	subl $table_list, %edx           							# 当前串口队列指针地址 - 缓冲队列指针表首址 ->edx，
	shrl $3, %edx                    							# 差值/8，得串口号。对于串口1是1,对于串口2是2。
	movl (%ecx), %ecx											# read-queue    # 取读缓冲队列结构地址 ->ecx。
	movl head(%ecx), %ebx            							# 取读队列中缓冲头指针 ->ebx。
	movb %al, buf(%ecx, %ebx)         							# 将字符放在缓冲区中头指针所指位置处。
	incl %ebx                       							# 将头指针前移（右移）一字节。
	andl $size - 1, %ebx               							# 用缓冲区长度对头指针取模操作。
	cmpl tail(%ecx), %ebx            							# 缓冲区头指针与尾指针比较。
	je 1f                           							# 若指针移动后相等，表示缓冲区满，不保存头指针，跳转。
	movl %ebx, head(%ecx)            							# 保存修改过的头指针。
1:	addl $63, %edx                   							# 串口号转换成tty号（63或64）并作为参数入栈。
	pushl %edx
	call do_tty_interrupt           							# 调用tty中断处理C函数（tty_io.c）。
	addl $4, %esp                    							# 丢弃入栈参数，并返回。
	ret

# 由设置了发送保持寄存器允许中断标志而引起此次中断。说明对应串行终端的写字符缓冲队列中有字符需要发送。于是计算出写队列中当前
# 所含字符数，若字符数已小于256个，则唤醒等待写操作进程。然后从写缓冲队列尾部取出一个字符发送，并调整和保存尾指针。如果写缓
# 冲队列已空，则跳转到write_buffer_empty处处理写缓冲队列空的情况。
.align 4
write_char:
	movl 4(%ecx), %ecx											# write-queue       # 取写缓冲队列结构地址->ecx。
	movl head(%ecx), %ebx            							# 队写队列头指针->ebx。
	subl tail(%ecx), %ebx            							# 头指针 - 尾指针 = 队列中字符数。
	andl $size - 1, %ebx										# nr chars in queue
	je write_buffer_empty           							# 若头指针 = 尾指针，说明写队列空，跳转处理。
	cmpl $startup, %ebx              							# 队列中字符数还超过256个？
	ja 1f                           							# 超过则跳转处理。
	movl proc_list(%ecx), %ebx									# wake up sleeping process  # 唤醒等待的进程。
	testl %ebx, %ebx											# is there any?     # 有等待写的进程吗？
	je 1f                           							# 是空的，则向前跳转到标号1处。
	movl $0, (%ebx)                  							# 否则将进程置为可运行状态（唤醒进程）。
1:	movl tail(%ecx), %ebx            							# 取尾指针。
	movb buf(%ecx, %ebx), %al         							# 从缓冲中尾指针处取一字符->al。
	outb %al, %dx                    							# 向端口0x3f8（0x2f8）写到发送保持寄存器中。
	incl %ebx                       							# 尾指针前移。
	andl $size - 1, %ebx               							# 尾指针若到缓冲区末端，则折回。
	movl %ebx, tail(%ecx)            							# 保存已修改的尾指针。
	cmpl head(%ecx), %ebx            							# 尾指针与头指针相比较，
	je write_buffer_empty           							# 若相等，表示队列已空，则跳转。
	ret

# 处理写缓冲队列write_q已空的情况。若有等待写该串行终端的进程则唤醒之，然后屏蔽发送保持寄存器中断，不让发送保持寄存器空时产
# 生中断。
# 如果此时写缓冲队列write_q已空，表示当前无字符需要发送。于是我们应该做两件事情。
# 首先看看有没有进程正等待写队列空出来，如果有就唤醒之。另外，因为现在系统已无字符需要发送，所以此时我们要暂时禁止发送保持寄存器
# THR空时产生中断，因此UART就又会“自动”地来取写缓冲队列中的字符，并发送出去。
.align 4
write_buffer_empty:
	movl proc_list(%ecx), %ebx									# wake up sleeping process  # 唤醒等待的进程。
                                        						# 取等待该队列的进程的指针，并判断是否为空。
	testl %ebx, %ebx											# is there any?     # 有等待的进程码？
	je 1f                           							# 无，则向前跳转到标号1处。
	movl $0, (%ebx)                  							# 否则将进程置为可运行状态（唤醒进程）。
1:	incl %edx                       							# 指向端口0x3f9（0x2f9）。
	inb %dx, %al                     							# 读取中断允许寄存器IER。
	jmp 1f                          							# 稍作延迟。
1:	jmp 1f                  									/* 屏蔽发送保持寄存器空中断（位1） */
1:	andb $0xd, %al												/* disable transmit interrupt */
	outb %al, %dx                    							# 写入0x3f9（0x2f9）。
	ret
