/*
 *  linux/kernel/sys.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>

#include <linux/sched.h>
//#include <linux/tty.h>
#include <linux/kernel.h>
#include <linux/config.h>
#include <asm/segment.h>
#include <sys/times.h>
#include <sys/utsname.h>
//#include <sys/param.h>
#include <sys/resource.h>
#include <string.h>

/*
 * The timezone where the local system is located.  Used as a default by some
 * programs who obtain this value by using gettimeofday.
 */
/*
 * 本系统所在的时区（timezone）。作为某些程序使用gettimeofday系统调用获取时区的默认值。
 */
// 时区结构timezone第1个字段（tz_minuteswest）表示距格林尼治标准时间GMT以西的分钟数；第2个字段（tz_dsttime）
// 是夏令时DST（Daylight Savings Time）调整类型。该结构定义在include/sys/time.h中。
struct timezone sys_tz = { 0, 0};

extern int session_of_pgrp(int pgrp);

// 返回日期和时间（ftime - Fetch time）。
// 以下返回值是-ENOSYS的系统调用函数均表示在本版本内核中还未实现。
int sys_ftime()
{
	return -ENOSYS;
}

int sys_break()
{
	return -ENOSYS;
}

// 用于当前进程对子进程进行高度（debugging）。
int sys_ptrace()
{
	return -ENOSYS;
}

// 改变并打印终端行设置。
int sys_stty()
{
	return -ENOSYS;
}

// 取终端行设置信息。
int sys_gtty()
{
	return -ENOSYS;
}

// 修改文件名。
int sys_rename()
{
	return -ENOSYS;
}

int sys_prof()
{
	return -ENOSYS;
}

/*
 * This is done BSD-style, with no consideration of the saved gid, except
 * that if you set the effective gid, it sets the saved gid too.  This
 * makes it possible for a setgid program to completely drop its privileges,
 * which is often a useful assertion to make when you are doing a security
 * audit over a program.
 *
 * The general idea is that a program which uses just setregid() will be
 * 100% compatible with BSD.  A program which uses just setgid() will be
 * 100% compatible with POSIX w/ Saved ID's.
 */
/*
 * 以下是BSD形式的实现，没有考虑保存的gid（saved git或sgid），除了当你设置了有效的gid（effective gid或
 * egid）时，保存的gid也会被设置。这使得一个使用setid的程序可以完全放弃其特权。当你在对一个程序进行安全审计
 * 时，这通常是一种很好的处理方法。
 *
 * 最基本的考虑是一个使用setregid()的程序将会与BSD系统100%的兼容。而一个使用setgid()和保存的gid的程序将
 * 会与POSIX100%的兼容。
 */
// 设置当前任务的实际以及/或者有效组ID（gid）。如果任务没有超级用户特权，那么只能互换其实际组ID和有效组ID。
// 如果任务具有超级用户特权，就能任意设置有效的和实际的组ID。保留的gid（saved gid）被设置成与有效gid。实际
// 组ID是指进程当前的gid。
int sys_setregid(int rgid, int egid)
{
	if (rgid > 0) {
		if ((current->gid == rgid) ||
		    suser())
			current->gid = rgid;
		else
			return(-EPERM);
	}
	if (egid > 0) {
		if ((current->gid == egid) ||
		    (current->egid == egid) ||
		    suser()) {
			current->egid = egid;
			current->sgid = egid;
		} else
			return(-EPERM);
	}
	return 0;
}

/*
 * setgid() is implemeneted like SysV w/ SAVED_IDS
 */
/*
 * setgid()的实现与具有SAVED_IDS的SYSV的实现方法相似。
 */
// 设置进程组号（gid）。如果任务没有超级用户特权，它可以使用setgid()将其有效gid（effective gid）设置为成其保留
// gid（saved git）或其实际git（real gid）。如果任务有超级用户特权，则实际gid、有效gid和保留gid都被设置成参数
// 指定的gid。
int sys_setgid(int gid)
{
	if (suser())
		current->gid = current->egid = current->sgid = gid;
	else if ((gid == current->gid) || (gid == current->sgid))
		current->egid = gid;
	else
		return -EPERM;
	return 0;
}

