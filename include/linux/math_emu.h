/*
 * linux/include/linux/math_emu.h
 *
 * (C) 1991 Linus Torvalds
 */
#ifndef _LINUX_MATH_EMU_H
#define _LINUX_MATH_EMU_H

#include <linux/sched.h>

struct info {
	long ___math_ret;               // math_emulate()调用者（int 7）返回地址。
	long ___orig_eip;               // 临时保存原EIP的地方。
	long ___edi;                    // 异常中断int 7处理过程入栈的寄存器。
	long ___esi;
	long ___ebp;
	long ___sys_call_ret;           // 中断7返回时将去执行系统调用的返回处理代码。
	long ___eax;                    // 以下部分（18--30行）与系统调用时栈中结构相同。
	long ___ebx;
	long ___ecx;
	long ___edx;
	long ___orig_eax;               // 如不是系统调用而是其他中断时，该值为-1。
	long ___fs;
	long ___es;
	long ___ds;
	long ___eip;                    // 26--30行由CPU自动入栈。
	long ___cs;
	long ___eflags;
	long ___esp;
	long ___ss;
};

#define EAX (info->___eax)
#define EBX (info->___ebx)
#define ECX (info->___ecx)
#define EDX (info->___edx)
#define ESI (info->___esi)
#define EDI (info->___edi)
#define EBP (info->___ebp)
#define ESP (info->___esp)
#define EIP (info->___eip)
#define ORIG_EIP (info->___orig_eip)
#define EFLAGS (info->___eflags)
#define DS (*(unsigned short *) &(info->___ds))
#define ES (*(unsigned short *) &(info->___es))
#define FS (*(unsigned short *) &(info->___fs))
#define CS (*(unsigned short *) &(info->___cs))
#define SS (*(unsigned short *) &(info->___ss))

void __math_abort(struct info *, unsigned int);

#define math_abort(x,y) \
(((void (*)(struct info *,unsigned int)) __math_abort)((x),(y)))

/*
 * Gcc forces this stupid alignment problem: I want to use only two longs
 * for the temporary real 64-bit mantissa, but then gcc aligns out the
 * structure to 12 bytes which breaks things in math_emulate.c. Shit. I
 * want some kind of "no-alignt" pragma or something.
 */
/*
 * Gcc强制对齐问题是愚蠢：我想仅仅使用两个long去表示临时实型64位的有效数，但。
 */
// 临时实数结构
typedef struct {
	long a,b;               // 有效数。
	short exponent;         // 指数。
} temp_real;

// 临时实数结构（不进行对齐）
typedef struct {
	short m0,m1,m2,m3;      // 有效数。
	short exponent;         // 指数。
} temp_real_unaligned;

#define real_to_real(a,b) \
((*(long long *) (b) = *(long long *) (a)),((b)->exponent = (a)->exponent))

// 长实型结构。
typedef struct {
	long a,b;
} long_real;

typedef long short_real;        // 短实型。

typedef struct {
	long a,b;
	short sign;
} temp_int;                     // 临时整型。

// 协处理器状态字数据结构。
struct swd {
	int ie:1;               // 无效操作。
	int de:1;               // 非规格化。
	int ze:1;               // 除零。
	int oe:1;               // 上溢出。
	int ue:1;               // 下溢出。
	int pe:1;               // 精度。
	int sf:1;               // 栈出错标志。
	int ir:1;               // 
	int c0:1;               // 条件位。
	int c1:1;
	int c2:1;
	int top:3;              // 用于指明当前哪个80位寄存器位于栈顶。
	int c3:1;
	int b:1;                // 忙。
};

#define I387 (current->tss.i387)        // 当前任务结构中保存的数学协处理器结构。
#define SWD (*(struct swd *) &I387.swd) // 协处理器状态字。
#define ROUNDING ((I387.cwd >> 10) & 3) // 取控制字中舍入控制字段。
#define PRECISION ((I387.cwd >> 8) & 3) // 取控制字中精度控制字段。

