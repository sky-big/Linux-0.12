/*
 *  linux/fs/namei.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * Some corrections by tytso.
 */

#include <linux/sched.h>										// 调试程序头文件,定义了任务结构task_struct,0的数据等.
//#include <linux/kernel.h>
#include <asm/segment.h>										// 段操作头文件.定义了有关段寄存器操作的嵌入式汇编函数.

#include <string.h>
#include <fcntl.h>												// 文件控制头文件.文件及其描述符的操作控制常数符号的定义.
#include <errno.h>												// 错误号头文件.包含系统中各种出错号.
#include <const.h>												// 常数符号头文件.目前仅定义i节点中i_mode字段的各标志位.
#include <sys/stat.h>											// 文件状态头文件.含有文件或文件系统状态结构stat()和常量.

// 由文件名查找对应i节点的内部函数.
static struct m_inode * _namei(const char * filename, struct m_inode * base, int follow_links);

// 下面宏中右侧表达式是访问数组的一种特殊使用方法.它基于这样的一个事实,即用数组名和数组下标所表示的数组项(例如a[b])的值等同于使用数组首指针(地址)
// 加上该项偏移地址的形式的值*(a+b),同时可知项a[b]也可以表示成b[a]的形式.因此对于字符数组项形式为"LoveYou"[2](或者2["LoveYou"])就等同于*("LoveYou" + 2)
// 另外,字符串"LoveYou"在内存中被存储的位置就是其地址,因此数组项"LoveYou"[2]的值就是该字符串索引值为2的字符"v"所对应的ASCII码值0x76,或用八进制
// 表示就是0166.在C语言中,字符也可以用其ASCII码值来表示,方法是在字符的ASCII码值前面加一个反斜杠.例如字符"v"可以表示成"\x76"或者"\166".因此
// 对于不可显示的字符(例如ASCII码值为0x00--0x1f的控制字符)就可用其ASCII码值来表示.
//
// 下面是访问模式宏.x是头文件include/fcntl.h中行7行开始定义的文件访问(打开)标志.这个宏根据文件访问标志x的值来索引双引号中对应的数值.双引号中
// 有4个八进制数值(实际表示4个控制字符):"\004\002\006\377",分别表示读,写和执行的权限为:r,w,rw和wxrwxrwx,并且分别对应x的索引值0--3.例如,如果x为2,
// 则该宏返回八进制值006,表示可读可写(rw).另外,其中O_ACCMODE = 00003,是索引值x的屏蔽码.
#define ACC_MODE(x) ("\004\002\006\377"[(x) & O_ACCMODE])

/*
 * comment out this line if you want names > NAME_LEN chars to be
 * truncated. Else they will be disallowed.
 */
/*
 * 如果想让文件名长度 > NAME_LEN个的字符被截掉,就将下面定义注释掉.
 */
/* #define NO_TRUNCATE */

#define MAY_EXEC 			1	// 可执行(可进入).
#define MAY_WRITE 			2	// 可写.
#define MAY_READ 			4	// 可读.

/*
 *	permission()
 *
 * is used to check for read/write/execute permissions on a file.
 * I don't know if we should look at just the euid or both euid and
 * uid, but that should be easily changed.
 */
/*
 *	permission()
 *
 * 该函数用于检测一个文件的读/写/执行权限.我不知道是否只需检查euid,还是需要检查euid和uid两者,不过这很容易修改.
 */
// 检测文件访问许可权限.
// 参数:inode - 文件的i节点指针;mask - 访问属性屏蔽码.
// 返回:访问许可返回1,否则返回0.
static int permission(struct m_inode * inode, int mask)
{
	int mode = inode->i_mode;								// 文件访问属性.

	/* special case: not even root can read/write a deleted file */
	/* 特殊情况:即使是超级用户(root)也不能读/写一个已被删除的文件. */
	// 如果i节点有对应的设备,但该i节点的链接计数值等于0,表示该文件已被删除,则返回.
	if (inode->i_dev && !inode->i_nlinks)
		return 0;
	// 如果进程的有效用户id(euid)与i节点的用户id相同,则取文件宿主的访问权限
	else if (current->euid == inode->i_uid)
		mode >>= 6;
	// 如果进程有效组id(egid)与i节点的组id相同,则取组用户的访问权限
	else if (in_group_p(inode->i_gid))
		mode >>= 3;
	// 最后判断如果所取的的访问权限与屏蔽码相同,或者是超级用户,则返回1,否则返回0.
	if (((mode & mask & 0007) == mask) || suser())
		return 1;
	return 0;
}

/*
 * ok, we cannot use strncmp, as the name is not in our data space.
 * Thus we'll have to use match. No big problem. Match also makes
 * some sanity tests.
 *
 * NOTE! unlike strncmp, match returns 1 for success, 0 for failure.
 */
/*
 * ok,我们不能使用strncmp字符串比较函数,因为名称不在我们的数据空间(不在内核空间).因而我们只能使用match().问题不大,match()同样也处理一些
 * 完整的测试.
 *
 * 注意!与strncmp不同的是match()成功时返回1,失败时返回0.
 */
// 指定长度字符串比较函数.
// 参数:len - 比较的字符串长度;name - 文件名指针;de - 目录项结构.
// 返回:相同返回1,不同返回0.
static int match(int len, const char * name, struct dir_entry * de)
{
	register int same __asm__("ax");

	// 首先判断函数参数的有效性.如果目录项指针空,或者目录项i节点等于0,或者要比较的字符串长度超过文件名长度,则返回0(不匹配).
	if (!de || !de->inode || len > NAME_LEN)
		return 0;
	/* "" means "." ---> so paths like "/usr/lib//libc.a" work */
    /* ""当作"."来看待 ---> 这样就能处理象"/usr/lib//libc.a"那样的路径名 */
    // 如果比较的长度len等于0并且目录项中文件名的第1个字符是'.',并且只有这么一个字符,那么我们就认为是相同的,因此返回1(匹配)
	if (!len && (de->name[0] == '.') && (de->name[1] == '\0'))
		return 1;
	// 如果要比较的长度len小于NAME_LEN,但是目录项中文件名长度超过len,则也返回0(不匹配)
	// 第75行上对目录项中文件名长度是否超过len的判断方法是检测name[len]是否为NULL.若长度超过len,则name[len]处就是一个不是NULL的普通字符.而对于长度
	// 为len的字符串name,字符name[len]就应该是NULL.
	if (len < NAME_LEN && de->name[len])
		return 0;
	// 然后使用嵌入汇编语句进行快速比较操作.它会在用户数据空间(fs段)执行字符串的比较操作.%0 - eax(比较结果same);%1 - eax(eax初值0);%2 - esi(名字指针);
	// %3 - edi(目录项名指针);%4 - ecs(比较的字节长度值len).
	__asm__(\
		"cld\n\t"							// 清方向标志位.
		"fs ; repe ; cmpsb\n\t"				// 用户空间执行循环比较[esi++]和[edi++]操作.
		"setz %%al"							// 若比较结果一样(zf=0)则置al=1(same=eax).
		:"=a" (same)
		:"0" (0), "S" ((long) name), "D" ((long) de->name), "c" (len)
		:);
	return same;							// 返回比较结果.
}

/*
 *	find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the cache buffer in which the entry was found, and the entry
 * itself (as a parameter - res_dir). It does NOT read the inode of the
 * entry - you'll have to do that yourself if you want to.
 *
 * This also takes care of the few special cases due to '..'-traversal
 * over a pseudo-root and a mount point.
 */
/*
 *	find_entry()
 *
 * 在指定目录中寻找一个与名字匹配的目录项.返回一个含有找到目录项的高速缓冲块以及目录项本身(作为一个参数--res_dir).该函数并不读取目录
 * 项的i节点--如果需要的话则自己操作.
 *
 * 由于有'..'目录项,因此在操作期间也会对几种特殊情况分别处理--比如横越一个伪根目录以及安装点.
 */
