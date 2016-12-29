/*
 * linux/kernel/math/add.c
 *
 * (C) 1991 Linus Torvalds
 */

/*
 * temporary real addition routine.
 *
 * NOTE! These aren't exact: they are only 62 bits wide, and don't do
 * correct rounding. Fast hack. The reason is that we shift right the
 * values by two, in order not to have overflow (1 bit), and to be able
 * to move the sign into the mantissa (1 bit). Much simpler algorithms,
 * and 62 bits (61 really - no rounding) accuracy is usually enough. The
 * only time you should notice anything weird is when adding 64-bit
 * integers together. When using doubles (52 bits accuracy), the
 * 61-bit accuracy never shows at all.
 */
/*
 * 临时实数加法子程序。
 * 
 * 注意！这些并不精确：它们的宽度只有62位，并且不能进行正确的舍入操作。这些仅是草就之作。原因是为了不会溢出（1位），我们
 * 把值右移了2位，并且使得符号位（1位）能够移入尾数中。这是非常简单的算法，而62位（实际上是61位 - 没有舍入）的精度通常
 * 也足够了。只有当你把64位的整数相加时才会发觉一些奇怪的问题。当使用双精度（52位精度）数据时，是永远不可能超过61位精度
 * 的。
 */

#include <linux/math_emu.h>

// 求一个数的负数（二进制补码）表示。
// 把临时实数尾数（有效数）取反后再加1。
// 参数a是临时实数结构。其中a、b字段组合是实数的有效数。
#define NEGINT(a) \
__asm__("notl %0 ; notl %1 ; addl $1,%0 ; adcl $0,%1" \
	:"=r" (a->a),"=r" (a->b) \
	:"0" (a->a),"1" (a->b))

// 尾数符号化。
// 即把临时实数变换成指数和整数表示形式，便于仿真运算。因此这里称其为仿真格式。
static void signify(temp_real * a)
{
// 把64位二进制尾数右移2位（因此指数需要加2）。因为指针字段exponent的最高位是符号位，所以若指数值小于零，说明该数是负数。
// 于是则把尾数用补码表示（取负）。然后把指数取正值。此时尾数中不仅包含移过2位的有效数，而且还包含数值的符号位。
// 30行上：%0 - a->a；%1 - a->b。汇编指令“shrdl $2, %1, %0”执行双精度（64位）右移，即把组合尾数<b,a>右移2位。由于
// 该移动操作不会改变%1（a->b）中的值，因此还需要单独对其右移2位。
	a->exponent += 2;
	__asm__("shrdl $2,%1,%0 ; shrl $2,%1"   // 使用双精度指令把尾数右移2位。
		:"=r" (a->a),"=r" (a->b)
		:"0" (a->a),"1" (a->b));
	if (a->exponent < 0)                    // 是负数，则尾数用补码表示（取负值）。
		NEGINT(a);
	a->exponent &= 0x7fff;                  // 去掉符号位（若有）。
}

// 尾数非符号化。
// 将仿真格式转换为临时实数格式。即把指数和整数表示的实数转换为临时实数格式。
static void unsignify(temp_real * a)
{
// 对于值为0的数不用处理，直接返回。否则，我们先复位临时实数格式的符号位。然后判断尾数的高位long字段a->b是否带有符号位。
// 若有，则在exponent字段添加符号位，同时把尾数用无符号数形式表示（取补）。最后对尾数进行规格化处理，同时指数值作相应递
// 减。即执行左移操作，使得尾数最高有效位不为0（最后a->b值表现为负值）。
	if (!(a->a || a->b)) {                          // 若值为0就返回。
		a->exponent = 0;
		return;
	}
	a->exponent &= 0x7fff;                          // 去掉符号位（若有）。
	if (a->b < 0) {                                 // 去负数，则尾数取正值。
		NEGINT(a);
		a->exponent |= 0x8000;                  // 临时实数添加置符号位。
	}
	while (a->b >= 0) {                             // 对尾数进行规格化处理。
		a->exponent--;
		__asm__("addl %0,%0 ; adcl %1,%1"
			:"=r" (a->a),"=r" (a->b)
			:"0" (a->a),"1" (a->b));
	}
}

// 仿真浮点加法指令运算。
// 临时实数参数src1 + src2 -> result。
void fadd(const temp_real * src1, const temp_real * src2, temp_real * result)
{
	temp_real a,b;
	int x1,x2,shift;

// 首先取两个数的指数值x1、x2（去掉符号位）。然后让变量a等于基中最大值，shift为指数差值（即相差2的倍数值）。
	x1 = src1->exponent & 0x7fff;
	x2 = src2->exponent & 0x7fff;
	if (x1 > x2) {
		a = *src1;
		b = *src2;
		shift = x1-x2;
	} else {
		a = *src2;
		b = *src1;
		shift = x2-x1;
	}
// 若两者相差太大，大于等于2的64次方，则我们可以忽略小的那个数，即b值。于是直接返回a值即可。否则，若相差大于等于2的32次
// 方，那么我们可以忽略小值b中的低32位值。于是我们把b的高long字段值b.b右移32位，即放到b.a中。然后把b的指数值相应地增加
// 32次方。即指数差值减去32。这样调整之后，相加的两个数的尾数基本上落在相同区域中。
	if (shift >= 64) {
		*result = a;
		return;
	}
	if (shift >= 32) {
		b.a = b.b;
		b.b = 0;
		shift -= 32;
	}
// 接着再进行细致的调整，以将相加两者调整成相同。调整方法是把小值b的尾数右移shift各位。这样两者的指数相同，处于同一个数量级。
// 我们就要以对尾数进行相加运算了。相加之前我们需要先把它们转换成仿真运算格式。在加法运算后再变换回临时实数格式。
	__asm__("shrdl %4,%1,%0 ; shrl %4,%1"                   // 双精度（64位）右移。
		:"=r" (b.a),"=r" (b.b)
		:"0" (b.a),"1" (b.b),"c" ((char) shift));
	signify(&a);                                            // 变换格式。
	signify(&b);
	__asm__("addl %4,%0 ; adcl %5,%1"                       // 执行加法运算。
		:"=r" (a.a),"=r" (a.b)
		:"0" (a.a),"1" (a.b),"g" (b.a),"g" (b.b));
	unsignify(&a);                                          // 再变换回临时实数格式。
	*result = a;
}
