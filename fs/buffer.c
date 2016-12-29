/*
 *  linux/fs/buffer.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'buffer.c' implements the buffer-cache functions. Race-conditions have
 * been avoided by NEVER letting a interrupt change a buffer (except for the
 * data, of course), but instead letting the caller do it. NOTE! As interrupts
 * can wake up a caller, some cli-sti sequences are needed to check for
 * sleep-on-calls. These should be extremely quick, though (I hope).
 */
/*
 * 'buffer.c'用于实现缓冲区高速缓存功能.通过不让中断处理过程改变缓冲区,而是让调用者来执行,避免了竞争条件(当然除改变数据外).
 * 注意!由于中断可以唤醒一个调用者,因此就需要开关中断指令(cli-sti)序列来检测由于调用而睡眠.但需要非常快(我希望是这样).
 */

/*
 * NOTE! There is one discordant note here: checking floppies for
 * disk change. This is where it fits best, I think, as it should
 * invalidate changed floppy-disk-caches.
 */
/*
 * 注意!有一个程序应不属于这里:检测软盘是否更换.但我想这里是放置该程序最好的地方了,因为它需要使已更换软盘缓冲失效.
 */

#include <stdarg.h>

//#include <linux/config.h>
#include <linux/sched.h>
#include <linux/fs.h>
//#include <linux/kernel.h>
#include <asm/system.h>
//#include <asm/io.h>

// 变量end是由编译时的连接程序ld生成,用于表明内核代码的末端,即指明内核模块末端位置.也可以从编译时生成的System.map文件中查出.这里用它
// 来表明高速缓冲区开始于内核代码末端位置.
// buffer_wait变量是等待空闲缓冲块而睡眠的任务队列头指针.它与缓冲块头部结构中b_wait指针的作用不同.当任务申请一个缓冲块而正好遇到系统
// 缺乏可用空闲缓冲块时,当前任务就会被添加到buffer_wait睡眠等待队列中.而b_wait则是专门供等待指定缓冲块(即b_wait对应的缓冲块)的任务
// 使用的等待队列头指针.
extern int end;
struct buffer_head * start_buffer = (struct buffer_head *) &end;
struct buffer_head * hash_table[NR_HASH];							// NR_HASH = 307项.
static struct buffer_head * free_list;								// 空闲缓冲块链表头指针.
static struct task_struct * buffer_wait = NULL;						// 等待空闲缓冲块而睡眠的任务队列.
// 下面定义系统缓冲区中含有的缓冲块个数.这里,NR_BUFFERS是一个定义在linux/fs.h头文件的宏,其值即是变量名nr_buffers,并且在fs.h文件声明
// 为全局变量.
// 大写名称通常都是一个宏名称,Linus这样编写代码是为了利用这个大写名称来隐含地表示nr_buffers是一个在内核初始化之后不再改变的"常量".它将在
// 初始化函数buffer_init()中被设置.
int NR_BUFFERS = 0;													// 系统含有缓冲块个数.

// 等待指定缓冲块解锁.
// 如果指定的缓冲块bh已经上锁就让进程不可中断地睡眠在该缓冲块的等待队列b_wait中.在缓冲块解锁时,其等待队列上的所有进程将被唤醒.虽然是在关闭
// 中断(cli)之后去睡眠的,但这样做并不会影响在其他进程上下文中响应中断.因为每个进程都在自己的TSS段中保存了标志寄存器EFLAGS的值,所在在进程
// 切换时CPU中当前EFLAGS的值也随之改变.使用sleep_on()进入睡眠状态的进程需要用wake_up()明确地唤醒.
static inline void wait_on_buffer(struct buffer_head * bh)
{
	cli();							// 关中断
	while (bh->b_lock)				// 如果已被上锁则进程进入睡眠,等待其解锁.
		sleep_on(&bh->b_wait);
	sti();							// 开中断.
}

