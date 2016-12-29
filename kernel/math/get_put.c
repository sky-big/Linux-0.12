/*
 * linux/kernel/math/get_put.c
 *
 * (C) 1991 Linus Torvalds
 */

/*
 * This file handles all accesses to user memory: getting and putting
 * ints/reals/BCD etc. This is the only part that concerns itself with
 * other than temporary real format. All other cals are strictly temp_real.
 */
/*
 * 本程序处理所有对用户内存的访问：获取和存入指令/实数值/BCD数值等。这是涉及临时实数以外其他格式仅有的部分。所有其他运算
 * 全都使用临时实数格式。
 */

#include <linux/math_emu.h>
#include <asm/segment.h>

// 取用户内存中的短实数（单精度实数）。
// 根据浮点指令代码中寻址模式字节中的内容和info结构中当前寄存器中的内容，取得短实数所在有效地址（math/ea.c），然后从用户
// 数据区读取相应实数值。最后把用户短实数转换成临时实数（math/convert.c）。
// 参数：tmp - 转换成临时实数后的指针；info - info结构指针；code - 指令代码。
void get_short_real(temp_real * tmp,
	struct info * info, unsigned short code)
{
	char * addr;
	short_real sr;

	addr = ea(info,code);                           // 计算有效地址。
	sr = get_fs_long((unsigned long *) addr);       // 取用户数据区中的值。
	short_to_temp(&sr,tmp);                         // 转换成临时实数格式。
}

// 取用户内存中的长实数（双精度实数）。
// 首先根据浮点指令代码中寻址模式字节中的内容和info结构中当前寄存器中的内容，取得长实数所在有效地址（math/ec.c），然后从
// 用户数据区读取相应实数值。最后把用户实数值转换成临时实数（math/convert.c）。
// 参数：tmp - 转换成临时实数后的指针；info - info结构指针；code - 指令代码。
void get_long_real(temp_real * tmp,
	struct info * info, unsigned short code)
{
	char * addr;
	long_real lr;

	addr = ea(info,code);                           // 取指令中的有效地址。
	lr.a = get_fs_long((unsigned long *) addr);     // 取长8字节实数。
	lr.b = get_fs_long(1 + (unsigned long *) addr); // 转换成临时实数格式。
	long_to_temp(&lr,tmp);
}

// 取用户内存中的临时实数。
// 首先根据浮点指令代码中寻址模式字节中的内容和info结构中当前寄存器中的内容，取得临时实数所在有效地址（math/ea.c），然后
// 从用户数据区读取相应临时实数值。
// 参数：tmp - 转换成临时实数后的指针；info - info结构指针；code - 指令代码。
void get_temp_real(temp_real * tmp,
	struct info * info, unsigned short code)
{
	char * addr;

	addr = ea(info,code);           // 取指令中的有效地址值。
	tmp->a = get_fs_long((unsigned long *) addr);
	tmp->b = get_fs_long(1 + (unsigned long *) addr);
	tmp->exponent = get_fs_word(4 + (unsigned short *) addr);
}

// 取用户内存中的短整数并转换成临时实数格式。
// 临时整数也用10字节表示。其中低8字节是无符号整数值，高2字节表示指数值和符号位。如果高2字节最高有效位为1,则表示是负数；
// 若最高有效位是0,表示是正数。
// 该函数首先根据浮点指令代码中寻址模式字节中的内容和info结构中当前寄存器中的内容，取得短整数所在有效地址（math/ea.c）
// 然后从用户数据区读取相应整数值，并保存为临时整数格式。最后把临时整数值转换成临时实数（math/convert.c）。
// 参数：tmp - 转换成临时实数后的指针；info - info结构指针；code - 指令代码。
void get_short_int(temp_real * tmp,
	struct info * info, unsigned short code)
{
	char * addr;
	temp_int ti;

	addr = ea(info,code);           // 取指令中的有效地址值。
	ti.a = (signed short) get_fs_word((unsigned short *) addr);
	ti.b = 0;
	if (ti.sign = (ti.a < 0))       // 若是负数，则设置临时整数符号位。
		ti.a = - ti.a;          // 临时整数“尾数”部分为无符号数。
	int_to_real(&ti,tmp);           // 把临时整数转换成临时实数格式。
}

