/*
 * linux/kernel/math/compare.c
 *
 * (C) 1991 Linus Torvalds
 */

/*
 * temporary real comparison routines
 */
/*
 * 累加器中临时实数比较子程序。
 */

#include <linux/math_emu.h>

// 复位状态字中的C3、C2、C1和C0条件位。
#define clear_Cx() (I387.swd &= ~0x4500)

// 对临时实数a进行规格化处理。即表示成指数、有效数形式。
// 例如：102.345表示成1.02345 X 10^2。0.0001234表示成1.234 X 10^-1。当然，函数中是二进制表示。
static void normalize(temp_real * a)
{
	int i = a->exponent & 0x7fff;           // 取指数值（略去符号位）。
	int sign = a->exponent & 0x8000;        // 取符号位。

// 如果临时实数a的64位有效数（尾数）为0，那么说明a等于0。于是清a的指数，返回。
	if (!(a->a || a->b)) {
		a->exponent = 0;
		return;
	}
// 如果a的尾数最左端有0值位，那么将尾数左移，同时调整指数值（递减）。直到尾数的b字段最高有效位MSB是1位置（此时b表现为负值）
// 最后再添加符号位。
	while (i && a->b >= 0) {
		i--;
		__asm__("addl %0,%0 ; adcl %1,%1"
			:"=r" (a->a),"=r" (a->b)
			:"0" (a->a),"1" (a->b));
	}
	a->exponent = i | sign;
}

// 仿真浮点指令FTST。
// 即栈定累加器ST(0)与0比较，并根据比较结果设置条件位。若ST > 0.0，则C3，C2，C0分别为000；若ST < 0.0，则条件位为001；若
// ST == 0.0，则条件位是100；若不可比较，则条件位为111。
void ftst(const temp_real * a)
{
	temp_real b;

// 首先清状态字中条件标志位，并对比较值b（ST）进行规格化处理。若b不等于零并且设置了符号位（是负数），则设置条件位C0。否则设置
// 条件位C3。
	clear_Cx();
	b = *a;
	normalize(&b);
	if (b.a || b.b || b.exponent) {
		if (b.exponent < 0)
			set_C0();
	} else
		set_C3();
}

// 仿真浮点指令FCOM。
// 比较两个参数src1、src2。并根据比较结果设置条件位。若src1 > src2，则C3，C2，C0分别为000；若src1 < src2，则条件位为
// 001；若两者相等，则条件位是100。
void fcom(const temp_real * src1, const temp_real * src2)
{
	temp_real a;

	a = *src1;
	a.exponent ^= 0x8000;           // 符号位取反。
	fadd(&a,src2,&a);               // 两者相加（即相减）。
	ftst(&a);                       // 测试结果并设置条件位。
}

// 仿真浮点指令FUCOM（无次序比较）。
// 用于操作数之一是NaN的比较。
void fucom(const temp_real * src1, const temp_real * src2)
{
	fcom(src1,src2);
}