// 设备数据同步。
// 同步设备和内存高速缓冲中数据。其中，sync_inodes()定义在inode.c。
int sys_sync(void)
{
	int i;
	struct buffer_head * bh;

	// 首先调用i节点同步函数，把内在i节点表中所有修改过的i节点写入高速缓冲中。然后扫描所有高速缓冲区，对已被修改的缓冲块
	// 产生写盘请求，将缓冲中数据写入盘中，做到高速缓冲中的数据与设备中的同步。
	sync_inodes();							/* write out inodes into buffers */
	bh = start_buffer;      				// bh指向缓冲开始处。
	for (i = 0 ; i < NR_BUFFERS ; i++, bh++) {
		wait_on_buffer(bh);             	// 等待缓冲区解锁（如果已上锁的话）。
		if (bh->b_dirt)
			ll_rw_block(WRITE, bh);  		// 产生写设备块请求。
	}
	return 0;
}

// 对指定设备进行高速缓冲数据与设备上数据的同步操作。
// 该函数首先搜索高速缓冲区中所有缓冲块。对于指定设备dev的缓冲块，若其数据已被修改过就写入盘中（同步操作）。然后
// 把内存中i节点数据写入高速缓冲中。之后再指定设备dev执行一次与上述相同的写盘操作。
int sync_dev(int dev)
{
	int i;
	struct buffer_head * bh;

	// 首先对参数指定的设备执行数据同步操作，让设备上的数据与高速缓冲区中的数据同步。方法是扫描高速缓冲区中所有缓冲块，
	// 对指定设备dev的缓冲块，先检测其是否已被上锁，若已被锁就睡眠等待其解锁。然后再判断一次该缓冲块是否还是指定设备的
	// 缓冲块并且已修改过（b_dirt标志置位），若是就对其执行写盘操作。因为在我们睡眠期间该缓冲块有可能已被释放或者被挪
	// 作它用，所以在继续执行前需要再次判断一下该缓冲块是否还是指定设备的缓冲块。
	bh = start_buffer;                      		// bf指向缓冲区开始处。
	for (i = 0 ; i < NR_BUFFERS ; i++, bh++) {
		if (bh->b_dev != dev)           			// 不是设备dev的缓冲块则继续。
			continue;
		wait_on_buffer(bh);             			// 等待缓冲区解锁（如果已上锁的话）。
		if (bh->b_dev == dev && bh->b_dirt)
			ll_rw_block(WRITE, bh);
	}
	// 再将i节点数据写入高速缓冲。让i节点表inode_table中的inode与缓冲中的信息同步。
	sync_inodes();
	// 然后在高速缓冲中的数据更新之后，再把它们与设备中的数据同步。这里采用两遍同步操作是为了提高内核执行效率。第一遍缓
	// 冲区同步操作可以让内核中许多“脏块”变干净，使得i节点的同步操作能够高效执行。本次缓冲区同步操作则把那些由于i节点
	// 同步操作而又变脏的缓冲块与设备中数据同步。
	bh = start_buffer;
	for (i = 0 ; i < NR_BUFFERS ; i++, bh++) {
		if (bh->b_dev != dev)
			continue;
		wait_on_buffer(bh);
		if (bh->b_dev == dev && bh->b_dirt)
			ll_rw_block(WRITE, bh);
	}
	return 0;
}

// 使指定设备在高速缓冲区中的数据无效。
// 扫描高速缓冲中所有的缓冲块。对指定设备的缓冲块复位其有效（更新）标志和修改标志。
void invalidate_buffers(int dev)
{
	int i;
	struct buffer_head * bh;

	bh = start_buffer;
	for (i = 0 ; i < NR_BUFFERS ; i++, bh++) {
		if (bh->b_dev != dev)           // 如果不是指定设备的缓冲块，则继续扫描下一块。
			continue;
		wait_on_buffer(bh);             // 等待该缓冲区解锁（如果已被上锁）。
		// 由于进程执行过睡眠等待，所以需要再判断一下缓冲区是否是指定设备的。
		if (bh->b_dev == dev)
			bh->b_uptodate = bh->b_dirt = 0;
	}
}