// 取用户内存中的长整数并转换成临时实数格式。
// 首先根据浮点指令代码中寻址模式字节中的内容和info结构中当前寄存器中的内容，取得长整数所在有效地址（math/ea.c），然后从
// 用户数据区读取相应整数值，并保存为临时整数格式。最后把临时整数值转换成临时实数（math/convert.c）。
// 参数：tmp - 转换成临时实数后的指针；info - info结构指针；code - 指令代码。
void get_long_int(temp_real * tmp,
	struct info * info, unsigned short code)
{
	char * addr;
	temp_int ti;

	addr = ea(info,code);           // 取指令中的有效地址值。
	ti.a = get_fs_long((unsigned long *) addr);
	ti.b = 0;
	if (ti.sign = (ti.a < 0))       // 若是负数，则设置临时整数符号位。
		ti.a = - ti.a;          // 临时整数“尾数”部分为无符号数。
	int_to_real(&ti,tmp);           // 把临时整数转换成临时实数格式。
}

// 取用户内存中的64位长整数并转换成临时实数格式。
// 首先根据浮点指令代码中寻址模式字节中的内容和info结构中当前寄存器中的内容，取得64位长整数所有有效地址（math/ea.c），
// 然后从用户数据区读取相应整数值，并保存为临时整数格式。最后再把临时整数值转换成临时实数（math/convert.c）。
// 参数：tmp - 转换成临时实数后的指针；info - info结构指针；code - 指令代码。
void get_longlong_int(temp_real * tmp,
	struct info * info, unsigned short code)
{
	char * addr;
	temp_int ti;

	addr = ea(info,code);                           // 取指令中的有效地址值。
	ti.a = get_fs_long((unsigned long *) addr);     // 取用户64位长整数。
	ti.b = get_fs_long(1 + (unsigned long *) addr);
	if (ti.sign = (ti.b < 0))                       // 若是负数则设置临时整数符号位。
		__asm__("notl %0 ; notl %1\n\t"         // 同时取反加1和进位调整。
			"addl $1,%0 ; adcl $0,%1"
			:"=r" (ti.a),"=r" (ti.b)
			:"0" (ti.a),"1" (ti.b));
	int_to_real(&ti,tmp);                           // 把临时整数转换成临时实数格式。
}

// 将一个64位整数（例如N）乘10。
// 这个宏用于下面BCD码数值转换成临时实数格式过程中。方法是：N<<1 + N<<3。
#define MUL10(low,high) \
__asm__("addl %0,%0 ; adcl %1,%1\n\t" \
"movl %0,%%ecx ; movl %1,%%ebx\n\t" \
"addl %0,%0 ; adcl %1,%1\n\t" \
"addl %0,%0 ; adcl %1,%1\n\t" \
"addl %%ecx,%0 ; adcl %%ebx,%1" \
:"=a" (low),"=d" (high) \
:"0" (low),"1" (high):"cx","bx")

// 64位加法。
// 把32位的无符号数val加到64位数<high,low>中。
#define ADD64(val,low,high) \
__asm__("addl %4,%0 ; adcl $0,%1":"=r" (low),"=r" (high) \
:"0" (low),"1" (high),"r" ((unsigned long) (val)))

