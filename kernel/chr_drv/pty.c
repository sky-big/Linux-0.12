/*
 *  linux/kernel/chr_drv/pty.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *	pty.c
 *
 * This module implements the pty functions
 *	void mpty_write(struct tty_struct * queue);
 *	void spty_write(struct tty_struct * queue);
 */
/*
 *      pty.c
 * 这个文件实现了伪终端通信函数。
 *      void mpty_write(struct tty_struct * queue);
 *      void spty_write(struct tty_struct * queue);
 */
#include <linux/tty.h>
#include <linux/sched.h>
#include <asm/system.h>
#include <asm/io.h>

// 伪终端写函数。
// 参数：from - 源伪终端结构；to - 目的伪终端结构。
static inline void pty_copy(struct tty_struct * from, struct tty_struct * to)
{
	char c;

	// 判断源终端是否停止或源终端写队列是否为空。如果源终端未停止，并且源终端写队列不为空，则循环处理之。
	while (!from->stopped && !EMPTY(from->write_q)) {
		// 判断目的终端读队列是否已满，如果已满则先调用copy_to_cooked函数处理目的终端读队列，然后重新循环处理。
		if (FULL(to->read_q)) {
			// 判断目的终端辅助队列是否已满，如果已满则直接退出循环，不再处理源终端写队列中的数据。
			if (FULL(to->secondary))
				break;
			copy_to_cooked(to);     						// 把读队列中的字符处理成成规范模式字符序列放入辅助队列。
			continue;
		}
		GETCH(from->write_q, c);         					// 从源终端写队列中取一个字符，放入c，
		PUTCH(c, to->read_q);            					// 然后把c中的字符放入目的终端读队列中。
		// 判断当前进程是否有信号需要处理，如果有，则退出循环。
		if (current->signal & ~current->blocked)
			break;
	}
	copy_to_cooked(to);     								// 把读队列中的字符处理成成规范模式字符序列放入辅助队列。
	wake_up(&from->write_q->proc_list);     				// 唤醒等待源终端写队列的进程，如果有。
}

/*
 * This routine gets called when tty_write has put something into
 * the write_queue. It copies the input to the output-queue of it's
 * slave.
 */
/*
 * 这个函数被调用时，tty_write函数已经把一些字符放到写队列write_queue中。它将复制这些输入到它的从伪终端的
 * 输出队列中。
 */
// 主伪终端写函数。
void mpty_write(struct tty_struct * tty)
{
	int nr = tty - tty_table;       						// 获取终端号

	// 终端号除以64取整如果为2则是“主伪终端”，否则显示出错信息。
	if ((nr >> 6) != 2)
		printk("bad mpty\n\r");
	else
		pty_copy(tty, tty + 64);   							// 调用伪终端写函数。
}

// 从伪终端写函数。
void spty_write(struct tty_struct * tty)
{
	int nr = tty - tty_table;       						// 获取终端号

	// 终端号除以64取整如果为3则是“从伪终端”，否则显示出错信息。
	if ((nr >> 6) != 3)
		printk("bad spty\n\r");
	else
		pty_copy(tty, tty - 64);   							// 调用伪终端写函数。
}