/*
 * This routine checks whether a floppy has been changed, and
 * invalidates all buffer-cache-entries in that case. This
 * is a relatively slow routine, so we have to try to minimize using
 * it. Thus it is called only upon a 'mount' or 'open'. This
 * is the best way of combining speed and utility, I think.
 * People changing diskettes in the middle of an operation deserve
 * to loose :-)
 *
 * NOTE! Although currently this is only for floppies, the idea is
 * that any additional removable block-device will use this routine,
 * and that mount/open needn't know that floppies/whatever are
 * special.
 */
/*
 * 该子程序检查一个软盘是否已被更换，如果已经更换就使高速缓冲中与该软驱对应的所有缓冲区无效。
 * 该子程序相对来说较慢，所以我们要尽量少使用它。
 * 所以仅在执行'mount'或'open'时才调用它。我想这是将程度与实用性相结合的最好方法。若在操作过程中更换软盘，
 * 就会导致数据的丢失。这是咎由自取。
 *
 * 注意！尽管目前该子程序仅用于软盘，以后任何可移动介质的块设备都有将使用该程序，mount/open操作不需要知道
 * 是软盘还是其他什么特殊介质。
 */
// 检查磁盘是否更换，如果已更换就使用对应调整缓冲区无效。
void check_disk_change(int dev)
{
	int i;

	// 首先检测一下是不是软盘设备。因为现在仅支持软盘可移动介质。如果不是则退出。然后测试软盘是否已更换，如果没有
	// 则退出。floppy_chang()在blk_drv/floppy.c。
	if (MAJOR(dev) != 2)
		return;
	if (!floppy_change(dev & 0x03))
		return;
	// 软盘已更换，所以释放对应设备的i节点位图和逻辑位图所占的高速缓冲区；并使该设备的i节点和数据块信息所占据的高速缓冲
	// 块无效。
	for (i = 0 ; i < NR_SUPER ; i++)
		if (super_block[i].s_dev == dev)
			put_super(super_block[i].s_dev);
	invalidate_inodes(dev);         // 释放设备dev在内存i节点表中的所有i节点
	invalidate_buffers(dev);        //
}

// 下面两行代码是hash(散列)函数定义和hash表项的计算宏.
// hash表的主要作用是减少查找比较元素所花费的时间.通过在元素的存储位置与关键字之间建立一个对应关系(hash函数),我们就可以直接通过函数计算立刻
// 查询到指定的元素.建立hash函数的指导条件主要是尽量确保散列到任何数组项的概率基本相等.建立函数的方法有多种,这里Linux0.12主要采用了关键字除
// 余数法.因为我们寻找的缓冲块有两个条件,即设备号dev和缓冲块号block,因此设计的hash函数肯定需要包含这两个关键值.这里两个关键字的异或操作只是
// 计算关键值的一种方法.再对关键值进行MOD运算就可以保证函数计算得到的值都处于函数数组项范围内.
#define _hashfn(dev, block) (((unsigned)(dev ^ block)) % NR_HASH)
#define hash(dev, block) hash_table[_hashfn(dev, block)]

// 从hash队列和空闲缓冲队列中移走缓冲块.
// hash队列是双向链表结构,空闲缓冲块队列是双向循环链表结构.
static inline void remove_from_queues(struct buffer_head * bh)
{
	/* remove from hash-queue */
	/* 从hash队列中移除缓冲块 */
	if (bh->b_next)
		bh->b_next->b_prev = bh->b_prev;
	if (bh->b_prev)
		bh->b_prev->b_next = bh->b_next;
	// 如果该缓冲我是该队列的头一个块,则让hash表的对应项指向本队列中的下一个缓冲区.
	if (hash(bh->b_dev, bh->b_blocknr) == bh)
		hash(bh->b_dev, bh->b_blocknr) = bh->b_next;
	/* remove from free list */
	/* 从空闲缓冲块表中移除缓冲块 */
	if (!(bh->b_prev_free) || !(bh->b_next_free))
		panic("Free block list corrupted");
	bh->b_prev_free->b_next_free = bh->b_next_free;
	bh->b_next_free->b_prev_free = bh->b_prev_free;
	// 如果空闲链表头指向本缓冲区,则让其指向下一缓冲区.
	if (free_list == bh)
		free_list = bh->b_next_free;
}