// 打开或关闭进程计账功能。
int sys_acct()
{
	return -ENOSYS;
}

// 映射任意物理内在到进程的虚拟地址空间。
int sys_phys()
{
	return -ENOSYS;
}

int sys_lock()
{
	return -ENOSYS;
}

int sys_mpx()
{
	return -ENOSYS;
}

int sys_ulimit()
{
	return -ENOSYS;
}

// 返回从1970年1月1日00:00:00 GMT开始计时的时间值（秒）。如果tloc不为null，则时间值也存储在那里。
// 由于参数是一个指针，而其所指位置在用户空间，因此需要使用函数put_fs_long()来访问该值。在进入内核中运行时，段
// 寄存器fs默认地指向当前用户数据空间。因此该函数就可利用fs来访问用户空间中的值。
int sys_time(long * tloc)
{
	int i;

	i = CURRENT_TIME;
	if (tloc) {
		verify_area(tloc,4);            				// 验证内存容量是否够（这里是4字节）。
		put_fs_long(i,(unsigned long *)tloc);   		// 放入用户数据段tloc处。
	}
	return i;
}

/*
 * Unprivileged users may change the real user id to the effective uid
 * or vice versa.  (BSD-style)
 *
 * When you set the effective uid, it sets the saved uid too.  This
 * makes it possible for a setuid program to completely drop its privileges,
 * which is often a useful assertion to make when you are doing a security
 * audit over a program.
 *
 * The general idea is that a program which uses just setreuid() will be
 * 100% compatible with BSD.  A program which uses just setuid() will be
 * 100% compatible with POSIX w/ Saved ID's.
 */
/*
 * 无特权的用户可以见实际的uid（realuid）改成有效的uid（effective uid），反之亦然。（BSD 形式的实现）
 *
 * 当你设置有效的uid时，同时也设置了保存的uid。这使得一个使用setuid的程序可以完全放弃其特权。当你在对一个程序进行
 * 安全审计时，这通常是一种很好的处理方法。最基本的考虑是一个使用setreuid()的程序将会与BSD系统100%兼容。而一个使
 * 用setuid()和保存的gid的程序将会与POSIX 100%兼容。
 */
// 设置任务的实际以及/或者有效的用户ID（uid）。如果任务没有超级用户特权，那么只能互换其实际的uid和有效的uid。如果
// 任务具有超级用户特权，就能任意设置有效的和实际的用户ID。保存的uid（saved uid）被设置成与有效uid同值。
int sys_setreuid(int ruid, int euid)
{
	int old_ruid = current->uid;

	if (ruid > 0) {
		if ((current->euid == ruid) ||
                    (old_ruid == ruid) ||
		    suser())
			current->uid = ruid;
		else
			return(-EPERM);
	}
	if (euid > 0) {
		if ((old_ruid == euid) ||
                    (current->euid == euid) ||
		    suser()) {
			current->euid = euid;
			current->suid = euid;
		} else {
			current->uid = old_ruid;
			return(-EPERM);
		}
	}
	return 0;
}

/*
 * setuid() is implemeneted like SysV w/ SAVED_IDS
 *
 * Note that SAVED_ID's is deficient in that a setuid root program
 * like sendmail, for example, cannot set its uid to be a normal
 * user and then switch back, because if you're root, setuid() sets
 * the saved uid too.  If you don't like this, blame the bright people
 * in the POSIX commmittee and/or USG.  Note that the BSD-style setreuid()
 * will allow a root program to temporarily drop privileges and be able to
 * regain them by swapping the real and effective uid.
 */
/*
 * setuid()的实现与具有SAVED_IDS的SYSV的实现方法相似。
 *
 * 请注意使用SAVED_ID的setuid()在某些方面是不完善的。例如，一个使用setuid的超级用户程序sendmail就做不到把其uid
 * 设置成一个普通用户的uid，然后再交换回来。因为如果你是一个超级用户，setuid()也同时会设置保存的uid。如果你不喜欢
 * 这样的做法的话，就责怪POSIX组委会以及/或者USG中的聪明人吧。不过请注意BSD形式的setreuid()实现能够允许一个超级
 * 用户程序临时放弃特权，并且能通过交换实际的和有效的uid而再次获得特权。
 */