// 取用户内存中的BCD码数值并转换成临时实数格式。
// 该函数首先根据浮点指令代码中寻址模式字节中的内容和info结构中当前寄存器中的内容，取得BCD码所在有效地址（math/ea.c），
// 然后从用户数据区读取10字节相应BCD码值（其中1字节用于符号），同时转换成临时整数形式。最后把临时整数值转换成临时实数。
// 参数：tmp - 转换成临时实数后的指针；info - info结构指针；code - 指令代码。
void get_BCD(temp_real * tmp, struct info * info, unsigned short code)
{
	int k;
	char * addr;
	temp_int i;
	unsigned char c;

// 取得BCD码数值所在内存有效地址。然后从最后1个BCD码字节（最高有效位）开始处理。
// 先取得BCD码数值的符号位，并设置临时整数的符号位。然后把9字节的BCD码值转换成临时整数格式，最后把临时整数值转换成临时
// 实数。
	addr = ea(info,code);                   // 取有效地址。
	addr += 9;                              // 指向最后一个（第10个）字节。
	i.sign = 0x80 & get_fs_byte(addr--);    // 取其中符号位。
	i.a = i.b = 0;
	for (k = 0; k < 9; k++) {               // 转换成临时整数格式。
		c = get_fs_byte(addr--);
		MUL10(i.a, i.b);
		ADD64((c>>4), i.a, i.b);
		MUL10(i.a, i.b);
		ADD64((c&0xf), i.a, i.b);
	}
	int_to_real(&i,tmp);                    // 转换成临时实数格式。
}

// 把运算结果以短（单精度）实数格式保存到用户数据区中。
// 该函数首先根据浮点指令代码中寻址模式字节中的内容和info结构中当前寄存器中的内容，取得保存结果的有效地址addr，然后把临时实
// 数格式的结果转换成短实数格式并存储到有效地址addr处。
// 参数：tmp - 临时实数格式结果值；info - info结构指针；code - 指令代码。
void put_short_real(const temp_real * tmp,
	struct info * info, unsigned short code)
{
	char * addr;
	short_real sr;

	addr = ea(info,code);                           // 取有效地址。
	verify_area(addr,4);                            // 为保存结果验证或分配内存。
	temp_to_short(tmp,&sr);                         // 结果转换成短实数格式。
	put_fs_long(sr,(unsigned long *) addr);         // 存储数据到用户内存区。
}

// 把运算结果以长（双精度）实数格式保存到用户数据区中。
// 该函数首先根据浮点指令代码中寻址模式字节中的内容和info结构中当前寄存器中的内容，取得保存结果的有效地址addr，然后把临时
// 实数格式的结果转换成长实数格式，并存储到有效地址addr处。
// 参数：tmp - 临时实数格式结果值；info - info结构指针；code - 指令代码。
void put_long_real(const temp_real * tmp,
	struct info * info, unsigned short code)
{
	char * addr;
	long_real lr;

	addr = ea(info,code);                           // 取有效地址。
	verify_area(addr,8);                            // 为保存结果验证或分配内存。
	temp_to_long(tmp,&lr);                          // 结果转换成长实数格式。
	put_fs_long(lr.a, (unsigned long *) addr);      // 存储数据到用户内存区。
	put_fs_long(lr.b, 1 + (unsigned long *) addr);
}

// 把运算结果以临时实数格式保存到用户数据区中。
// 该函数首先根据浮点指令代码中寻址模式字节中的内容和info结构中当前寄存器中的内容，取得保存结果的有效地址addr，然后把临
// 时实数存储到有效地址addr处。
// 参数：tmp - 临时实数格式结果值；info - info结构指针；code - 指令代码。
void put_temp_real(const temp_real * tmp,
	struct info * info, unsigned short code)
{
	char * addr;

	addr = ea(info,code);                           // 取有效地址。
	verify_area(addr,10);                           // 为保存结果验证或分配内存。
	put_fs_long(tmp->a, (unsigned long *) addr);    // 存储数据到用户内存区。
	put_fs_long(tmp->b, 1 + (unsigned long *) addr);
	put_fs_word(tmp->exponent, 4 + (short *) addr);
}

// 把运算结果以短整数格式保存到用户数据区中。
// 该函数首先根据浮点指令代码中寻址模式字节中的内容和info结构中当前寄存器中的内容，取得保存结果的有效地址addr，然后把临
// 时实数格式的结果转换成临时整数格式。如果是负数则设置整数符号位。最后把整数保存到用户内存中。
// 参数：tmp - 临时实数格式结果值；info - info结构指针；code - 指令代码。
void put_short_int(const temp_real * tmp,
	struct info * info, unsigned short code)
{
	char * addr;
	temp_int ti;