// 将缓冲块插入空闲链表尾部,同时放入hash队列中.
static inline void insert_into_queues(struct buffer_head * bh)
{
	/* put at end of free list */
	/* 放在空闲链表末尾处 */
	bh->b_next_free = free_list;
	bh->b_prev_free = free_list->b_prev_free;
	free_list->b_prev_free->b_next_free = bh;
	free_list->b_prev_free = bh;
	/* put the buffer in new hash-queue if it has a device */
	/* 如果该缓冲块对应一个设备,则将其插入新hash队列中 */
	bh->b_prev = NULL;
	bh->b_next = NULL;
	if (!bh->b_dev)
		return;
	bh->b_next = hash(bh->b_dev, bh->b_blocknr);
	hash(bh->b_dev, bh->b_blocknr) = bh;
	// 请注意当hash表某项第1次插入项时,hash()计算值肯定为NULL,因此此时hash(bh->b_dev,bh->b_blocknr)得到的bh->b_next肯定是NULL,
	// 所以bh->b_next->b_prev = bh应该在bh->b_next不为NULL时才能给b_pev赋bh值.即bh->b_next->b_prev = bh前应该增加判断"
	// if(bh->b_next)".该错误到0.96版后才被纠正.
	if(bh->b_next)
		bh->b_next->b_prev = bh;			// 此句前应添加"if(bh->b_next)"判断.
}

// 利用hash表在高速缓冲中寻找给定设备和指定块号的缓冲区块.
// 如果找到则返回缓冲区块的指针,否则返回NULL.
static struct buffer_head * find_buffer(int dev, int block)
{
	struct buffer_head * tmp;

	// 搜索hash表,寻找指定设备与和块号的缓冲块.
	for (tmp = hash(dev, block) ; tmp != NULL ; tmp = tmp->b_next)
		if (tmp->b_dev == dev && tmp->b_blocknr == block)
			return tmp;
	return NULL;
}

/*
 * Why like this, I hear you say... The reason is race-conditions.
 * As we don't lock buffers (unless we are readint them, that is),
 * something might happen to it while we sleep (ie a read-error
 * will force it bad). This shouldn't really happen currently, but
 * the code is ready.
 */
/*
 * 代码为什么会是这样子的?我听见你问......原因是竞争条件.由于我们没有对缓冲块上锁(除非我们正在读取它们的数据),那么当我们(进程)睡眠时缓冲块可能
 * 发生一些问题(例如一个读错误将导致该缓冲块出错).目前这种情况实际上是不会发生的,但处理的代码已经准备好了.
 */
// 利用hash表在高速缓冲区中寻找指定的缓冲块.若找到则对该缓冲块上锁并返回块头指针.
struct buffer_head * get_hash_table(int dev, int block)
{
	struct buffer_head * bh;

	for (;;) {
		// 在高速缓冲中寻找给定设备和指定块的缓冲区块,如果没有找到则返回NULL,退出.
		if (!(bh = find_buffer(dev, block)))
			return NULL;
		// 对该缓冲块增加引用计数,并等待该缓冲块解锁(如果已被上锁).由于经过了睡眠状态,因此有必要再验证该缓冲块的正确性,并返回缓冲块头指针.
		bh->b_count++;
		wait_on_buffer(bh);
		if (bh->b_dev == dev && bh->b_blocknr == block)
			return bh;
		// 如果在睡眠时该缓冲块所属的设备号或块号发生的改变,则撤消对它的用计数.重新寻找.
		bh->b_count--;
	}
}

/*
 * Ok, this is getblk, and it isn't very clear, again to hinder
 * race-conditions. Most of the code is seldom used, (ie repeating),
 * so it should be much more efficient than it looks.
 *
 * The algoritm is changed: hopefully better, and an elusive bug removed.
 */
