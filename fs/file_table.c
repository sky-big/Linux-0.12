/*
 *  linux/fs/file_table.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/fs.h>		// 文件系统头文件,定义文件表结构(file_,buffer_head,m_inode等).

struct file file_table[NR_FILE];	// 文件表数组(64项).