// 设置任务用户ID（uid）。如果任务没有超级用户特权，它可以使用setuid()将其有效的uid（effective uid）设置成其保存
// 的uid（saved uid）或其实际的uid（real uid）。如果用户有超级用户特权，则实际的uid、有效的uid和保存的uid都会被
// 设置成参数指定的uid。
int sys_setuid(int uid)
{
	if (suser())
		current->uid = current->euid = current->suid = uid;
	else if ((uid == current->uid) || (uid == current->suid))
		current->euid = uid;
	else
		return -EPERM;
	return(0);
}

// 设置系统开机时间。参数tptr是从1970年1月1日00：00：00 GMT开始计时的时间值（秒）。
// 调用进程必须具有超级用户权限。其中HZ=100,是内核系统运行频率。
// 由于参数是一个指针，而其所指位置在用户空间，因此需要使用函数get_fs_long()来访问该值。在进入内核中运行时，段
// 寄存器fs被默认地指向当前用户数据空间。因此该函数就可利用fs来访问用户空间中的值。函数参数提供的当前时间值减去
// 系统已经运行的时间秒值（jiffies/HZ）即是开机时间秒值。
int sys_stime(long * tptr)
{
	if (!suser())
		return -EPERM;          					// 如果不是超级用户则出错返回（许可）。
	startup_time = get_fs_long((unsigned long *)tptr) - jiffies / HZ;
	jiffies_offset = 0;
	return 0;
}

// 获取当前任务运行时间统计值。
// 在tbuf所指用户数据空间处返回tms结构的任务运行时间统计值。tms结构中包括进程用户运行时间、内核（系统）时间、子进
// 程用户运行时间、子进程系统运行时间。函数返回值是系统运行到当前的嘀嗒数。
int sys_times(struct tms * tbuf)
{
	if (tbuf) {
		verify_area(tbuf,sizeof *tbuf);
		put_fs_long(current->utime,(unsigned long *)&tbuf->tms_utime);
		put_fs_long(current->stime,(unsigned long *)&tbuf->tms_stime);
		put_fs_long(current->cutime,(unsigned long *)&tbuf->tms_cutime);
		put_fs_long(current->cstime,(unsigned long *)&tbuf->tms_cstime);
	}
	return jiffies;
}

// 当参数end_data_seg数值合理，并且系统确实有足够内存，而且进程没有超越其最大数据段大小时，该函数设置数据段末尾为
// end_data_seg指定的值。该值必须大于代码结尾并且要小于堆栈结尾16KB。返回值是数据段的新结尾值（如果返回值与要求不
// 同，则表明有错误发生)。该函数并不被用户直接调用，而由libc库函数进行包装，并且返回值也不一样。
int sys_brk(unsigned long end_data_seg)
{
	// 如果参数值大于代码结尾，并且小于（堆栈 - 16KB），则设置新数据段结尾值。
	if (end_data_seg >= current->end_code &&
	    end_data_seg < current->start_stack - 16384)
		current->brk = end_data_seg;
	return current->brk;            			// 返回进程当前的数据段结尾值。
}

/*
 * This needs some heave checking ...
 * I just haven't get the stomach for it. I also don't fully
 * understand sessions/pgrp etc. Let somebody who does explain it.
 *
 * OK, I think I have the protection semantics right.... this is really
 * only important on a multi-user system anyway, to make sure one user
 * can't send a signal to a process owned by another.  -TYT, 12/12/91
 */
/*
 * 下面代码需要某些严格的检查...
 * 我只是没有胃口来做这些。我也不完全明白sessions/pgrp等的含义。还是让了解它们的人来做吗。
 *
 * OK，我想我已经正确地实现了保护语义...。总之，这其实只对多用户系统是重要的，以确定一个用户不能向其他用户的进程发送
 * 信号。-TYT 12/12/91
 */