/*
 * OK,下面是getbl函数,该函数的逻辑并不是很清晰,同样也是因为要考虑竞争条件问题.其中大部分代码很少用到(例如重复操作语句),
 * 因此它应该比看上去的样子有效得多.
 *
 * 算法已经作了改变:希望能更好,而且一个难以琢磨的错误已经去除.
 */
// 下面宏用于同时判断缓冲区的修改标志和锁定标志,并且定义修改标志的权重要比锁定标志大.
#define BADNESS(bh) (((bh)->b_dirt << 1) + (bh)->b_lock)
// 取高速缓冲中指定的缓冲块.
// 检查指定(设备号和块号)的缓冲区是否已经在高速缓冲中.如果指定块已经在高速缓冲中,则返回对应缓冲区头指针退出;如果不在,就需要在高速中
// 中设置一个对应设备号和块号的新项.返回相应缓冲区头指针.
struct buffer_head * getblk(int dev, int block)
{
	struct buffer_head * tmp, * bh;

repeat:
	if (bh = get_hash_table(dev, block))
		return bh;
	// 扫描空闲数据块链表,寻找空闲缓冲区.
	// 首先让tmp指向空闲链表的第一个空闲缓冲区头.
	tmp = free_list;
	do {
		// 如果该缓冲区正在被使用(引用计数不等于0),则继续扫描下一项.对于b_count=0的块,即高速缓冲中当前没有引用的块不一定就是干净的(b_dirt=0)
		// 或没有锁定的(b_lock=0).因此,我们还是需要继续下面的判断和选择.例如当一个任务改写过一块内容后就释放了,于是该块b_count=0,但b_lock不等于0;
		// 当一个任务执行breada()预读几个块时,只要ll_rw_block()命令发出后,它就会递减b_count;但此时实际上硬盘访问操作可能还在进行,因此此时
		// b_lock=1,但b_count=0.
		if (tmp->b_count)
			continue;
		// 如果缓冲头指针bh为空,或者tmp所指缓冲头的标志(修改,锁定)权重小于bh头标志的权重,则让bh指向tmp缓冲块头.如果该tmp缓冲块头表明缓冲
		// 块既没有修改也没有锁定标志置位,则说明已为指定设备上的块取得对应的高速缓冲块,则退出循环.否则我们就继续执行本循环,看看能否找到一个BADNESS()
		// 最小的缓冲块.
		if (!bh || BADNESS(tmp) < BADNESS(bh)) {
			bh = tmp;
			if (!BADNESS(tmp))
				break;
		}
	/* and repeat until we find something good */	/* 重复操作直到找到适合的缓冲块 */
	} while ((tmp = tmp->b_next_free) != free_list);
	// 如果循环检查发现所有缓冲块都正在被使用(所有缓冲块的状况引用计数者>0)中,则睡眠等待有空闲缓冲区可用.当有空闲缓冲块可用时本各会被明确地唤醒.然后
	// 我们就跳转到函数开始处重新查找空闲缓冲块.
	if (!bh) {
		sleep_on(&buffer_wait);
		goto repeat;
	}
	// 执行到这里,说明我们已经找到了一个比较适合的空闲缓冲块了.于是先等待该缓冲区解锁(如果已被上锁的话).如果在我们睡眠阶段该缓冲区又被其他任务使用的话,
	// 只好重复上述寻找过程.
	wait_on_buffer(bh);
	if (bh->b_count)	// 又被占用??
		goto repeat;
	// 如果该缓冲区已被修改,则将数据写盘,并再次等待缓冲区解锁.同样地,若该缓冲区又被其他任务使用的话,只好再重复上述寻找过程.
	while (bh->b_dirt) {
		sync_dev(bh->b_dev);
		wait_on_buffer(bh);
		if (bh->b_count)	// 又被占用??
			goto repeat;
	}
	/* NOTE!! While we slept waiting for this block, somebody else might */
	/* already have added "this" block to the cache. check it */
	/* 注意!!当进程为了等待该缓冲块而睡眠时,其他进程可能已经将该缓冲块加入进高速缓冲中,所以我们也要对此进行检查. */
	// 在高速缓冲hash表中检查指定设备和块的缓冲块是否乘我们睡眠之即已经被加入进去.如果是的话就再次重复上述寻找过程.
	if (find_buffer(dev, block))
		goto repeat;
	/* OK, FINALLY we know that this buffer is the only one of it's kind, */
	/* and that it's unused (b_count=0), unlocked (b_lock=0), and clean */
	/* OK,最终我们知道该缓冲块是指定参数的唯一一块,而且目前还没有被占用 */
	/* (b_count=0),也未被上锁(b_lock=0),并且是干净的(未被修改的) */
	// 于是让我们占用此缓冲块.置引用计数为1,复位修改标志和有效(更新)标志.
	bh->b_count = 1;
	bh->b_dirt = 0;
	bh->b_uptodate = 0;
	// 从hash队列和空闲块链表中移出该缓冲头,让该缓冲区用于指定设备和其上的指定块.然后根据此新设备号和块号重新插入空闲链表和hash队列新位置处.并最终返回缓冲
	// 头指针.
	remove_from_queues(bh);
	bh->b_dev = dev;
	bh->b_blocknr = block;
	insert_into_queues(bh);
	return bh;
}

