/*
 *  linux/fs/fcntl.c
 *
 *  (C) 1991  Linus Torvalds
 */

//#include <string.h>
#include <errno.h>
#include <linux/sched.h>
//#include <linux/kernel.h>
//#include <asm/segment.h>

#include <fcntl.h>
//#include <sys/stat.h>

extern int sys_close(int fd);

// 复制文件句柄(文件描述符).
// 参数fd是欲复制的文件句柄,arg指定新文件句柄的最小数值.
// 返回新文件句柄或出错码.
static int dupfd(unsigned int fd, unsigned int arg)
{
	// 首先检查函数参数的有效性.如果文件句柄值大于一个程序最多打开文件数NR_OPEN,或者该句柄的文件结构不存在,则返回出错码并退出.如果指定的新
	// 句柄值arg大于最多打开文件数,也返回出错码并退出.注意,实际上文件句柄就是进程文件结构指针数组项索引号.
	if (fd >= NR_OPEN || !current->filp[fd])
		return -EBADF;
	if (arg >= NR_OPEN)
		return -EINVAL;
	// 然后在当前进程的文件结构指针数组中寻找索引号等于或大于arg,但还没有使用的项.若找到的新句柄值arg大于最多打开文件数(即没有空闲项),则返回
	// 出错码并退出.
	while (arg < NR_OPEN)
		if (current->filp[arg])
			arg++;
		else
			break;
	if (arg >= NR_OPEN)
		return -EMFILE;
	// 否则针对找到的空闲项(句柄),在执行时关闭标志位图close_on_exec中复位该句柄位.即在运行exec()类函数时,不会关闭用dup()创建的的句柄.并令该
	// 文件结构指针等于原句柄fd的指针,并且将文件引用数增1.最后返回新的文件句柄arg.
	current->close_on_exec &= ~(1 << arg);
	(current->filp[arg] = current->filp[fd])->f_count++;
	return arg;
}

// 复制文件句柄系统调用。
// 复制指定文件句柄oldfd，新文件句柄值等于newfd。如果newfd已打开，则首先关闭之。
// 参数：oldfd -- 原文件句柄；newfd - 新文件句柄。
// 返回新文件句柄值。
int sys_dup2(unsigned int oldfd, unsigned int newfd)
{
	sys_close(newfd);               						// 若句柄newfd已经打开，则首先关闭之。
	return dupfd(oldfd, newfd);      						// 复制并返回新句柄。
}

// 复制文件句柄系统调用.
// 复制指定文件句柄oldfd,新句柄的值是当前最小的未用句柄值.
// 参数:fildes -- 被复制的文件句柄.
// 返回新文件句柄值.
int sys_dup(unsigned int fildes)
{
	return dupfd(fildes, 0);
}

// 文件控制系统调用函数。
// 参数fd是文件句柄；cmd是控制命令（参见include/fcntl.h）；arg则针对不同的命令有不同的含义。对于复制句柄命令F_DUFD
// arg是新文件句可取的最小值；对于设置文件操作和访问标志命令F_SETFL，arg是新的文件操作和访问模式。对于文件上锁命令
// F_GETLK、F_SETLK和F_SETLKW，arg是指向flock结构的指针。但本内核中没有实现文件上锁功能。
// 返回：若出错，则所有操作都返回-1.若成功，那么F_DUPFD返回新文件句柄；F_GETFD返回文件句柄的当前执行时关闭标志
// close_on_exec；F_GETFL返回文件操作和访问标志。
int sys_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct file * filp;

	// 首先检查给出的文件句柄有效性。然后根据不同命令cmd进行分别处理。如果文件句柄值大于一个进程最多打开文件数NR_OPEN，或者
	// 该句柄的文件结构指针为空，则返回出错码并退出。
	if (fd >= NR_OPEN || !(filp = current->filp[fd]))
		return -EBADF;
	switch (cmd) {
		case F_DUPFD:   										// 复制文件句柄。
			return dupfd(fd,arg);
		case F_GETFD:   										// 取文件句柄的执行时关闭标志。
			return (current->close_on_exec >> fd) & 1;
		case F_SETFD:   										// 设置执行时关闭标志。arg位0置位是设置，否则关闭。
			if (arg & 1)
				current->close_on_exec |= (1 << fd);
			else
				current->close_on_exec &= ~(1 << fd);
			return 0;
		case F_GETFL:   										// 取文件状态标志和访问模式。
			return filp->f_flags;
		case F_SETFL:   										// 设置文件状态和访问模式（根据arg设置添加、非阻塞标志）。
			filp->f_flags &= ~(O_APPEND | O_NONBLOCK);
			filp->f_flags |= arg & (O_APPEND | O_NONBLOCK);
			return 0;
		case F_GETLK:	case F_SETLK:	case F_SETLKW:  		// 未实现。
			return -1;
		default:
			return -1;
	}
}