// 设置指定进程pid的进程组号为pgid。
// 参数pid是指定进程的进程号。如果它为0,则让pid等于当前进程的进程号。参数pgid是指定的进程组号。如果它为0,则让它等于
// 进程组号。如果该函数用于将进程从一个进程组移到另一个进程组，则这两个进程组必须属于同一个会话（session）。在这种情
// 况下，参数pgid指定了要加入的现在进程组ID，此时该组的会话ID必须与将要加入进程的相同。
int sys_setpgid(int pid, int pgid)
{
	int i;

	// 如果参数pid为0,则pid取值为当前进程的进程号pid。如果参数pgid为0,则pgid也取值为当前进程的pid。[??这里与POSIX标
	// 准的描述有出入]。若pgid小于0,则返回无效错误码。
	if (!pid)
		pid = current->pid;
	if (!pgid)
		pgid = current->pid;
	if (pgid < 0)
		return -EINVAL;
	// 扫描任务数组，查找指定进程号pid的任务。如果找到了进程号是pid的进程，并且该进程的父进程就是当前进程或者该进程就是当
	// 前进程，那么若该任务已经是会话首领，则出错返回。若该任务的会话号（session）与当前进程的不同，或者指定的进程组号pgid
	// 与pid不同并且pgid进程组所属会话号与当前进程所属会话号不同，则也出错返回。否则把查找到的进程的pgrp设置为pgid，并
	// 返回0。若没有找到指定pid的进程，则返回进程不存在出错码。
	for (i = 0 ; i < NR_TASKS ; i++)
		if (task[i] && (task[i]->pid == pid) && ((task[i]->p_pptr == current) || (task[i] == current))) {
			if (task[i]->leader)
				return -EPERM;
			if ((task[i]->session != current->session) ||
			    ((pgid != pid) &&
			     (session_of_pgrp(pgid) != current->session)))
				return -EPERM;
			task[i]->pgrp = pgid;
			return 0;
		}
	return -ESRCH;
}

// 返回当前进程的进程组号。与getpgid(0)等同。
int sys_getpgrp(void)
{
	return current->pgrp;
}

// 创建一个会话（session）（即设置其leader = 1），并且设置其会话号=其组号=其进程号。
// 如果当前进程已是会话首领并且不是超级用户，则出错返回。否则设置当前进程为新会话首领（leader = 1），并且设置当前进程会话
// 号session和组号pgrp都等于进程号pid，而且设置当前进程没有控制终端。最后系统调用返回会话号。
int sys_setsid(void)
{
	if (current->leader && !suser())
		return -EPERM;
	current->leader = 1;
	current->session = current->pgrp = current->pid;
	current->tty = -1;      				// 表示当前进程没有控制终端。
	return current->pgrp;
}

/*
 * Supplementary group ID's
 */
/*
 * 进程的其他用户组号。
 */
// 取当前进程其他辅助用户组号。
// 任务数据结构中groups[]数组保存着进程同时所属的多个用户组号。该数组共NGROUPS个项，若某项值是NOGROUP（即为-1），则
// 表示从该项开始以后所有项都空闲。否则数组项中保存的是用户组号。
// 参数gidsetsize是获取的用户组号个数；grouplist是存储这些用户组号的用户空间缓存。
int sys_getgroups(int gidsetsize, gid_t *grouplist)
{
	int	i;

	// 首先验证grouplist指针所指的用户缓存空间是否足够，然后从当前进程结构的groups[]数组中逐个取得用户组号并复制到用户缓存
	// 中。在复制过程中，如果groups[]中的项数大于给定的参数gitsetsize所指定的个数，则表示用户给出的缓存太小，不能容下当前
	// 进程所有组号，因此此次取组号操作会出错返回。若复制过程正常，则函数最后会返回复制的用户组号个数。（gidsetsize - gid
	// set size，用户组号集大小）。
	if (gidsetsize)
		verify_area(grouplist, sizeof(gid_t) * gidsetsize);

	for (i = 0; (i < NGROUPS) && (current->groups[i] != NOGROUP); i++, grouplist++) {
		if (gidsetsize) {
			if (i >= gidsetsize)
				return -EINVAL;
			put_fs_word(current->groups[i], (short *) grouplist);
		}
	}
	return(i);              				// 返回实际含有的用户组号个数。
}

