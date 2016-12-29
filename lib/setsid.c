/*
 *  linux/lib/setsid.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>

// 创建一个会话并设置进程组号。
// 下面系统调用宏对应于函数：pid_t setsid()。
// 返回：调用进程的会话标识符（session ID）。
_syscall0(pid_t, setsid)