// 释放指定缓冲块.
// 等待该缓冲块解锁.然后引用计数递减1,并明确地唤醒等待空闲缓冲块的进程.
void brelse(struct buffer_head * buf)
{
	if (!buf)						// 如果缓冲头指针无效则返回.
		return;
	wait_on_buffer(buf);
	if (!(buf->b_count--))
		panic("Trying to free free buffer");
	wake_up(&buffer_wait);
}

/*
 * bread() reads a specified block and returns the buffer that contains
 * it. It returns NULL if the block was unreadable.
 */
/*
 * 从设备上读取指定的数据块并返回含有数据的缓冲区.如果指定的块不存在则返回NULL.
 */
// 从设备上读取数据块.
// 该函数根据指定的设备号dev和数据块号block,首先在高速缓冲区中申请一块缓冲块.如果该缓冲块中经包含有有效的数据就直接返回该缓冲块指针,否则就从设备中
// 读取指定的数据块到该缓冲块中并返回缓冲块指针.
struct buffer_head * bread(int dev, int block)
{
	struct buffer_head * bh;

	// 从高速缓冲区中申请一块缓冲块.如果返回值是NULL,则表示内核出错,停机.否则我们判断其中是否已有可用数据.
	if (!(bh = getblk(dev, block)))
		panic("bread: getblk returned NULL\n");
	// 如果该缓冲块中数据是有效的(已更新的)可以直接使用,则返回.
	if (bh->b_uptodate)
		return bh;
	// 否则我们就调用底层块设备读写ll_rw_block()函数,产生读设备块请求.然后等待指定数据块被读入,并等待缓冲区解锁.在睡眠醒来之后,如果该缓冲区已更新,则返回
	// 缓冲区头指针,退出.否则表明读设备操作失败,于是释放该缓冲区,返回NULL,退出.
	ll_rw_block(READ, bh);
	wait_on_buffer(bh);
	if (bh->b_uptodate)
		return bh;
	brelse(bh);
	return NULL;
}

// 复制内存块.
// 从from地址复制一块(1024字节)数据到to位置(重写的是先将edi,esi寄存器入栈保存起来，拷贝完毕后再还原，避免edi,esi寄存器的污染)
#define COPYBLK(from, to) \
__asm__("cld\n\t" \
		"pushl %%edi\n\t" \
		"pushl %%esi\n\t" \
		"rep\n\t" \
		"movsl\n\t" \
		"popl %%esi\n\t" \
		"popl %%edi\n\t" \
	::"c" (BLOCK_SIZE/4),"S" (from),"D" (to) \
	:)

//#define COPYBLK(from,to) \
	__asm__("cld\n\t" \
			"rep\n\t" \
			"movsl\n\t" \
			::"c" (BLOCK_SIZE/4),"S" (from),"D" (to) \
			:)