#define BITS24	0
#define BITS53	2
#define BITS64	3

#define ROUND_NEAREST	0               // 舍入到最近或偶数。
#define ROUND_DOWN	1               // 趋向负无限。
#define ROUND_UP	2               // 趋向正无限。
#define ROUND_0		3               // 无效。

#define CONSTZ   (temp_real_unaligned) {0x0000,0x0000,0x0000,0x0000,0x0000}     // 常数0.0。
#define CONST1   (temp_real_unaligned) {0x0000,0x0000,0x0000,0x8000,0x3FFF}     // 临时实数1.0。
#define CONSTPI  (temp_real_unaligned) {0xC235,0x2168,0xDAA2,0xC90F,0x4000}     // 常数Pi。
#define CONSTLN2 (temp_real_unaligned) {0x79AC,0xD1CF,0x17F7,0xB172,0x3FFE}     // 常数Loge(2)。
#define CONSTLG2 (temp_real_unaligned) {0xF799,0xFBCF,0x9A84,0x9A20,0x3FFD}     // 常数Log10(2)。
#define CONSTL2E (temp_real_unaligned) {0xF0BC,0x5C17,0x3B29,0xB8AA,0x3FFF}     // 常数Log2(e)。
#define CONSTL2T (temp_real_unaligned) {0x8AFE,0xCD1B,0x784B,0xD49A,0x4000}     // 常数Log2(10)。

// 置状态字控制器中异常位。
#define set_IE() (I387.swd |= 1)        // 无效操作。
#define set_DE() (I387.swd |= 2)        // 非规格化。
#define set_ZE() (I387.swd |= 4)        // 除零。
#define set_OE() (I387.swd |= 8)        // 上溢出。
#define set_UE() (I387.swd |= 16)       // 下溢出。
#define set_PE() (I387.swd |= 32)       // 精度。

// 下面宏定义用于设置状态字中条件标志位C0、C1、C2和C3。
#define set_C0() (I387.swd |= 0x0100)
#define set_C1() (I387.swd |= 0x0200)
#define set_C2() (I387.swd |= 0x0400)
#define set_C3() (I387.swd |= 0x4000)
/* ea.c */

char * ea(struct info * __info, unsigned short __code);

/* convert.c */

void short_to_temp(const short_real * __a, temp_real * __b);
void long_to_temp(const long_real * __a, temp_real * __b);
void temp_to_short(const temp_real * __a, short_real * __b);
void temp_to_long(const temp_real * __a, long_real * __b);
void real_to_int(const temp_real * __a, temp_int * __b);
void int_to_real(const temp_int * __a, temp_real * __b);

/* get_put.c */

void get_short_real(temp_real *, struct info *, unsigned short);
void get_long_real(temp_real *, struct info *, unsigned short);
void get_temp_real(temp_real *, struct info *, unsigned short);
void get_short_int(temp_real *, struct info *, unsigned short);
void get_long_int(temp_real *, struct info *, unsigned short);
void get_longlong_int(temp_real *, struct info *, unsigned short);
void get_BCD(temp_real *, struct info *, unsigned short);
void put_short_real(const temp_real *, struct info *, unsigned short);
void put_long_real(const temp_real *, struct info *, unsigned short);
void put_temp_real(const temp_real *, struct info *, unsigned short);
void put_short_int(const temp_real *, struct info *, unsigned short);
void put_long_int(const temp_real *, struct info *, unsigned short);
void put_longlong_int(const temp_real *, struct info *, unsigned short);
void put_BCD(const temp_real *, struct info *, unsigned short);

/* add.c */

void fadd(const temp_real *, const temp_real *, temp_real *);

/* mul.c */

void fmul(const temp_real *, const temp_real *, temp_real *);

/* div.c */

void fdiv(const temp_real *, const temp_real *, temp_real *);

/* compare.c */

void fcom(const temp_real *, const temp_real *);        // 仿真浮点指令FTST。
void fucom(const temp_real *, const temp_real *);
void ftst(const temp_real *);

#endif