// 设置当前进程同时所属的其他辅助用户组号。
// 参数gidsetsize是将设置的用户组号个数；grouplist是含有用户组号的用户空间缓存。
int sys_setgroups(int gidsetsize, gid_t *grouplist)
{
	int	i;

	// 首先查权限和参数的有效性。只有超级用户可以修改或设置当前进程的辅助用户组号，而且设置的项数不能超过进程的groups[NGROUPS]
	// 数组的容量。然后从用户缓冲中逐个复制用户组号，共gidsetsize个。如果复制的个数没有填满group[]，则在随后一项上填上值为-1
	// 的项（NOGROUP）。最后函数返回0。
	if (!suser())
		return -EPERM;
	if (gidsetsize > NGROUPS)
		return -EINVAL;
	for (i = 0; i < gidsetsize; i++, grouplist++) {
		current->groups[i] = get_fs_word((unsigned short *) grouplist);
	}
	if (i < NGROUPS)
		current->groups[i] = NOGROUP;
	return 0;
}

// 检查当前进程是否在指定的用户组grp中.是则返回1,否则返回0.
int in_group_p(gid_t grp)
{
	int	i;

	// 如果当前进程的有效组号就是grp,则表示进程属于grp进程组.函数返回1.否则就在进程的辅助用户组数组中扫描是否有grp进程组号.若
	// 有则函数也返回1.若扫描到值为NOGROUP的项,表示已扫描完全部有效项没有发现匹配的组号,因此函数返回0.
	if (grp == current->egid)
		return 1;

	for (i = 0; i < NGROUPS; i++) {
		if (current->groups[i] == NOGROUP)
			break;
		if (current->groups[i] == grp)
			return 1;
	}
	return 0;
}

// utsname结构含有一些字符串字段。用于保存系统的名称。其中包含5个字段，分别是：当前操作系统的名称、网络节点名称（主机名）、
// 当前操作系统发行级别、操作系统版本号以及系统运行的硬件类型名称。该结构定义在include/sys/utsname.h文件中。这里内核使用
// include/linux/config.h文件中的常数符号设置了它们的默认值。它们分别为“Linux”、“(none)”、“0”、“0.12”、“i386”。
static struct utsname thisname = {
	UTS_SYSNAME, UTS_NODENAME, UTS_RELEASE, UTS_VERSION, UTS_MACHINE
};

// 获取系统名称等信息。
int sys_uname(struct utsname * name)
{
	int i;

	if (!name) return -ERROR;
	verify_area(name,sizeof *name);
	for(i = 0; i < sizeof *name; i++)
		put_fs_byte(((char *) &thisname)[i], i + (char *) name);
	return 0;
}

/*
 * Only sethostname; gethostname can be implemented by calling uname()
 */
/*
 * 通过调用uname()只能实现sethostname和gethostname。
 */
// 设置系统主机名（系统的网络节点名）。
// 参数name指针指向用户数据区中含有主机名字符串的缓冲区；len是主机名字符串长度。
int sys_sethostname(char *name, int len)
{
	int	i;

	// 系统主机名只能由超级用户设置或修改，并且主机名长度不能超过最大长度MAXHOSTNAMELEN。
	if (!suser())
		return -EPERM;
	if (len > MAXHOSTNAMELEN)
		return -EINVAL;
	for (i = 0; i < len; i++) {
		if ((thisname.nodename[i] = get_fs_byte(name + i)) == 0)
			break;
	}
	// 在复制完毕后，如果用户提供的字符串没有包含NULL字符，那么若复制的主机名长度还没有超过MAXHOSTNAMELEN，则在主机名字
	// 符串后添加一个NULL。若已经填满MAXHOSTNAMELEN个字符，则把最后一个字符改成NULL字符。即thisname.nodename[min(
	// i,MAXHOSTNAMELEN)] = 0。
	if (thisname.nodename[i]) {
		thisname.nodename[i > MAXHOSTNAMELEN ? MAXHOSTNAMELEN : i] = 0;
	}
	return 0;
}

