/*
 *  linux/kernel/panic.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * This function is used through-out the kernel (includeinh mm and fs)
 * to indicate a major problem.
 */
/*
 * 该函数在整个内核中使用(包括在头文件*.h,内存管理程序mm和文件系统fs中),用以指出主要的出错问题.
 */
#include <linux/kernel.h>                           // 内核头文件。含有一些内核常用函数的原型定义。
#include <linux/sched.h>	                        // 调度程序头文件,定义了任务结构task_struct,初始任务0的数据,还有一些有关描述符
				                                    // 参数设置和获取的嵌入式汇编函数宏语句.

void sys_sync(void);	                           /* it's really int */	/* 实际上是整形int(fs/buffer.c) */

// 该函数用来显示内核中出现在重大错误信息,并运行文件系统同步函数,然后进入死循环--死机.
// 如果当前进程是任务0的话,还说明是交换任务出错,并且还没有运行文件系统同步函数.
// 函数名前的关键字volatile用于告诉编译器gcc该函数不会返回.这样可让gcc产生更好一些的代码,更重要的是使用这个关键字
// 以避免产生某些(未初始化变量的)假警告信息.
// 等同于现在gcc的函数属性说明:void panic(const char *s) __attribute__((noreturn));
void panic(const char * s)
{
	printk("Kernel panic: %s\n\r", s);
	if (current == task[0])
		printk("In swapper task - not syncing\n\r");
	else
		sys_sync();
	for(;;);
}