// 查找指定目录和文件名的目录项.
// 参数:*dir - 指定目录i节点的指针;name - 文件名;namelen - 文件名长度;该函数在指定目录的数据(文件)中搜索指定文件名的目录项.并对指定
// 文件名是'..'的情况根据当前进行的相关设置进行特殊处理.
// 返回:成功则返回高速缓冲区指针,并在*res_dir处返回的目录项结构指针.失败则返回空指针NULL.
static struct buffer_head * find_entry(struct m_inode ** dir,
	const char * name, int namelen, struct dir_entry ** res_dir)
{
	int entries;
	int block,i;
	struct buffer_head * bh;
	struct dir_entry * de;
	struct super_block * sb;

	// 同样,本函数一开始也需要对函数参数的有效性进行判断和验证.如果我们在前面第30行定义了符号常数NO_TRUNCATE,那么如果文件名长度超过最大长度NAME_LEN,
	// 则不予处理.如果没有定义过NO_TRUNCATE,那么在文件名长度超过最大长度NAME_LEN时截短之.
#ifdef NO_TRUNCATE
	if (namelen > NAME_LEN)
		return NULL;
#else
	if (namelen > NAME_LEN)
		namelen = NAME_LEN;
#endif
	// 首先计算本目录中目录项项数entries.目录i节点i_size字段中含有本目录包含的数据长度,因此其除以一个目录项的长度(16字节)即可得到该目录中目录项数.然后
	// 置空返回目录项结构指针.
	entries = (*dir)->i_size / (sizeof (struct dir_entry));
	*res_dir = NULL;
	// 接下来我们对目录项文件名是'..'的情况进行特殊处理.如果当前进程指定的根i节点就是函数参数指定的目录,则说明对于本进程来说,这个目录就是它的伪根目录,即进程
	// 只能访问该目录中的项而不能退到其父目录中去.也即对于该进程本目录就如同是文件系统的根目录.因此我们需要将文件名修改为'.'.
	// 否则,如果该目录的i节点号等于ROOT_INO(1号)的话,说明确实是文件系统的根i节点.则取文件系统的超级块.如果被安装到的i节点存在,则先放回原i节点,然后对被
	// 安装到的i节点进行处理.于是我们让*dir指向该被安装到的i节点;并且该i节点的引用数加1.即针对这种情况,我们悄悄进行了"偷梁换柱"工程:)
	/* check for '..', as we might have to do some "magic" for it */
	/* 检查目录项'..',因为我们可能需要对其进行特殊处理 */
	if (namelen == 2 && get_fs_byte(name) == '.' && get_fs_byte(name + 1) == '.') {
		/* '..' in a pseudo-root results in a faked '.' (just change namelen) */
		/* 伪根中的'..'如同一个假'.'(只需改变名字长度) */
		if ((*dir) == current->root)
			namelen = 1;
		else if ((*dir)->i_num == ROOT_INO) {
			/* '..' over a mount-point results in 'dir' being exchanged for the mounted
			   directory-inode. NOTE! We set mounted, so that we can iput the new dir */
			/* 在一个安装点上的'..'将导致目录交换到被安装文件系统的目录i节点上.注意! 由于我们设置了mounted标志,因而我们能够放回该新目录 */
			sb = get_super((*dir)->i_dev);
			if (sb->s_imount) {
				iput(*dir);
				(*dir)=sb->s_imount;
				(*dir)->i_count++;
			}
		}
	}
	// 现在我们开始正常操作，查找指定文件名的目录项在什么地方。因此我们需要读取目录的数据，即取出目录i节点对应块设备数据区中的数据块（逻辑块）信息。这些逻辑块的
	// 块号保存在i节点结构的i_zone[9]数组中.我们先取其中第1个块号.如果目录i节点指向的第一个直接盘块号为0,则说明该目录竟然不含数据,这不正常.于是返回NULL退出.
	// 否则我们就从节点所在设备读取指定的目录项数据块.当然,如果不成功,则也返回NULL退出.
	if (!(block = (*dir)->i_zone[0]))
		return NULL;
	if (!(bh = bread((*dir)->i_dev, block)))
		return NULL;
	// 此时我们就在这个读取的目录i节点数据块中搜索匹配指定文件名的目录项.首先让de指向缓冲块中的数据块部分,并在不超过目录项数据的条件下,循环执行搜索.其中i是目录
	// 中的目录项索引号,在循环开始时初始化为0.
	i = 0;
	de = (struct dir_entry *) bh->b_data;
	while (i < entries) {
		// 如果当前目录项数据块已经搜索完,还没有找到匹配的目录项,则释放当前目录项数据块.再读入目录的下一个逻辑块.若这块为空,则只要还没有搜索完目录中的所有目录项,就
		// 跳过该块,继续读目录的下一逻辑块.若该块不空,就让de指向该数据块,然后在其中继续搜索.其中141行上i/DIR_ENTRIES_PER_BLOCK可得到当前搜索的目录项所在目录文件中的
		// 块号,而bmap()函数(inode.c)则可计算出在设备上对应的逻辑块号.
		if ((char *)de >= BLOCK_SIZE + bh->b_data) {
			brelse(bh);
			bh = NULL;
			if (!(block = bmap(*dir, i / DIR_ENTRIES_PER_BLOCK)) ||
			    !(bh = bread((*dir)->i_dev, block))) {
				i += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			de = (struct dir_entry *) bh->b_data;
		}
		// 如果找到匹配的目录项的话,则返回目录项结构指针de和该目录项i节点指针*dir以及该目录项数据块指针bh,并退出函数.否则继续在目录项数据块中比较下一个目录项.
		if (match(namelen, name, de)) {
			*res_dir = de;
			return bh;
		}
		de++;
		i++;
	}
	// 如果指定目录中的所有目录项都搜索赛后,还没有找到相应的目录项,则释放目录的数据块,最后返回NULL(失败).
	brelse(bh);
	return NULL;
}

/*
 *	add_entry()
 *
 * adds a file entry to the specified directory, using the same
 * semantics as find_entry(). It returns NULL if it failed.
 *
 * NOTE!! The inode part of 'de' is left at 0 - which means you
 * may not sleep between calling this and putting something into
 * the entry, as someone else might have used it while you slept.
 */
/*
 *      add_entry()
 * 使用与find_entry()同样的方法，往指定目录中添加一指定文件名的目录项。如果失败则返回NULL。
 *
 * 注意！！'de'（指定目录项结构指针）的i节点部分被设置为0 - 这表示在调用该函数和往目录项中添加信息之间不能去睡眠，
 * 因为如果睡眠，那么其他人（进程）可能会使用该目录项。
 */
// 根据指定的目录和文件名添加目录项。
// 参数：dir - 指定目录的i节点；name - 文件名；namelen - 文件名长度；
// 返回：高速缓冲区指针；res_dir - 返回的目录项结构指针。
static struct buffer_head * add_entry(struct m_inode * dir,
	const char * name, int namelen, struct dir_entry ** res_dir)
{
	int block, i;
	struct buffer_head * bh;
	struct dir_entry * de;

	// 同样，本函数一开始也需要对函数参数的有效性进行判断和验证。如果我们在前面定义了符号常数NO_TRUNCATE，那么如果文件
	// 名长度超过最大长度NAME_LEN，则不予处理。如果没有定义过NO_TRUNCATE，那么在文件长度超过最大长度NAME_LEN时截短之。
	*res_dir = NULL;                							// 用于返回目录项结构指针。
#ifdef NO_TRUNCATE
	if (namelen > NAME_LEN)
		return NULL;
#else
	if (namelen > NAME_LEN)
		namelen = NAME_LEN;
#endif
	// 现在我们开始操作，向指定目录中添加一个指定文件名的目录项。因此我们需要先读取目录的数据，即取出目录i节点对应块设备
	// 数据区中的数据块（逻辑块）信息。这些逻辑块的块号保存在i节点结构的i_zone[9]数组中。我们先取其第1个块号。如果目录
	// i节点指向的第一个直接磁盘块号为0,则说明该目录竟然不含数据，这不正常。于是返回NULL退出。否则我们就从节点所在设备读取
	// 指定的目录项数据块。如果不成功，则也返回NULL退出。另外，如果参数提供的文件名长度等于0,则也返回NULL退出。
	if (!namelen)
		return NULL;
	if (!(block = dir->i_zone[0]))
		return NULL;
	if (!(bh = bread(dir->i_dev, block)))
		return NULL;
	// 此时我们就在这个目录i节点数据块中循环查找最后未使用的空目录项。首先让目录项结构指针de指向缓冲块中的数据块部分，即第
	// 一个目录项处。其中i是目录中的目录项索引号，在循环开始时初始化为0。
	i = 0;
	de = (struct dir_entry *) bh->b_data;
	while (1) {
		// 如果当前目录项数据块已经搜索完毕，但还没有找到需要的空目录项，则释放当前目录项数据块，再读入目录的下一个逻辑块。如果
		// 对应的逻辑块不存在就创建一块。若读取或创建操作失败则返回空。如果此次读取的磁盘逻辑块数据返回的缓冲块指针为空，说明这
		// 块逻辑块可能是因为不存在而新创建的空块，则把目录项索引值加上一块逻辑块所能容纳的目录项数DIR_ENTRIES_PER_BLOCK，
		// 用以跳过该块并继续搜索。否则说明新读入的块上有目录项数据，于是让目录项结构指针de指向该块的缓冲块数据部分，然后在其中
		// 继续搜索。其中i/DIR_ENTRIES_PER_BLOCK可计算得到当前搜索的目录项i所在目录文件中的块号，而create_block()函数
		// （inode.c）则可读取或创建出在设备上对应的逻辑块。
		if ((char *)de >= BLOCK_SIZE + bh->b_data) {
			brelse(bh);
			bh = NULL;
			block = create_block(dir, i / DIR_ENTRIES_PER_BLOCK);
			if (!block)
				return NULL;
			if (!(bh = bread(dir->i_dev, block))) {          			// 若空则跳过该块继续。
				i += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			de = (struct dir_entry *) bh->b_data;
		}
		// 如果当前所操作的目录项序号i乘上结构大小所得长度值已经超过目录i节点信息所指出的目录数据长度值i_size，则说明整个目录
		// 文件数据中没有由于删除文件留下的空目录项，因此我们只能把需要添加的新目录项附加到目录文件数据的末端处。于是对该处目录
		// 项进行设置（置该目录项的i节点指针为空），并更新该目录文件的长度值（加上一个目录项的长度），然后设置目录的i节点已修改
		// 标志，再更新该目录的改变时间为当前时间。
		if (i * sizeof(struct dir_entry) >= dir->i_size) {
			de->inode = 0;
			dir->i_size = (i + 1) * sizeof(struct dir_entry);
			dir->i_dirt = 1;
			dir->i_ctime = CURRENT_TIME;
		}
		// 若当前搜索的目录项de的i节点为空，则表示找到一个还未使用的空闲目录项或是添加的新目录项。于是更新目录的修改时间为当前
		// 时间，并从用户数据区复制文件名到该目录项的文件名字段，置含有本目录项的相应高速缓冲块已修改标志。返回该目录项的指针以及
		// 该高速缓冲块的指针，退出。
		if (!de->inode) {
			dir->i_mtime = CURRENT_TIME;
			for (i = 0; i < NAME_LEN ; i++)
				de->name[i] = (i < namelen) ? get_fs_byte(name + i) : 0;
			bh->b_dirt = 1;
			*res_dir = de;
			return bh;
		}
		de++;           												// 如果该目录项已经被使用，则继续检测下一个目录项。
		i++;
	}
	// 本函数执行不到这里。这也许是Linus在写这段代码时，先复制了上面find_entry()函数的代码，而后修改成本函数的。
	brelse(bh);
	return NULL;
}

// 查找符号链接的i节点.
// 参数:dir - 目录i节点;inode - 目录项i节点.
// 返回:返回符号链接到文件的i节点指针.出错返回NULL.
static struct m_inode * follow_link(struct m_inode * dir, struct m_inode * inode)
{
	unsigned short fs;													// 用于临时保存fs段寄存器值.
	struct buffer_head * bh;

	// 首先判断函数参数的有效性.如果没有给出目录i节点,我们就使用进程任务结构中设置的根i节点,并把链接数增1.如果没有给出目录
	// 项i节点,则放回目录i节点后返回NULL.如果指定目录项不是一个符号链接,就直接返回目录项对应的i节点inode.
	if (!dir) {
		dir = current->root;
		dir->i_count++;
	}
	if (!inode) {
		iput(dir);
		return NULL;
	}
	if (!S_ISLNK(inode->i_mode)) {
		iput(dir);
		return inode;
	}
	// 然后取fs段寄存器值.fs通常保存着指向任务数据段的选择符0x17.如果fs没有指向用户数据段,或者给出的目录项i节点第1个直接块
	// 块号等于0,或者是读取第1个直接块出错,则放回dir和inode两个i节点并返回NULL退出.否则说明现在fs正指向用户数据段,并且我
	// 们已经成功地读取了符号链接目录项的文件内容,并且文件内容已经在bh指向的缓冲块数据区中.实际上,这个缓冲块数据区中仅包含一
	// 个链接指向的文件路径名字符串.
	__asm__("mov %%fs, %0":"=r" (fs));
	if (fs != 0x17 || !inode->i_zone[0] ||
	   !(bh = bread(inode->i_dev, inode->i_zone[0]))) {
		iput(dir);
		iput(inode);
		return NULL;
	}
	// 此时我们已经不需要符号链接目录项的i节点了,于是把它放回.现在遇到一个问题,那就是内核函数处理的用户数据都是存放在用户数据
	// 空间中的,并使用了fs段寄存器来从用户空间传递数据到内核空间中.而这里需要处理的数据却在内核空间中.因此为了正确地处理位于
	// 内核中的用户数据,我们需要让fs段寄存器临时指向内核空间,即让fs=0x10.并在调用函数处理完后再恢复原fs的值.最后释放相应缓冲
	// 块,并返回_namei()解析得到符号链接指向的文件i节点.
	iput(inode);
	__asm__("mov %0, %%fs"::"r" ((unsigned short) 0x10));
	inode = _namei(bh->b_data,dir,0);
	__asm__("mov %0, %%fs"::"r" (fs));
	brelse(bh);
	return inode;
}

/*
 *	get_dir()
 *
 * Getdir traverses the pathname until it hits the topmost directory.
 * It returns NULL on failure.
 */
/*
 *	get_dir()
 *
 * 该函数根据给出的路径名进行搜索,直到达到最顶端的目录.如果失败是返回NULL.
 */
// 从指定目录开始搜寻指定路径名的目录(或文件名)的i节点.
// 参数:pathname - 路径名;inode - 指定起始目录的i节点.
// 返回:目录或文件的i节点指针.失败时返回NULL.
static struct m_inode * get_dir(const char * pathname, struct m_inode * inode)
{
	char c;
	const char * thisname;
	struct buffer_head * bh;
	int namelen, inr;
	struct dir_entry * de;
	struct m_inode * dir;

	// 首先判断参数有效性.如果给出的指定目录的i节点指针inode为空,则使用当前进程的工作目录i节点.
	if (!inode) {
		inode = current->pwd;									// 进程的当前工作目录i节点.
		inode->i_count++;
	}
	// 如果用户指定路径名的第1个字符是'/',则说明路径名是绝对路径名.则应该从当前进程任务结构中设置的根(或伪根)i节点开始操作.
	// 于是我们需要先放回参数指定的或者设定的目录i节点,并取得进程使用的根i节点.然后把该i节点的引用计数加1,
	// 并删除路径名的第1个字符'/'.这样就可以保证进程只能以其设定的根i节点作为搜索的起点.
	if ((c = get_fs_byte(pathname)) == '/') {
		iput(inode);											// 放回原i节点.
		inode = current->root;									// 为进程指定的根i节点.
		pathname++;
		inode->i_count++;
	}
	// 然后针对路径名中的各个目录名部分和文件名进行循环处理。在循环处理过程中，我们先要对当前正在处理的目录名部分的i节点进行有效性判断，并且把
	// 变量thisname指向当前正在处理的目录名部分。如果该i节点表明当前处理的目录名部分不是目录类型，或者没有可进入该目录的访问许可，则放回该i节点
	// 并返回NULL退出。当然在刚进入循环时，当前目录的ｉ节点inode就是进程根i节点或者是当前工作目录的i节点，或者是参数指定的某个搜索起始目录的i节点。
	while (1) {
		thisname = pathname;
		if (!S_ISDIR(inode->i_mode) || !permission(inode, MAY_EXEC)) {
			iput(inode);
			return NULL;
		}
		// 每次循环我们处理路径名中一个目录名(或文件名)部分.因此在每次循环中我们都要从路径名字符串中分离出一个目录名(或文件名).方法是从当前路径名指针
		// pathname开始处搜索检测字符,直到字符是一个结尾符(NULL)或者是一个'/'字符.此时变量namelen正好是当前处理目录名部分的长度,而变量thisname正指向
		// 该目录名部分的开始处.此时如果字符是结尾符NULL,则表明已经搜索到路径名末尾,并已到达最后指定目录名或文件名,则返回该i节点指针退出.
		// 注意!如果路径名中最后一个名称也是一个目录名,但其后面没有加上'/'字符,则函数不会返回该最后目录名的i节点!例如:对于路径/usr/src/linux,该函数将
		// 只返回src/目录名的i节点.
		for(namelen = 0; (c = get_fs_byte(pathname++)) && (c != '/'); namelen++)
			/* nothing */ ;
		if (!c)
			return inode;
		// 在得到当前目录名部分(或文件名)后,我们调用查找目录项函数find_entry()在当前处理的目录中寻找指定名称的目录项.如果没有找到,则放回该i节点,并返回
		// NULL退出.然后在找到的目录项中取出其i节点号inr和设备号idev,释放包含该目录项的高速缓冲块并放回该i节点.然后取节点号inr的i节点inode,并以该目录
		// 项为当前目录继续循环处理路径名中的下一目录名部分(或文件名).如果当前处理的目录项是一个符号链接名,则使用follow_link()就可以得到其指向的目录项名i节点.
		if (!(bh = find_entry(&inode, thisname, namelen, &de))) {
			iput(inode);
			return NULL;
		}
		inr = de->inode;										// 当前目录名部分的i节点号.
		brelse(bh);
		dir = inode;
		if (!(inode = iget(dir->i_dev, inr))) {					// 取i节点内容.
			iput(dir);
			return NULL;
		}
		if (!(inode = follow_link(dir, inode)))
			return NULL;
        }
}

/*
 *	dir_namei()
 *
 * dir_namei() returns the inode of the directory of the
 * specified name, and the name within that directory.
 */
/*
 *	dir_namei()
 *
 * dir_namei()函数返回指定目录名的i节点指针,以及在最顶层目录的名称.
 */
// 参数:pathname - 目录路径名;namelen - 路径名长度;name - 返回的最顶层目录名.
// base - 搜索起始目录的i节点.
// 返回:指定目录名最顶层的i节点指针和最顶层目录名称及长度.出错时返回NULL.
// 注意!!这里"最顶层目录"是指路径名中最靠近末端的目录.
static struct m_inode * dir_namei(const char * pathname,
	int * namelen, const char ** name, struct m_inode * base)
{
	char c;
	const char * basename;
	struct m_inode * dir;

	// 首先取得指定路径名最顶层目录的i节点.然后对路径名pathname进行搜索检测,查出最后一个'/'字符后面的名字字符串,计算其长度,并且
	// 返回最顶层目录的i节点指针.注意!如果路径名最后一个字符是斜杠字符'/',那么返回的目录名为空,并且长度为0.但返回的i节点指针仍然
	// 指向最后一个'/'字符前目录名的i节点.
	if (!(dir = get_dir(pathname, base)))					// base是指定的起始目录i节点.
		return NULL;
	basename = pathname;
	while (c = get_fs_byte(pathname++))
		if (c == '/')
			basename = pathname;
	*namelen = pathname - basename - 1;
	*name = basename;
	return dir;
}

// 取指定路径名的i节点内部函数.
// 参数:pathname - 路径名;base - 搜索起点目录i节点;follow_links - 是否跟随符号链接的标志,1 - 需要,0 不需要.
struct m_inode * _namei(const char * pathname, struct m_inode * base,
	int follow_links)
{
	const char * basename;
	int inr, namelen;
	struct m_inode * inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	// 首先查找指定路径名中最顶层目录的目录名并得到其i节点.若不存在,则返回NULL退出.如果返回的最顶层名字的长度是0,则表示该路径名以一个目录名为
	// 最后一项.因此说明我们已经找到对应目录的i节点,可以直接返回该i节点退出.
	if (!(base = dir_namei(pathname, &namelen, &basename, base)))
		return NULL;
	if (!namelen)										/* special case: '/usr/' etc */
		return base;									/* 对应于'/usr/'等情况 */
	// 然后在返回的顶层目录中寻找指定文件名目录项的i节点.注意!因为如果最后也是一个目录名,但其后没有加'/',则不会返回该最后目录的i节点!例如:/usr/src/linux,
	// 将只返回src/目录名的i节点.因为函数dir_namei()将不以'/'结束的最后一个名字当作一个文件名来看待,因此这里需要单独对这种情况使用寻找目录项i节点函数
	// find_entry()进行处理.此时de中含有寻找到的目录项指针,而base是包含该目录项的目录的i节点指针.
	bh = find_entry(&base, basename, namelen, &de);
	if (!bh) {
		iput(base);
		return NULL;
	}
	// 接着取该目录项的i节点号,并释放包含该目录项的高速缓冲块并放回目录i节点.然后取对应节点号的i节点,修改其被访问时间为当前时间,并置已修改标志.最后返回
	// 该i节点指针inode.如果当前处理的目录项是一个符号链接名,则使用follow_link()得到其指向的目录项名的i节点.
	inr = de->inode;
	brelse(bh);
	if (!(inode = iget(base->i_dev, inr))) {
		iput(base);
		return NULL;
	}
	if (follow_links)
		inode = follow_link(base, inode);
	else
		iput(base);
	inode->i_atime = CURRENT_TIME;
	inode->i_dirt = 1;
	return inode;
}

// 取指定路径名的i节点，不跟随符号链接。
// 参数：pathname - 路径名。
// 返回：对应的i节点。
struct m_inode * lnamei(const char * pathname)
{
	return _namei(pathname, NULL, 0);
}

/*
 *	namei()
 *
 * is used by most simple commands to get the inode of a specified name.
 * Open, link etc use their own routines, but this is enough for things
 * like 'chmod' etc.
 */
/*
 *	namei()
 *
 * 该函数被许多简单命令用于取得指定路径名称的i节点.open,link等则使用它们自己的相应函数.但对于像修改模式"chmod"等这样的命令,该
 * 函数已足够用了.
 */
// 取指定路径名的i节点,跟随符号链接.
// 参数:pathname - 路径名.
// 返回:对应的i节点.
struct m_inode * namei(const char * pathname)
{
	return _namei(pathname, NULL, 1);
}

/*
 *	open_namei()
 *
 * namei for open - this is in fact almost the whole open-routine.
 */
/*
 * 	open_namei()
 *
 * open()函数使用的namei函数 - 这其实几乎是完整的打开文件程序.
 */
// 文件打开namei函数.
// 参数filename是文件名,flag是打开文件标志,它可取值:O_RDONLY(只读),O_WRONLY(只写)或O_RDWR(读写),以及O_CREAT(创建),
// O_EXCL(被创建文件必须不存在),O_APPEND(在文件尾添加数据)等其他一些标志的组合,如果本调用创建了一个新文件,则mode就用于指定
// 文件的许可属性.这些属性有S_IRWXU(文件宿主具有读,写和执行权限),S_IRUSR(用户具有读文件权限),S_IRWXG(组成员有读,写
// 执行)等等.对于新创建的文件,这些属性只应用于将来对文件的访问,创建了只读文件的打开调用也将返回一个读写的文件句柄.如果调用
// 操作成功,则返回文件句柄(文件描述符),否则返回出错码.参见sys/stat.h,fcntl.h.
// 返回:成功返回0,否则返回出错码;res_inode - 返回对应文件路径名的i节点指针.
int open_namei(const char * pathname, int flag, int mode,
	struct m_inode ** res_inode)
{
	const char * basename;
	int inr, dev, namelen;
	struct m_inode * dir, *inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	// 首先对函数参数进行合理的处理.如果文件访问模式标志是只读(O),但是文件截零标志O_TRUNC却置位了,则在文件打开标志中添加只写标志
	// O_WRONLY.这样做的原因是由于截零标志O_TRUNC必须在文件可写情况下有效.
	if ((flag & O_TRUNC) && !(flag & O_ACCMODE))
		flag |= O_WRONLY;
	// 使用当前进程的文件访问许可屏蔽码,屏蔽掉给定模式中的相应位,并添上普通文件标志I_REGULAR.
	// 该标志将用于打开的文件不存在而需要创建文件时,作为新文件的默认属性
	mode &= 0777 & ~current->umask;
	mode |= I_REGULAR;													// 常规文件标志.见参见include/const.h文件.
	// 然后根据指定的路径名寻找到对应的i节点,以及最顶端目录名及其长度.此时如果最顶端目录名长度为0(例如'/usr/'这种路径名的情况),那么
	// 若操作不是读写,创建和文件长度截0,则表示是在打开一个目录名文件操作.于是直接返回该目录的i节点并返回0退出.否则说明进程操作非法,于是
	// 放回该i节点,返回出错码.
	if (!(dir = dir_namei(pathname, &namelen, &basename, NULL)))
		return -ENOENT;
	// 文件名字为空，则返回
	if (!namelen) {														/* special case: '/usr/' etc */
		if (!(flag & (O_ACCMODE | O_CREAT | O_TRUNC))) {
			*res_inode = dir;
			return 0;
		}
		iput(dir);
		return -EISDIR;
	}
	// 接着根据上面得到的最顶层目录名的i节点dir,在其中查找取得路径名字符串中最后的文件名对应的目录项结构de,并同时得到该目录项所在的高速缓冲
	// 区指针.如果该高速缓冲指针为NULL,则表示没有找到对应文件名的目录项,因此只可能是创建文件操作.此时如果不是创建文件,则放回该目录的i节点,返回
	// 出错号退出.如果用户在该目录没有写的权力,则放回该目录的i节点,返回出错号退出.
	bh = find_entry(&dir, basename, namelen, &de);
	if (!bh) {
		if (!(flag & O_CREAT)) {                						// 不是创建文件，放回i节点
			iput(dir);
			return -ENOENT;
		}
		if (!permission(dir, MAY_WRITE)) {       						// 没有写权限，放回i节点
			iput(dir);
			return -EACCES;
		}
		// 现在我们确定了是创建操作并且有写操作许可。因此我们就在目录i节点对应设备上申请一个新的i节点给路径名上指定的文件使用。
		// 若失败则放回目录的i节点，并返回没有空间出错码。否则使用该新i节点，对其进行初始设置：置节点的用户id；对应节点访问模式；
		// 置已修改标志。然后并在指定目录dir中添加一个新目录项。
		inode = new_inode(dir->i_dev);
		if (!inode) {
			iput(dir);
			return -ENOSPC;
		}
		inode->i_uid = current->euid;
		inode->i_mode = mode;
		inode->i_dirt = 1;
		bh = add_entry(dir, basename, namelen, &de);
		// 如果返回的应该含有新目录项的调整缓冲区指针为NULL，则表示添加目录项操作失败。于是将该新i节点的引用连接计数减1,放回该
		// i节点与目录的i节点并返回出错码退出。否则说明添加目录项操作成功。于是我们来设置该新目录项的一些初始值：置i节点号为新申
		// 请到的i节点的号码；并置高速缓冲区修改标志。然后释放该高速缓冲区，放回目录的i节点。返回新目录项的i节点指针，并成功退出。
		if (!bh) {
			inode->i_nlinks--;
			iput(inode);
			iput(dir);
			return -ENOSPC;
		}
		de->inode = inode->i_num;
		bh->b_dirt = 1;
		brelse(bh);
		iput(dir);
		*res_inode = inode;
		return 0;
    }
	// 若上面(411行)在目录中取文件名对应目录项结构的操作成功(即bh不为NULL),则说明指定打开的文件已经存在.于是取出该目录项的i节点和其所在设备号,并释放
	// 该高速缓冲区以及放回目录的i节点.如果此时独占操作标志O_EXCL置位,但现在文件已经存在,则返回文件已存在出错码退出.
	inr = de->inode;
	dev = dir->i_dev;
	brelse(bh);
	if (flag & O_EXCL) {
		iput(dir);
		return -EEXIST;
	}
	// 然后我们读取该目录项的i节点内容.若该i节点是一个目录的i节点并且访问模式是只写或读写,或者没有访问的许可权限,则放回该i节点,返回访问权限出错码退出.
	if (!(inode = follow_link(dir, iget(dev, inr))))
		return -EACCES;
	if ((S_ISDIR(inode->i_mode) && (flag & O_ACCMODE)) ||
	    !permission(inode, ACC_MODE(flag))) {
		iput(inode);
		return -EPERM;
	}
	// 接着我们更新该i节点的访问时间字段值为当前时间.如果设立了截0标志,则将该i节点的文件长度截为0.最后返回该目录项i节点的指针.并返回0(成功).
	inode->i_atime = CURRENT_TIME;
	if (flag & O_TRUNC)
		truncate(inode);
	*res_inode = inode;
	return 0;
}

// 创建一个设备特殊文件或普通文件节点（node）。
// 该函数创建名称为filename，由mode和dev指定的文件系统节点（普通文件、设备特殊文件或命名管道）。
// 参数：filename - 路径名；mode - 指定使用许可以及所创建节点的类型；dev - 设备号。
int sys_mknod(const char * filename, int mode, int dev)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	// 首先检查操作许可和参数的有效性并取路径名中顶层目录的i节点。如果不是超级用户，则返回访问许可出错码。
	// 如果不是超级用户，则返回访问许可出错码。
	if (!suser())
		return -EPERM;
	// 如果找不到对应路径名中顶层目录的i节点，则返回出错码。
	if (!(dir = dir_namei(filename, &namelen, &basename, NULL)))
		return -ENOENT;
	// 如果最顶端的文件名长度为0，则说明给出的路径名最后没有指定文件名，放回该目录i节点，返回出错码退出。
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	// 如果在该目录中没有写的权限，则放回该目录的i节点，返回访问许可出错码退出。
	if (!permission(dir, MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
	// 然后我们搜索一下路径名指定的文件是否已经存在。若已经存在则不能创建同名文件节点。如果对应路径名上最后的文件名的
	// 目录项已经存在，则释放包含该目录项的缓冲区块并放回目录的i节点，返回文件已经存在的出错退出。
	bh = find_entry(&dir, basename, namelen, &de);
	if (bh) {
		brelse(bh);
		iput(dir);
		return -EEXIST;
	}
	// 否则我们就申请一个新的i节点，并设置该i节点的属性模式。
	inode = new_inode(dir->i_dev);
	if (!inode) {
		iput(dir);
		return -ENOSPC;
	}
	inode->i_mode = mode;
	// 如果要创建的是块设备文件或者是字符设备文件，则令i节点的直接逻辑块指针0等于设备号。即对于设备文件来说，
	// 其i节点的i_zone[0]中存放的是该设备文件所定义设备的设备号。
	if (S_ISBLK(mode) || S_ISCHR(mode))
		inode->i_zone[0] = dev;
	// 设置该i节点的修改时间、访问时间为当前时间，并设置i节点已修改标志。
	inode->i_mtime = inode->i_atime = CURRENT_TIME;
	inode->i_dirt = 1;
	// 接着为这个新的i节点在目录中新添加一个目录项。如果失败（包含该目录项的高速缓冲块指针为NULL），则放回目录的i节点；
	// 把所申请的i节点引用连接计数复位，并放回该i节点，返回出错码退出。
	bh = add_entry(dir, basename, namelen, &de);
	if (!bh) {
		iput(dir);
		inode->i_nlinks = 0;
		iput(inode);
		return -ENOSPC;
	}
	// 现在添加目录项操作也成功了，于是我们来设置这个目录项内容。令该目录项的i节点字段等于新i节点号，并置高速缓冲区已修
	// 改标志，放回目录和新的i节点，释放高速缓冲区，最后返回0（成功）。
	de->inode = inode->i_num;
	bh->b_dirt = 1;
	iput(dir);
	iput(inode);
	brelse(bh);
	return 0;
}

// 创建一个目录。
// 参数：pathname - 路径名；mode - 目录使用的权限属性。
// 返回：成功则返回0,否则返回出错码。
int sys_mkdir(const char * pathname, int mode)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh, *dir_block;
	struct dir_entry * de;

	// 首先检查参数的有效性并取路径名中顶层目录的i节点。如果找不到对应路径名中顶层目录的i节点，则返回出错码。
	if (!(dir = dir_namei(pathname,&namelen,&basename, NULL)))
		return -ENOENT;
	// 如果最顶端文件名长度为0,则说明给出的路径名最后没有指定文件名，放回该目录i节点，返回出错码退出。
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	// 如果在该目录中没有写的权限，则放回该目录i节点，返回访问许可出错码退出。
	// 如果不是超级用户，则返回访问许可出错码。
	if (!permission(dir, MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
	// 然后我们搜索一下路径名指定的目录名是否已经存在。若已经存在则不能创建同名目录节点。如果对应路径名上最后的目录名的目录
	// 项已经存在，则释放包含该目录项的缓冲区块并放回目录的i节点，返回文件已经存在 的出错码退出。否则我们就申请一个新的i节
	// 点，并设置该i节点的属性模式：置该新i节点对应的文件长度为32字节（2个目录项的大小）、置节点已修改标志，以及节点的修改
	// 时间和访问时间。2个目录项分别用于'.'和'..'目录。
	bh = find_entry(&dir, basename, namelen, &de);
	if (bh) {
		brelse(bh);
		iput(dir);
		return -EEXIST;
	}
	inode = new_inode(dir->i_dev);
	if (!inode) {           						// 若不成功则放回目录的i节点，返回无空间出错码。
		iput(dir);
		return -ENOSPC;
	}
	inode->i_size = 32;
	inode->i_dirt = 1;
	inode->i_mtime = inode->i_atime = CURRENT_TIME;
	// 接着为该新i节点申请一用于保存目录项数据的磁盘块，并令i节点的第一个直接块指针等于该块号。如果申请失败则放回对应目录
	// 的i节点；复位新申请的i节点连接计数；放回该新的i节点，返回没有空间出错码退出。否则置该新的i节点已修改标志。
	if (!(inode->i_zone[0] = new_block(inode->i_dev))) {
		iput(dir);
		inode->i_nlinks--;
		iput(inode);
		return -ENOSPC;
	}
	inode->i_dirt = 1;
	// 从设备上读取新申请的磁盘块（目的是把对应块放到高速缓冲区中）。若出错，则放回对应目录的i节点；释放申请的磁盘块；复位新
	// 申请的i节点连接计数；放回该新的i节点，返回没有空间出错码退出。
	if (!(dir_block = bread(inode->i_dev, inode->i_zone[0]))) {
		iput(dir);
		inode->i_nlinks--;
		iput(inode);
		return -ERROR;
	}
	// 然后我们在缓冲块中建立起所创建目录文件中的2个默认的新目录项（'.'和'..'）结构数据。首先令de指向存放目录项的数据块，然
	// 后置该目录项的i节点号字段等于新申请的i节点号，名字字段等于“.”。然后de指向下一个目录项结构，并在该结构中存放上级目录的
	// i节点号和名字“..”。然后设置该高速缓冲块已修改标志，并释放该缓冲区块。再初始化设置新i节点的模式字段，并置该i节点已修改
	// 标志。
	de = (struct dir_entry *) dir_block->b_data;
	de->inode = inode->i_num;         				// 设置'.'目录项。
	strcpy(de->name, ".");
	de++;
	de->inode = dir->i_num;         				// 设置'..'目录项。
	strcpy(de->name, "..");
	inode->i_nlinks = 2;
	dir_block->b_dirt = 1;
	brelse(dir_block);
	inode->i_mode = I_DIRECTORY | (mode & 0777 & ~current->umask);
	inode->i_dirt = 1;
	// 现在我们在指定目录中新添加一个目录项，用于存放新建目录的i节点和目录名。如果失败（包含该目录项的高速缓冲区指针为NULL），
	// 则放回目录的i节点；所申请的i节点引用连接计数复位，并放回该i节点。返回出错码退出。
	bh = add_entry(dir, basename, namelen, &de);
	if (!bh) {
		iput(dir);
		inode->i_nlinks = 0;
		iput(inode);
		return -ENOSPC;
	}
	// 最后令该新目录项的i节点字段等于新i节点号，并置高速缓冲块已修改标志，放回目录和新的i节点，释放高速缓冲区，最后返回0（成功）。
	de->inode = inode->i_num;
	bh->b_dirt = 1;
	dir->i_nlinks++;
	dir->i_dirt = 1;
	iput(dir);
	iput(inode);
	brelse(bh);
	return 0;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
/*
 * 用于检查指定的目录是否为空的子程序（用于rmdir系统调用）。
 */
// 检查指定目录是否为空。
// 参数：inode - 指定目录的i节点指针。
// 返回：1 - 目录中是空的；0 - 不空。
static int empty_dir(struct m_inode * inode)
{
	int nr, block;
	int len;
	struct buffer_head * bh;
	struct dir_entry * de;

	// 首先计算指定目录中现有目录项个数并检查开始两个特定目录项中信息是否正确。一个目录中应该起码有2个目录项：即“.”和“..”。
	// 如果目录项个数少于2个或者该目录i节点的第1个直接块没有指向任何磁盘块号，或者该直接块读不出，则显示警告信息“设备dev上
	// 目录错”，返回0（失败）。
	len = inode->i_size / sizeof (struct dir_entry);        		// 目录中目录项个数。
	if (len < 2 || !inode->i_zone[0] ||
	    !(bh = bread(inode->i_dev, inode->i_zone[0]))) {
	    	printk("warning - bad directory on dev %04x\n", inode->i_dev);
		return 0;
	}
	// 此时bh所指缓冲块中含有目录项数据。我们让目录项指针de指向缓冲块中第1个目录项。对于第1个目录项（“.”），它的i节点号字段
	// inode应该等于当前目录的i节点号。对于第2个目录项（“..”），节点号字段inode应该等于上一层目录的i节点号，不会为0.因此，
	// 如果第1个目录项的i节点号字段值不等于该目录的i节点号，或者第2个目录项的i节点号字段为零，或者两个目录项的名字字段不分别
	// 等于“.”和“..”，则显示出错警告信息“设备dev上目录错”，并返回0。
	de = (struct dir_entry *) bh->b_data;
	if (de[0].inode != inode->i_num || !de[1].inode ||
	    strcmp(".", de[0].name) || strcmp("..", de[1].name)) {
	    	printk("warning - bad directory on dev %04x\n", inode->i_dev);
		return 0;
	}
	// 然后我们令nr等于目录项序号（从0开始计）；de指向第三个目录项。并循环检测该目录中其余所有的（len - 2）个目录项，看有没有
	// 目录项的i节点号字段不为0（被使用）。
	nr = 2;
	de += 2;
	while (nr < len) {
		// 如果该块磁盘块中的目录项已经全部检测完毕，则释放该磁盘块的缓冲块，并读取目录数据文件中下一块含有目录项的磁盘块。读取的方
		// 法是根据当前检测的目录项序号nr计算出对应目录项在目录数据文件中的数据块号（nr/DIR_ENTRIES_PER_BLOCK），然后使用bmap()
		// 函数取得对应的盘块号block，再使用读设备块函数bread()把相应盘块读入缓冲块中，并返回该缓冲块的指针。若所读取的相应盘块没有
		// 使用（或已经不用，如文件已经删除等），则继续读下一块，若读不出，则出错返回0.否则让de指向读出块的第1个目录项。
		if ((void *) de >= (void *) (bh->b_data + BLOCK_SIZE)) {
			brelse(bh);
			block = bmap(inode, nr / DIR_ENTRIES_PER_BLOCK);
			if (!block) {
				nr += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			if (!(bh = bread(inode->i_dev, block)))
				return 0;
			de = (struct dir_entry *) bh->b_data;
		}
		// 对于de指向的当前目录项，如果该目录项的i节点号字段不等于0,则表示该目录项目前正被使用，则释放该高速缓冲区，返回0退出。否则，
		// 若还没有查询完该目录中的所有目录项，则把目录项序号nr增1、de指向下一个目录项，继续检测。
		if (de->inode) {
			brelse(bh);
			return 0;
		}
		de++;
		nr++;
	}
	// 执行到这里说明该目录中没有找到已用的目录项（当然除了头两个以外），则释放缓冲块返回1。
	brelse(bh);
	return 1;
}

// 删除目录。
// 参数：name - 目录名（路径名）。
// 返回：返回0表示成功，否则返回出错号。
int sys_rmdir(const char * name)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	// 首先检查参数的有效性并取路径名中顶层目录的i节点。如果找不到对应路径名中顶层目录的i节点，则返回出错码。如果最顶端
	// 文件名长度为0,则说明给出的路径名最后没有指定文件名，放回该目录i节点，返回出错码退出。如果在该目录中没有写的权限，
	// 则放回该目录i节点，返回访问许可出错码退出。如果不是超级用户，则返回访问许可出错码。
	if (!(dir = dir_namei(name, &namelen, &basename, NULL)))
		return -ENOENT;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
	// 然后根据指定目录的i节点和目录名利用函数find_entry()寻找对应目录项，并返回包含该目录项的缓冲块指针bh、包含该目录
	// 项的目录的i节点指针dir和该目录项指针de。再根据该目录项de中的i节点号利用iget()函数得到对应的i节点inode。如果对应
	// 路径名上最后目录的名的目录项不存在，则释放包含该目录项的高速缓冲区，放回目录的i节点，返回文件不存在出错码，并退出。
	// 如果取目录项的i节点出错，则放回目录的i节点，并释放含有目录项的高速缓冲区，返回出错号。
	bh = find_entry(&dir, basename, namelen, &de);
	if (!bh) {
		iput(dir);
		return -ENOENT;
	}
	if (!(inode = iget(dir->i_dev, de->inode))) {
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	// 此时我们已有包含要被删除目录项的目录i节点dir、要被删除目录项的i节点inode和要被删除目录项指针de。下面我们通过对这3
	// 个对象中信息的检查来验证删除操作的可行性。
	// 若该目录设置了受限删除标志并且进程的有效用户id（euid）不是root，并且进程的有效用户id（euid）不等于该i节点的用户
	// id，则表示当前进程没有权限删除该目录，于是放回包含要删除目录名的目录i节点和该要删除目录的i节点，然后释放高速缓冲区，
	// 返回出错码。
	if ((dir->i_mode & S_ISVTX) && current->euid &&
	    inode->i_uid != current->euid) {
		iput(dir);
		iput(inode);
		brelse(bh);
		return -EPERM;
	}
	// 如果要被删除的目录项i节点的设备号不等于包含该目录项的目录的设备号，或者该被删除目录的引用连接计数大于1（表示有符号连
	// 接等），则不能删除该目录。于是释放包含要删除目录名的目录i节点和该要删除目录的i节点，释放高速缓冲块，返回出错码。
	if (inode->i_dev != dir->i_dev || inode->i_count > 1) {
		iput(dir);
		iput(inode);
		brelse(bh);
		return -EPERM;
	}
	// 如果要被删除目录的目录项i节点就等于包含该需删除目录的目录i节点，则表示试图删除“.”目录，这是不允许的。于是放回包含要删
	// 除目录名的目录i节点和要删除目录的i节点，释放高速缓冲块，返回出错码。
	if (inode == dir) {						/* we may not delete ".", but "../dir" is ok */
		iput(inode);
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	// 若要被删除目录i节点的属性表明这不是一个目录，则本删除操作的前提完全不存在。于是放回包含删除目录名的目录i节点和该要删除
	// 目录的i节点，释放高速缓冲块，返回出错码。
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		iput(dir);
		brelse(bh);
		return -ENOTDIR;
	}
	// 若该需要被删除的目录不空，则也不能删除。于是放回包含要删除目录名的目录i节点和该要删除目录的i节点，释放高速缓冲块，返回
	// 出错码。
	if (!empty_dir(inode)) {
		iput(inode);
		iput(dir);
		brelse(bh);
		return -ENOTEMPTY;
	}
	// 对于一个空目录，其目录项链接数应该为2（链接到上层目录和本目录）。若该需被删除目录的i节点的连接数不等于2,则显示警告信息，
	// 但删除操作仍然执行。于是置该需删除目录的目录项的i节点号字段为0,表示该目录项不再使用，并置含有该目录项的调整缓冲块已修改
	// 标志，并释放该缓冲块。然后再置被删除目录i节点的链接数为0（表示空闲），并置i节点已修改标志。
	if (inode->i_nlinks != 2)
		printk("empty directory has nlink!=2 (%d)", inode->i_nlinks);
	de->inode = 0;
	bh->b_dirt = 1;
	brelse(bh);
	inode->i_nlinks = 0;
	inode->i_dirt = 1;
	// 再将包含被删除目录名的目录的i节点链接计数减1,修改其改变时间和修改时间为当前时间，并置该节点已修改标志。最后放回包含要删除
	// 目录名的目录i节点和该要删除目录的i节点，返回0（删除操作成功）。
	dir->i_nlinks--;
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	dir->i_dirt = 1;
	iput(dir);
	iput(inode);
	return 0;
}

// 删除（释放）文件名对应的目录项。
// 从文件系统删除一个名字。如果是文件的最后一个链接，并且没有进程正打开该文件，则该文件也将被删除，并释放所占用的设备空间。
// 参数：name - 文件名（路径名）。
// 返回：成功则返回0,否则返回出错号。
int sys_unlink(const char * name)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	// 首先检查参数的有效性并取路径名中顶层目录的i节点。如果找不到对应路径名中顶层目录的i节点，则返回出错码。如果最顶端
	// 文件名长度为0,则说明给出的路径名最后没有指定文件名，放回该目录i节点，返回出错码退出。如果在该目录中没有写的权限，
	// 则放回该目录i节点，返回访问许可出错码退出。如果不是超级用户，则返回访问许可出错码。
	if (!(dir = dir_namei(name, &namelen, &basename, NULL)))
		return -ENOENT;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir, MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
	// 然后根据指定目录的i节点和目录名利用函数find_entry()寻找对应目录项，并返回包含该目录项的缓冲块指针bh、包含该目录
	// 项的目录的i节点指针dir和该目录项指针de。再根据该目录项de中的i节点号利用iget()函数得到对应的i节点inode。如果对应
	// 路径名上最后目录的名的目录项不存在，则释放包含该目录项的高速缓冲区，放回目录的i节点，返回文件不存在出错码，并退出。
	// 如果取目录项的i节点出错，则放回目录的i节点，并释放含有目录项的高速缓冲区，返回出错号。
	bh = find_entry(&dir, basename, namelen, &de);
	if (!bh) {
		iput(dir);
		return -ENOENT;
	}
	if (!(inode = iget(dir->i_dev, de->inode))) {
		iput(dir);
		brelse(bh);
		return -ENOENT;
	}
	// 此时我们已有包含要被删除目录项的目录i节点dir、要被删除目录项的i节点inode和要被删除目录项指针de。下面我们通过对这3
	// 个对象中信息的检查来验证删除操作的可行性。
	// 若该目录设置了受限删除标志并且进程的有效用户id（euid）不是root，并且进程的有效用户id（euid）不等于该i节点的用户
	// id，并且进程的euid也不等于目录i节点的用户id，则表示当前进程没有权限删除该目录，于是放回包含要删除目录名的目录i节点
	// 和该要删除目录的i节点，然后释放高速缓冲区，返回出错码。
	if ((dir->i_mode & S_ISVTX) && !suser() &&
	    current->euid != inode->i_uid &&
	    current->euid != dir->i_uid) {
		iput(dir);
		iput(inode);
		brelse(bh);
		return -EPERM;
	}
	// 如果该指定文件名是一个目录，则也不能删除。放回该目录i节点和该文件名目录项的i节点，释放包含该目录项的缓冲块，返回出错号。
	if (S_ISDIR(inode->i_mode)) {
		iput(inode);
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	// 如果该i节点的链接计数值已经为0,则显示警告信息，并修正其为1。
	if (!inode->i_nlinks) {
		printk("Deleting nonexistent file (%04x:%d), %d\n",
			inode->i_dev, inode->i_num, inode->i_nlinks);
		inode->i_nlinks = 1;
	}
	// 现在我们可以删除文件名对应的目录项了。于是将该文件名目录项中的i节点号字段置为0,表示释放该目录项，并设置包含该目录项的缓
	// 冲块已修改标志，释放该高速缓冲块。
	de->inode = 0;
	bh->b_dirt = 1;
	brelse(bh);
	// 然后把文件名对应i节点的链接数减1,置已修改标志，更新改变时间为当前时间。最后放回该i节点和目录的i节点，返回0（成功）。如果
	// 是文件的最后一个链接，即i节点链接数减1后等于0,并且此时没有进程正打开该文件，那么在调用iput()放回i节点时，该文件也将被删除
	// 并释放所占用的设备空间。参见fs/inode.c。
	inode->i_nlinks--;
	inode->i_dirt = 1;
	inode->i_ctime = CURRENT_TIME;
	iput(inode);
	iput(dir);
	return 0;
}

// 建立符号链接。
// 为一个已存在文件创建一个符号链接（也称为软连接 - hard link）。
// 参数：oldname - 原路径名；newname - 新的路径名。
// 返回：若成功则返回0，否则返回出错号。
int sys_symlink(const char * oldname, const char * newname)
{
	struct dir_entry * de;
	struct m_inode * dir, * inode;
	struct buffer_head * bh, * name_block;
	const char * basename;
	int namelen, i;
	char c;

	// 首先查找新路径名的最顶层目录的i节点dir，并返回最后的文件名及其长度。如果目录的i节点没有找到，则返回出错号。如果新路径名
	// 中不包括文件名，则放回新路径名目录的i节点，返回出错号。另外，如果用户没有在新目录中写的权限，则也不能建立连接，于是放回
	// 新路径名目录的i节点，返回出错号。
	dir = dir_namei(newname, &namelen, &basename, NULL);
	if (!dir)
		return -EACCES;
	if (!namelen) {
		iput(dir);
		return -EPERM;
	}
	if (!permission(dir, MAY_WRITE)) {
		iput(dir);
		return -EACCES;
	}
	// 现在我们在目录指定设备上申请一个新的i节点，并设置该i节点模式为符号链接类型以及进程规定的模式屏蔽码。并且设置该i节点已修
	// 改标志。
	if (!(inode = new_inode(dir->i_dev))) {
		iput(dir);
		return -ENOSPC;
	}
	inode->i_mode = S_IFLNK | (0777 & ~current->umask);
	inode->i_dirt = 1;
	// 为了保存符号链接路径名字符串信息，我们需要为该i节点申请一个磁盘块，并让i节点的第1个直接块号i_zone[0]等于得到的逻辑块号。
	// 然后置i节点已修改标志。如果申请失败则放回对应目录的i节点；复位新申请的i节点链接计数；放回该新的i节点，返回没有空间出错码
	// 退出。
	if (!(inode->i_zone[0] = new_block(inode->i_dev))) {
		iput(dir);
		inode->i_nlinks--;
		iput(inode);
		return -ENOSPC;
	}
	inode->i_dirt = 1;
	// 然后从设备上读取新申请的磁盘块（目的是把对应块放到高速缓冲区中）。若出错，则放回对应目录的i节点；复位新申请的i节点链接计数；
	// 放回该新的i节点，返回没有空间出错码退出。
	if (!(name_block = bread(inode->i_dev, inode->i_zone[0]))) {
		iput(dir);
		inode->i_nlinks--;
		iput(inode);
		return -ERROR;
	}
	// 现在我们可以把符号链接名字字符串放入这个盘块中了。盘块长度为1024字节，因此默认符号链接名长度最大也只能是1024字节。我们把用
	// 户空间中的符号链接名字符串复制到盘块所在的缓冲块中，并置缓冲块已修改标志。为防止用户提供的字符串没有以NULL结尾，我们在缓冲块
	// 数据区最后一个字节处放上一个NULL。然后释放该缓冲块，并设置i节点对应文件中数据长度等于符号链接名字符串长度，并置i节点已修改
	// 标志。
	i = 0;
	while (i < 1023 && (c = get_fs_byte(oldname++)))
		name_block->b_data[i++] = c;
	name_block->b_data[i] = 0;
	name_block->b_dirt = 1;
	brelse(name_block);
	inode->i_size = i;
	inode->i_dirt = 1;
	// 然后我们搜索一下路径名指定的符号链接名是否已经存在。若已经存在则不能创建同名目录项i节点。如果对应符号链接文件名已经存在，则
	// 释放包含该目录项的缓冲区块，复位新申请的i节点连接计数，并施加目录的i节点，返回文件已经存在的出错码退出。
	bh = find_entry(&dir, basename, namelen, &de);
	if (bh) {
		inode->i_nlinks--;
		iput(inode);
		brelse(bh);
		iput(dir);
		return -EEXIST;
	}
	// 现在我们在指定目录中新添加一个目录项，用于存放新建符号链接文件名的i节点号和目录名。如果失败（包含该目录项的高速缓冲区指针为
	// NULL），则放回目录的i节点；所申请的i节点引用链接计数复位，并放回该i节点。返回出错码退出。
	bh = add_entry(dir, basename, namelen, &de);
	if (!bh) {
		inode->i_nlinks--;
		iput(inode);
		iput(dir);
		return -ENOSPC;
	}
	// 最后令该新目录项的i节点字段等于新i节点号，并置高速缓冲块已修改标志，释放高速缓冲块，放回目录和新的i节点，最后返回0（成功）。
	de->inode = inode->i_num;
	bh->b_dirt = 1;
	brelse(bh);
	iput(dir);
	iput(inode);
	return 0;
}

// 为文件建立一个文件名目录项。
// 为一个已存在的文件创建一个新链接（也称为硬连接 - hard link）。
// 参数：oldname - 原路径名；newname - 新的路径名。
// 返回：若成功则返回0,否则返回出错号。
int sys_link(const char * oldname, const char * newname)
{
	struct dir_entry * de;
	struct m_inode * oldinode, * dir;
	struct buffer_head * bh;
	const char * basename;
	int namelen;

	// 首先对原文件名进行有效性验证，它应该存在并且不是一个目录名。所以我们先取原文件路径名对应的i节点oldinode。如果为0，则
	// 表示出错，返回出错号。如果原路径名对应的是一个目录名，则放回该i节点，也返回出错号。
	oldinode = namei(oldname);
	if (!oldinode)
		return -ENOENT;
	if (S_ISDIR(oldinode->i_mode)) {
		iput(oldinode);
		return -EPERM;
	}
	// 然后查找新路径名的最顶层目录的i节点dir，并返回最后的文件名及其长度。如果目录的i节点没有找到，则放回原路径名的i节点，返
	// 回出错号。如果新路径名中不包括文件名，则放回原路径名i节点和新路径名目录的i节点，返回出错号。
	dir = dir_namei(newname, &namelen, &basename, NULL);
	if (!dir) {
		iput(oldinode);
		return -EACCES;
	}
	if (!namelen) {
		iput(oldinode);
		iput(dir);
		return -EPERM;
	}
	// 我们不能跨设备建立硬链接。因此如果新路径名顶层目录的设备号与原路径名的设备号不一样，则放回新路径名目录的i节点和原路径名
	// 的i节点，返回出错号。另外，如果用户没有在新目录中写的权限，则也不能建立连接，于是放回新路径名目录的i节点和原路径名的i节点
	// 返回出错号。
	if (dir->i_dev != oldinode->i_dev) {
		iput(dir);
		iput(oldinode);
		return -EXDEV;
	}
	if (!permission(dir, MAY_WRITE)) {
		iput(dir);
		iput(oldinode);
		return -EACCES;
	}
	// 现在查询该新路径名是否已经存在，如果存在则也不能建立链接。于是释放包含该已存在目录项的高速缓冲块，放回新路径名目录的i节点
	// 和原路径名的i节点，返回出错号。
	bh = find_entry(&dir, basename, namelen, &de);
	if (bh) {
		brelse(bh);
		iput(dir);
		iput(oldinode);
		return -EEXIST;
	}
	// 现在所有条件都满足了，于是我们在新目录中添加一个目录项。若失败则放回该目录的i节点和原路径名的i节点，返回出错号。否则初始
	// 设置该目录项的i节点号等于原路径名的i节点号，并置包含该新添目录的缓冲块已修改标志，释放该缓冲块，放回目录的i节点。
	bh = add_entry(dir, basename, namelen, &de);
	if (!bh) {
		iput(dir);
		iput(oldinode);
		return -ENOSPC;
	}
	de->inode = oldinode->i_num;
	bh->b_dirt = 1;
	brelse(bh);
	iput(dir);
	// 再将原节点的链接计数加1,修改其改变时间为当前时间，并设置i节点已修改标志。最后放回原路径名的i节点，并返回0（成功）。
	oldinode->i_nlinks++;
	oldinode->i_ctime = CURRENT_TIME;
	oldinode->i_dirt = 1;
	iput(oldinode);
	return 0;
}

