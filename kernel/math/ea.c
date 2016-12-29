/*
 * linux/kernel/math/ea.c
 *
 * (C) 1991 Linus Torvalds
 */

/*
 * Calculate the effective address.
 */
/*
 * 计算有效地址。
 */

#include <stddef.h>     // 标准定义头文件。本程序使用了其中的offsetof()定义。

#include <linux/math_emu.h>
#include <asm/segment.h>

// info结构中各个寄存器在结构中的偏移位置。offsetof()用于求指定字段在结构中的偏移位置。参见include/stddef.h文件。
static int __regoffset[] = {
	offsetof(struct info,___eax),
	offsetof(struct info,___ecx),
	offsetof(struct info,___edx),
	offsetof(struct info,___ebx),
	offsetof(struct info,___esp),
	offsetof(struct info,___ebp),
	offsetof(struct info,___esi),
	offsetof(struct info,___edi)
};

// 取info结构中指定位置处寄存器内容。
#define REG(x) (*(long *)(__regoffset[(x)]+(char *) info))

// 求2字节寻址模式中第2操作数指示字节SIB（Scale, Index, Base）的值。
static char * sib(struct info * info, int mod)
{
	unsigned char ss,index,base;
	long offset = 0;

// 首先从用户代码段中取得SIB字节，然后取出各个字段位值。
	base = get_fs_byte((char *) EIP);
	EIP++;
	ss = base >> 6;                 // 比例因子大小ss。
	index = (base >> 3) & 7;        // 索引值索引代号index。
	base &= 7;                      // 基地址代号base。
// 如果索引代号为0b100，表示无索引偏移值。否则索引偏移值offset=对应寄存器内容×比例因子。
	if (index == 4)
		offset = 0;
	else
		offset = REG(index);
	offset <<= ss;
// 如果上一MODRM字节中的MOD不为零，或者Base不等于0b101，则表示有偏移值在base指定的寄存器中。因此偏移offset需要再加上base
// 对应寄存器中的内容。
	if (mod || base != 5)
		offset += REG(base);
// 如果MOD=1，则表示偏移值为1字节。否则，若MOD=2，或者base=0b101，则偏移值为4字节。
	if (mod == 1) {
		offset += (signed char) get_fs_byte((char *) EIP);
		EIP++;
	} else if (mod == 2 || base == 5) {
		offset += (signed) get_fs_long((unsigned long *) EIP);
		EIP += 4;
	}
// 最后保存并返回偏移值。
	I387.foo = offset;
	I387.fos = 0x17;
	return (char *) offset;
}

// 根据指令中寻址模式字节计算有效地址值。
char * ea(struct info * info, unsigned short code)
{
	unsigned char mod,rm;
	long * tmp = &EAX;
	int offset = 0;

// 首先取代码中的MOD字段和R/M字段值。如果MOD=0b11，表示是单字节指令，没有偏移字段。如果R/M字段=0b100，并且MOD不为0b11，
// 表示是2字节地址模式寻址，因此调用sib()求出偏移值并返回即可。
	mod = (code >> 6) & 3;          // MOD字段。
	rm = code & 7;                  // R/M字段。
	if (rm == 4 && mod != 3)
		return sib(info,mod);
// 如果R/M字段为0b101，并且MOD为0，表示是单字节地址模式编码且后随32字节偏移值。于是取出用户代码中4字节偏移值，保存并返回
// 之。
	if (rm == 5 && !mod) {
		offset = get_fs_long((unsigned long *) EIP);
		EIP += 4;
		I387.foo = offset;
		I387.fos = 0x17;
		return (char *) offset;
	}
// 对于其余情况，则根据MOD进行处理。首先取出R/M代码对应寄存器内容的值作为指针tmp。对于MOD=0，无偏移值。对于MOD=1，代码后
// 随1字节偏移值。对于MOD=2，代码后有4字节偏移值。最后保存并返回有效地址值。
	tmp = & REG(rm);
	switch (mod) {
		case 0: offset = 0; break;
		case 1:
			offset = (signed char) get_fs_byte((char *) EIP);
			EIP++;
			break;
		case 2:
			offset = (signed) get_fs_long((unsigned long *) EIP);
			EIP += 4;
			break;
		case 3:
			math_abort(info,1<<(SIGILL-1));
	}
	I387.foo = offset;
	I387.fos = 0x17;
	return offset + (char *) *tmp;
}