	addr = ea(info,code);           // 取有效地址。
	real_to_int(tmp,&ti);           // 转换成临时整数格式。
	verify_area(addr,2);            // 验证或分配存储内存。
	if (ti.sign)                    // 若有符号位，则取负数值。
		ti.a = -ti.a;
	put_fs_word(ti.a,(short *) addr);       // 存储到用户数据区中。
}

// 把运算结果以长整数格式保存到用户数据区中。
// 该函数首先根据浮点指令代码中寻址模式字节中的内容和info结构中当前寄存器中的内容，取得保存结果的有效地址addr，然后把临时
// 实数格式的结果转换成临时整数格式。如果是负数则设置整数符号位。最后把整数保存到用户内存中。
// 参数：tmp - 临时实数格式结果值；info - info结构指针；code - 指令代码。
void put_long_int(const temp_real * tmp,
	struct info * info, unsigned short code)
{
	char * addr;
	temp_int ti;

	addr = ea(info,code);                           // 取有效地址值。
	real_to_int(tmp,&ti);                           // 转换成临时整数格式。
	verify_area(addr,4);                            // 验证或分配存储内存。
	if (ti.sign)                                    // 若有符号位，则取负数值。
		ti.a = -ti.a;
	put_fs_long(ti.a,(unsigned long *) addr);       // 存储到用户数据区中。
}

// 把运算结果以64位整数格式保存到用户数据区中。
// 该函数首先根据浮点指令代码中寻址模式字节中的内容和info结构中当前寄存器中的内容，取得保存结果的有效地址addr，然后把临时
// 实数格式的结果转换成临时整数格式。如果是负数则设置整数符号位。最后把整数保存到用户内存中。
// 参数：tmp - 临时实数格式结果值；info - info结构指针；code - 指令代码。
void put_longlong_int(const temp_real * tmp,
	struct info * info, unsigned short code)
{
	char * addr;
	temp_int ti;

	addr = ea(info,code);           // 取有效地址。
	real_to_int(tmp,&ti);           // 转换成临时整数格式。
	verify_area(addr,8);            // 验证存储区域。
	if (ti.sign)                    // 若是负数，则取反加1。
		__asm__("notl %0 ; notl %1\n\t"
			"addl $1,%0 ; adcl $0,%1"
			:"=r" (ti.a),"=r" (ti.b)
			:"0" (ti.a),"1" (ti.b));
	put_fs_long(ti.a,(unsigned long *) addr);       // 存储到用户数据区中。
	put_fs_long(ti.b,1 + (unsigned long *) addr);
}

// 无符号数<high, low>除以10，余数放在rem中。
#define DIV10(low,high,rem) \
__asm__("divl %6 ; xchgl %1,%2 ; divl %6" \
	:"=d" (rem),"=a" (low),"=b" (high) \
	:"0" (0),"1" (high),"2" (low),"c" (10))

// 把运算结果以BCD码格式保存到用户数据区中。
// 该函数首先根据浮点指令代码中寻址模式字节中的内容和info结构中当前寄存器中的内容，取得保存结果的有效地址addr，并验证保
// 存10字节BCD码的用户空间。然后把临时实数格式的结果转换成BCD码格式的数据并保存到用户内存中。如果是负数则设置最高存储字
// 节的最高有效位。
// 参数：tmp - 临时实数格式结果值；info - info结构指针；code - 指令代码。
void put_BCD(const temp_real * tmp,struct info * info, unsigned short code)
{
	int k,rem;
	char * addr;
	temp_int i;
	unsigned char c;

	addr = ea(info,code);                   // 取有效地址。
	verify_area(addr,10);                   // 验证存储空间容量。
	real_to_int(tmp,&i);                    // 转换成临时整数格式。
	if (i.sign)                             // 若是负数，则设置符号字节最高有效位。
		put_fs_byte(0x80, addr+9);
	else                                    // 否则符号字节设置为0。
		put_fs_byte(0, addr+9);
	for (k = 0; k < 9; k++) {               // 临时整数转换成BCD码并保存。
		DIV10(i.a,i.b,rem);
		c = rem;
		DIV10(i.a,i.b,rem);
		c += rem<<4;
		put_fs_byte(c,addr++);
	}
}