// 取当前进程指定资源的界限值。
// 进程的任务结构中定义有一个数组rlim[RLIM_NLIMITS]，用于控制进程使用系统资源的界限。数组每个项是一个rlimit结构，
// 其中包含两个字段。一个说明进程对指定资源的当前限制界限（soft limit，即软限制），另一个说明系统对指定资源的最大限制
// 界限（hard limit，即硬限制）。rlim[]数组的每一项对应系统对当前进程一种资源的界限信息。Linux 0.12系统共对6种资源
// 规定了界限，即RLIM_NLIMITS=6。请参考头文件include/sys/resource.h说明。
// 参数resource指定我们咨询的资源名称，实际上它是任务结构中rlim[]数组的索引项值。
// 参数rlim是指向rlimit结构的用户缓冲区指针，用于存放取得的资源界限信息。
int sys_getrlimit(int resource, struct rlimit *rlim)
{
	// 所咨询的资源resource实际上是进程任务结构中rlim[]数组的索引项值。该索引值当然不能大于数组的最大项数RLIM_NLIMITS。
	// 在验证过rlim指针所指用户缓冲足够以后，这里就把参数指定的资源resource结构信息复制到用户缓冲区中，并返回0。
	if (resource >= RLIM_NLIMITS)
		return -EINVAL;
	verify_area(rlim, sizeof *rlim);
	put_fs_long(current->rlim[resource].rlim_cur,           // 当前（软）限制值。
		    (unsigned long *) rlim);
	put_fs_long(current->rlim[resource].rlim_max,           // 系统（硬）限制值。
		    ((unsigned long *) rlim) + 1);
	return 0;
}

// 设置当前进程指定资源的界限值。
// 参数resource指定我们设置界限的资源名称，实际上它是任务结构中rlim[]数组的索引项值。
// 参数rlim是指向rlimit结构的用户缓冲区指针，用于内核读取新的资源界限信息。
int sys_setrlimit(int resource, struct rlimit *rlim)
{
	struct rlimit new, *old;

	// 首先判断参数resource（任务结构rlim[]项索引值）有效性。然后先让rlimit结构指针old指向进程任务结构中指定资源的当前
	// rlimit结构信息。接着把用户提供的资源界限信息复制到临时rlimit结构new中。此时如果判断出new结构中的软界限值或硬界限
	// 值大于进程该资源原硬界限值，并且当前不是超级用户的话，就返回许可出错。否则表示new中信息合理或者进程是超级用户进程，
	// 则修改原进程指定资源信息等于new结构中的信息，并成功返回0。
	if (resource >= RLIM_NLIMITS)
		return -EINVAL;
	old = current->rlim + resource;
	new.rlim_cur = get_fs_long((unsigned long *) rlim);
	new.rlim_max = get_fs_long(((unsigned long *) rlim) + 1);
	if (((new.rlim_cur > old->rlim_max) || (new.rlim_max > old->rlim_max)) && !suser())
		return -EPERM;
	*old = new;
	return 0;
}

/*
 * It would make sense to put struct rusuage in the task_struct,
 * except that would make the task_struct be *really big*.  After
 * task_struct gets moved into malloc'ed memory, it would
 * make sense to do this.  It will make moving the rest of the information
 * a lot simpler!  (Which we're not doing right now because we're not
 * measuring them yet).
 */
/*
 * 把rusuage结构放进任务结构task_struct中是恰当的，除非它会使任务结构长度变得非常大。在把任务结构移入内核malloc分配的
 * 内存中之后，这样做即使任务结构很大也没问题了。这将使得其他信息的移动变得非常方便！（我们还没有这样做，因为我们还没有测试
 * 过它们的大小）。
 *
 */
