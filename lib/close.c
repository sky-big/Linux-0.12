/*
 *  linux/lib/close.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>	                                // Linux标准头文件.定义了各种符号常数和类型,并声明了各种函数.

// 关闭文件函数
// 下面该调用宏函数对应:int close(int fd).直接调用了系统中断int 0x80,参数是__NR_close.其中fd是文件描述符.
_syscall1(int, close, int, fd)