/*
 * bread_page reads four buffers into memory at the desired address. It's
 * a function of its own, as there is some speed to be got by reading them
 * all at the same time, not waiting for one to be read, and then another
 * etc.
 */
/*
 * bread_page一次读四个缓冲块数据读到内存指定的地址处.它是一个完整的函数,因为同时读取四块可以获得速度上的好处,不用等着读一块,再读一块了.
 */
// 读设备上一个页面(4个缓冲块)的内容到指定内存地址处.
// 参数address是保存页面数据的地址;dev是指定的设备号;b[4]是含有4个设备数据块号的数组.
// 该函数仅用于mm/memory.c文件的do_no_page()函数中.
void bread_page(unsigned long address, int dev, int b[4])
{
	struct buffer_head * bh[4];
	int i;

	// 该函数循环执行4次,根据放在数组b[]中的4个块号从设备dev中读取一页内容放到指定内存位置address处.对于参数b[i]给出的
	// 有效块号,函数首先从高速缓冲中取指定设备和块号的的缓冲块.如果缓冲块中数据无效(未更新)则产生读设备请求从设备上读取相
	// 应数据块.对于b[i]无效的块号则不用处理它了.因此本函数其实可以根据指定的b[]中的块号随意读取1-4个数据块.
	for (i = 0 ; i < 4 ; i++)
		if (b[i]) {
			// 先给该逻辑块号申请一个缓存块
			if (bh[i] = getblk(dev, b[i]))
				// 如果该缓冲块没有更新,则从块设备中读取出来
				if (!bh[i]->b_uptodate)
					ll_rw_block(READ, bh[i]);
		} else
			bh[i] = NULL;
	// 随后将4个缓冲块上的内容顺序复制到指定地址处.在进行复制(使用)缓冲块之前我们先要睡眠等待缓冲块解锁(若被上锁的话).另外,
	// 因为可能睡眠过了,所以我们还需要在复制之前再检查一下缓冲块中的数据是否是有效的.复制完后我们还需要释放缓冲块.
	for (i = 0 ; i < 4 ; i++, address += BLOCK_SIZE)
		if (bh[i]) {
			wait_on_buffer(bh[i]);						// 等待缓冲块解锁(若被上锁的话).
			if (bh[i]->b_uptodate)						// 若缓冲块中数据有效的话则复制.
				COPYBLK((unsigned long) bh[i]->b_data, address);
			brelse(bh[i]);								// 释放该缓冲区.
		}
}

/*
 * Ok, breada can be used as bread, but additionally to mark other
 * blocks for reading as well. End the argument list with a negative
 * number.
 */
/*
 * OK,breada可以象bread一样使用,但会另外预读一些块.该函数参数列表需要使用一个负数来表明参数列表的结束.
 */
// 从指定设备读取指定的一些块.
// 函数参数个数可变,是一系列指定的块号.成功时返回第1块的缓冲块头指针,否则返回NULL.
struct buffer_head * breada(int dev, int first, ...)
{
	va_list args;
	struct buffer_head * bh, *tmp;

	// 首先取可变参数表中第1个参数(块号).接着从调整缓冲区中取指定设备和块号的缓冲块.如果该缓冲块数据无效(更新标志未置位),则
	// 发出读设备数据块请求.
	va_start(args, first);
	if (!(bh = getblk(dev, first)))
		panic("bread: getblk returned NULL\n");
	if (!bh->b_uptodate)
		ll_rw_block(READ, bh);
	// 然后顺序取可变参数表中其他预读块号,并作与上面同样处理,但不引用.注意,336行上有一个bug.其中的bh应该是tmp.这个bug直到在0.96
	// 版的内核代码中才被纠正过来.另外,因为这里是预读随后的数据块,只需读进调整缓冲区但并不马上就使用,所以第337行语句需要将其引用计数
	// 递减释放掉该块(因为getblk()函数会增加缓冲块引用计数值).
	while ((first = va_arg(args, int)) >= 0) {
		tmp = getblk(dev, first);
		if (tmp) {
			if (!tmp->b_uptodate)
				ll_rw_block(READA, tmp);
			tmp->b_count--;					// 暂时释放掉该预读块.
		}
	}
	// 此时可变参数表中所有参数处理完毕.于是等待第1个缓冲区解锁(如果已被上锁).在等待退出之后如果缓冲区中数据仍然有效,则返回缓冲区头指针
	// 退出.否则释放该缓冲区返回NULL,退出.
	va_end(args);
	wait_on_buffer(bh);
	if (bh->b_uptodate)
		return bh;
	brelse(bh);
	return (NULL);
}