// 获取指定进程的资源利用信息。
// 本系统调用提供当前进程或其已终止的和等待着的子进程资源使用情况。如果参数who等于RUSAGE_SELF，则返回当前进程的资源利用
// 信息。如果指定进程who是RUSAGE_CHILDREN，则返回当前进程的已终止和等待着的子进程资源利用信息。符号常数RUSAGE_SELF和
// RUSAGE_CHILDREN以及rusage结构都定义在include/sys/resource.h文件中。
int sys_getrusage(int who, struct rusage *ru)
{
	struct rusage r;
	unsigned long	*lp, *lpend, *dest;

	// 首先判断参数指定进程的有效性。如果who即不是RUSAGE_SELF（指定当前进程），也不是RUSAGE_CHILDREN（指定子进程），则以
	// 无效参数码返回。否则在验证了指针ru指定的用户缓冲区域后，把临时rusage结构区域r清零。
	if (who != RUSAGE_SELF && who != RUSAGE_CHILDREN)
		return -EINVAL;
	verify_area(ru, sizeof *ru);
	memset((char *) &r, 0, sizeof(r));
	// 若参数who是RUSAGE_SELF，则复制当前进程资源利用信息到r结构中。若指定进程who是RUSAGE_CHILDREN，则复制当前进程的已终止
	// 和等待着的子进程资源利用信息到临时rusage结构r中。宏CT_TO_SECS和CT_TO_USECS用于把系统当前嘀嗒数转换成用秒值加微秒值
	// 表示。它们定义在include/linux/sched.h文件中。jiffies_offset是系统嘀嗒数误差调整数。
	if (who == RUSAGE_SELF) {
		r.ru_utime.tv_sec = CT_TO_SECS(current->utime);
		r.ru_utime.tv_usec = CT_TO_USECS(current->utime);
		r.ru_stime.tv_sec = CT_TO_SECS(current->stime);
		r.ru_stime.tv_usec = CT_TO_USECS(current->stime);
	} else {
		r.ru_utime.tv_sec = CT_TO_SECS(current->cutime);
		r.ru_utime.tv_usec = CT_TO_USECS(current->cutime);
		r.ru_stime.tv_sec = CT_TO_SECS(current->cstime);
		r.ru_stime.tv_usec = CT_TO_USECS(current->cstime);
	}
	// 然后让lp指针指向r结构，lpend指向r结构末尾处，而dest指针指向用户空间中的ru结构。最后把r中信息复制到用户空间ru结构中，并
	// 返回0。
	lp = (unsigned long *) &r;
	lpend = (unsigned long *) (&r + 1);
	dest = (unsigned long *) ru;
	for (; lp < lpend; lp++, dest++)
		put_fs_long(*lp, dest);
	return(0);
}

// 取得系统当前时间，并用指定格式返回。
// timeval结构和timezone结构都定义在include/sys/time.h文件中。timeval结构含有秒和微秒（tv_sec和tv_usec）两个
// 字段。timezone结构含有本地距格林尼治标准时间以西的分钟数（tz_minuteswest）和夏令时间调整类型（tz_dsttime）两上
// 字段。（dst -- Daylight Savings Time）
int sys_gettimeofday(struct timeval *tv, struct timezone *tz)
{
	// 如果参数给定的timeval结构指针不空，则在该结构中返回当前时间（秒值和微秒值）;
	// 如果参数给定的用户数据空间中timezone结构的指针不空，则也返回该结构的信息。
	// 程序中startup_time是系统开机时间（秒值）。宏CT_TO_SECS和CT_TO_USECS用于把系统当前嘀嗒数转换成用秒值加微秒值表示。
	// 它们定义在include/linux/sched.h文件中。jiffies_offset是系统嘀嗒数误差调整数。
	if (tv) {
		verify_area(tv, sizeof *tv);
		put_fs_long(startup_time + CT_TO_SECS(jiffies + jiffies_offset),
			    (unsigned long *) tv);
		put_fs_long(CT_TO_USECS(jiffies + jiffies_offset),
			    ((unsigned long *) tv) + 1);
	}
	if (tz) {
		verify_area(tz, sizeof *tz);
		put_fs_long(sys_tz.tz_minuteswest, (unsigned long *) tz);
		put_fs_long(sys_tz.tz_dsttime, ((unsigned long *) tz) + 1);
	}
	return 0;
}

/*
 * The first time we set the timezone, we will warp the clock so that
 * it is ticking GMT time instead of local time.  Presumably,
 * if someone is setting the timezone then we are running in an
 * environment where the programs understand about timezones.
 * This should be done at boot time in the /etc/rc script, as
 * soon as possible, so that the clock can be set right.  Otherwise,
 * various programs will get confused when the clock gets warped.
 */
/*
 * 在第1次设置时区（timezone）时，我们会改变时钟值以让系统使用格林尼治标准时间（GMT）运行，而非使用本地时间。推测起来
 * 说，如果某人设置了时区时间，那么我们就运行在程序知晓时区时间的环境中。设置时区操作应该在系统启动阶段，尽快地在/etc/rc
 * 脚本程序中进行。这样时钟就可以设置正确。否则的话，若我们以后才设置时区而导致时钟时间改变，可能会让一些程序的运行出现
 * 问题。
 */
