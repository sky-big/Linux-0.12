/*
 *  linux/kernel/printk.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * When in kernel-mode, we cannot use printf, as fs is liable to
 * point to 'interesting' things. Make a printf with fs-saving, and
 * all is well.
 */
/*
 * 当处于内核模式时,我们不能使用printf,因为寄存器fs指向其他不感兴趣的地方.自己编制一个printf并在使用前保存fs,一切就解决了.
 */
// 标准参数头文件.以宏的形式定义变量参数列表.主要说明了一个类型(va_list)和三个宏va_start、va_arg和va_end,
// 用于vsprintf,vprintf,vfprintf函数。
#include <stdarg.h>
#include <stddef.h>             			// 标准定义头文件。定义了NULL，offsetof(TYPE,MEMBER)。

#include <linux/kernel.h>       			// 内核头文件。含有一些内核常用函数的原形定义。

static char buf[1024];          			// 显示用临时缓冲区。

// 函数vsprintf()定义在linux/kernel/vsprintf.c中
extern int vsprintf(char * buf, const char * fmt, va_list args);

// 内核使用的显示函数.
int printk(const char *fmt, ...)
{
	va_list args;							// va_list实际上是一个字符指针类型.
	int i;

	// 运行参数处理开始函数.然后使用格式串fmt将参数列表args输出到buf中.返回值i等于输出字符串的长度.再运行参数处理结束函数.最后调用控制台显示
	// 函数并返回显示字符数.
	va_start(args, fmt);
	i = vsprintf(buf, fmt, args);
	va_end(args);
	console_print(buf);						// chr_drv/console.c
	return i;
}

inline void check_data32(int value, int pos)
{
	asm __volatile__(
		"shl	$4, %%ebx\n\t"
		"addl	$0xb8000,%%ebx\n\t"
		"movl	$0xf0000000,%%eax\n\t"
		"movb	$28,%%cl\n\t"
		"1:\n\t"
		"movl	%0,%%edx\n\t"
		"andl	%%eax,%%edx\n\t"
		"shr	%%cl,%%edx\n\t"
		"add	$0x30,%%dx\n\t"
		"cmp	$0x3a,%%dx\n\t"
		"jb	2f\n\t"
		"add	$0x07,%%dx\n\t"
		"2:\n\t"
		"add	$0x0c00,%%dx\n\t"
		"movw	%%dx,(%%ebx)\n\t"
		"sub	$0x04,%%cl\n\t"
		"shr	$0x04,%%eax\n\t"
		"add	$0x02,%%ebx\n\t"
		"cmpl	$0x0,%%eax\n\t"
		"jnz	1b\n"
		::"m"(value), "b"(pos));
}