// 缓冲区初始化函数
// 参数buffer_end是缓冲区内存末端.对于具有16M内存的系统,缓冲区末端被设置为4MB.对于有8MB内存的系统,缓冲区末端被设置2MB.该函数从缓冲区开始位置
// start_buffer处和缓冲区末端buffer_end处分别同时设置(初始化)缓冲块头结构和对应的数据块.直到缓冲区中所有内存被分配完毕.
void buffer_init(long buffer_end)
{
	struct buffer_head * h = start_buffer;
	void * b;
	int i;

	// 首先根据参数提供的缓冲区高端位置确定实际缓冲区高端位置b.如果缓冲区高端等于1MB,则因为从640KB-1MB被显示内存和BIOS占用,所以实际可用缓冲区内存
	// 高端位置应该是640KB.否则缓冲区内存高端一定大于1MB.
	if (buffer_end == 1 << 20)
		b = (void *) (640 * 1024);
	else
		b = (void *) buffer_end;
	// 这段代码用于初始化缓冲区,建立空闲缓冲块循环链表,并获取系统中缓冲块数目.操作的过程是从缓冲区高端开始划分1KB大小的缓冲块,与此同时在缓冲区低端建立
	// 描述该缓冲块的结构buffer_head,并将这些buffer_head组成双向链表.
	// h是指向缓冲头结构的指针,而h+1是指向内存地址连续的下一个缓冲头地址,也可以说是指向h缓冲有头的末端外.为了保证有足够长度的内存来存储一个缓冲头结构,
	// 需要b所指向的内存块地址>=h缓冲头的末端,即要求>=h+1.
	while ( (b -= BLOCK_SIZE) >= ((void *) (h + 1)) ) {
		h->b_dev = 0;								// 使用该缓冲块的设备号.
		h->b_dirt = 0;								// 脏标志,即缓冲块修改标志.
		h->b_count = 0;								// 缓冲块引用计数.
		h->b_lock = 0;								// 缓冲块锁定标志.
		h->b_uptodate = 0;							// 缓冲块更新标志(或称数据有效标志).
		h->b_wait = NULL;							// 指向等待该缓冲块解锁的进程.
		h->b_next = NULL;							// 指向具有相同hash值的下一个缓冲头.
		h->b_prev = NULL;							// 指向具有相同hash值的前一个缓冲头.
		h->b_data = (char *) b;						// 指向对应缓冲块数据块(1024字节).
		h->b_prev_free = h - 1;						// 指向链表中前一项.
		h->b_next_free = h + 1;						// 指向链表中下一项.
		h++;										// h指向下一新缓冲头位置.
		NR_BUFFERS++;								// 缓冲区块数累加.
		if (b == (void *) 0x100000)					// 若b递减到等于1MB,则跳过384KB
			b = (void *) 0xA0000;					// 让b指向地址0xA0000(640KB)处.
	}
	h--;											// 让h指向最后一个有效缓冲块头.
	free_list = start_buffer;						// 让空闲链表头指向头一个缓冲块.
	free_list->b_prev_free = h;     				// 链表头的b_prev_free指向前一项（即最后一项）。
	h->b_next_free = free_list;     				// h的下一项指针指向第一项，形成一个环链。
	// 最后初始化hash表(哈希表、散列表),置表中所有指针为NULL。
	for (i = 0; i < NR_HASH; i++)
		hash_table[i] = NULL;
}