// 设置系统当前时间。
// 参数tv是指向用户数据区中timeval结构信息的指针。参数tz是用户数据区中timezone结构的指针。该操作需要超级用户权限。如果
// 两者皆为空，则什么也不做，函数返回0。
int sys_settimeofday(struct timeval *tv, struct timezone *tz)
{
	static int	firsttime = 1;
	void 		adjust_clock();

	// 设置系统当前时间需要超级用户权限。如果tz指针不空，则设置系统时区信息。即复制用户timezone结构信息到系统中的sys_tz结构
	// 中。如果是第1次调用本系统调用并且参数tv指针不空，则调整系统时钟值。
	if (!suser())
		return -EPERM;
	if (tz) {
		sys_tz.tz_minuteswest = get_fs_long((unsigned long *) tz);
		sys_tz.tz_dsttime = get_fs_long(((unsigned long *) tz)+1);
		if (firsttime) {
			firsttime = 0;
			if (!tv)
				adjust_clock();
		}
	}
	// 如果参数的timeval结构指针tv不空，则用该结构信息设置系统时钟。首先从tv所指处获取以秒值（sec）加微秒值（usec）表示的系统
	// 时间，然后用秒值修改系统开机时间全局变量startup_time值，并用微秒值设置系统嘀嗒误差值jiffies_offset。
	if (tv) {
		int sec, usec;

		sec = get_fs_long((unsigned long *)tv);
		usec = get_fs_long(((unsigned long *)tv) + 1);

		startup_time = sec - jiffies / HZ;
		jiffies_offset = usec * HZ / 1000000 - jiffies % HZ;
	}
	return 0;
}

/*
 * Adjust the time obtained from the CMOS to be GMT time instead of
 * local time.
 *
 * This is ugly, but preferable to the alternatives.  Otherwise we
 * would either need to write a program to do it in /etc/rc (and risk
 * confusion if the program gets run more than once; it would also be
 * hard to make the program warp the clock precisely n hours)  or
 * compile in the timezone information into the kernel.  Bad, bad....
 *
 * XXX Currently does not adjust for daylight savings time.  May not
 * need to do anything, depending on how smart (dumb?) the BIOS
 * is.  Blast it all.... the best thing to do not depend on the CMOS
 * clock at all, but get the time via NTP or timed if you're on a
 * network....				- TYT, 1/1/92
 */
/*
 * 把从CMOS中读取的时间值调整为GMT时间值保存，而非本地时间值。
 *
 * 这里的做法很蹩脚，但要比其他方法好。否则我们就需要写一个程序并让它在/etc/rc中运行来做这件事（并且该程序会被多次执行而带来
 * 混乱。而且这样做也很难让程序把时钟精确地调整n小时候）或者把时区信息编译进内核中。当然这样做就非常、非常差劲了...
 *
 * 目前下面函数（XXX）的调整操作并没有考虑到夏令时问题。依据BIOS有多么智能（愚蠢？）也许根本就不用考虑这方面。当然，最好的做
 * 法是完全不依赖于CMOS时钟，而是让系统通过NTP（网络时钟协议）或者timed（时间服务器）获得时间，如果机器联上网的话...。
 */
// 把系统启动时间调整为以GMT为标准的时间。
// startup_time是秒值，因此这里需要把时区分钟值乘上60。
void adjust_clock()
{
	startup_time += sys_tz.tz_minuteswest * 60;
}

// 设置当前进程创建文件属性屏蔽码为mask & 0777。并返回原屏蔽码。
int sys_umask(int mask)
{
	int old = current->umask;

	current->umask = mask & 0777;
	return (old);
}

// 用于捕获未实现的System Call调用。
int sys_default(unsigned long arg1, unsigned long arg2, unsigned long arg3, unsigned long code){
    printk("System Call Number:%d\r\n",code);
    printk("Arg1:%X\r\n",arg1);
    printk("Arg2:%X\r\n",arg2);
    printk("Arg3:%X\r\n",arg3);
    for(;;);
}
