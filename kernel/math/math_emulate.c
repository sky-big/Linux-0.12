/*
 * linux/kernel/math/math_emulate.c
 *
 * (C) 1991 Linus Torvalds
 */

/*
 * Limited emulation 27.12.91 - mostly loads/stores, which gcc wants
 * even for soft-float, unless you use bruce evans' patches. The patches
 * are great, but they have to be re-applied for every version, and the
 * library is different for soft-float and 80387. So emulation is more
 * practical, even though it's slower.
 *
 * 28.12.91 - loads/stores work, even BCD. I'll have to start thinking
 * about add/sub/mul/div. Urgel. I should find some good source, but I'll
 * just fake up something.
 *
 * 30.12.91 - add/sub/mul/div/com seem to work mostly. I should really
 * test every possible combination.
 */
/*
 * 仿真范围有限的程序91.12.27 -绝大多数是一些加载/存储指令。除非你使用了Bruce Evans的补丁程序，否则即使使用软件执行浮点
 * 运算，gcc也需要这些指令。Bruce的补丁程序非常好，但每次换gcc版本你都得用这个补丁程序。而且对于软件浮点实现和80387，所
 * 使用的库是不同的。因此使用仿真是更为实际的方法，尽管仿真方法更慢。
 * 
 * 91.12.24 - 加载/存储协处理器指令可以用了，即使是BCD码的也能使用。我将开始考虑实现add/sub/mul/div指令。唉，我应该找
 * 一些好的资料，不过现在我会先仿造一些操作。
 * 
 * 91.12.30 - add/sub/mul/div/com这些指令好像大多数可以使用了。我真应该测试每种指令可能的组合操作。
 */

/*
 * This file is full of ugly macros etc: one problem was that gcc simply
 * didn't want to make the structures as they should be: it has to try to
 * align them. Sickening code, but at least I've hidden the ugly things
 * in this one file: the other files don't need to know about these things.
 *
 * The other files also don't care about ST(x) etc - they just get addresses
 * to 80-bit temporary reals, and do with them as they please. I wanted to
 * hide most of the 387-specific things here.
 */
/*
 * 这个程序中到处到是些别扭的宏：问题之一是gcc就是不想把结构建立成其应该成为的样子；gcc企图对结构进行对齐处理。真是讨厌，
 * 不过我起码已经把所有蹩脚的代码都隐藏在这么一个文件中了，其他程序文件不需要了解这些信息。
 * 
 * 其他的程序也不需要知道ST(x)等80387内部结构 - 它们只需要得到80位临时实数的地址就可以随意操作。我想尽可能在这里隐藏所
 * 有387专有信息。
 */

#include <linux/math_emu.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#define bswapw(x) __asm__("xchgb %%al,%%ah":"=a" (x):"0" ((short)x))    // 交换2字节位置。
#define ST(x) (*__st((x)))                              // 取仿真的ST(x)累加器的值。
#define PST(x) ((const temp_real *) __st((x)))          // 取仿真的ST(x)累加器的指针。

/*
 * We don't want these inlined - it gets too messy in the machine-code.
 */
static void fpop(void);
static void fpush(void);
static void fxchg(temp_real_unaligned * a, temp_real_unaligned * b);
static temp_real_unaligned * __st(int i);

// 执行浮点指令仿真。
// 该函数首先检测仿真的I387结构状态字寄存器中是否有未屏蔽的异常标志置位。若有则对状态字中忙标志B进行设置。然后把指令指针
// 保存起来，并取出代码指针EIP处的2字节浮点指令代码code。接着分析代码code，并根据其含义进行处理。针对不同代码类型值，Linus
// 使用了几个不同的switch程序块进行仿真处理。
// 参数是info结构指针。
static void do_emu(struct info * info)
{
	unsigned short code;
	temp_real tmp;
	char * address;

// 该函数首先检测仿真的I387结构状态字寄存器中是否有未屏蔽的异常标志置位。若有就设置状态字中忙标志B（位15），否则复位B标
// 志。然后我们把指令指针保存起来。再看看执行本函数的代码是不是用户代码。如是不是，即调用者的代码段选择符不等于0x0f，则
// 说明内核中有代码使用了浮点指令。于是在显示出浮点指令处的CS、EIP值和信息“内核中需要数学仿真”后停机。
	if (I387.cwd & I387.swd & 0x3f)
		I387.swd |= 0x8000;             // 设置忙标志B。
	else
		I387.swd &= 0x7fff;             // 清忙标志B。
	ORIG_EIP = EIP;                         // 保存浮点指令指针。
/* 0x0007 means user code space */
	if (CS != 0x000F) {                     // 不是用户代码则停机。
		printk("math_emulate: %04x:%08x\n\r",CS,EIP);
		panic("Math emulation needed in kernel");
	}
// 然后我们取出代码指针EIP处的2字节浮点指令代码code。由于Inter CPU存储数据时是“小头”（Little endian）在前的，此时取出
// 的代码正好与指令第1、第2字节顺序颠倒。因此我们需要交换一下code中两个字节的顺序。然后再屏蔽掉第1个代码字节中的ESC位（二
// 进制11011）。接着把浮点指令指针EIP保存到TSS段i387结构中的fip字段中，而CS保存到fcs字段中，同时把略微处理过的浮点指令
// 代码code放到fcs字段的高16位中。保存这些值是为了在出现仿真的处理器异常时程序可以像使用真实的协处理器一样进行处理。最后
// 让EIP指向随后的浮点指令或操作数。
	code = get_fs_word((unsigned short *) EIP);     // 取2字节的浮点指令或操作数。
	bswapw(code);                                   // 交换高低字节。
	code &= 0x7ff;                                  // 屏蔽代码中的ESC码。
	I387.fip = EIP;                                 // 保存指令指针。
	*(unsigned short *) &I387.fcs = CS;             // 保存代码段选择符。
	*(1+(unsigned short *) &I387.fcs) = code;       // 保存代码。
	EIP += 2;                                       // 指令指针指向下一个字节。
        switch (code) {
		case 0x1d0: /* fnop */          // 空操作指令FNOP
			return;
		case 0x1d1: case 0x1d2: case 0x1d3:     // 无效指令代码。必信号，退出。
		case 0x1d4: case 0x1d5: case 0x1d6: case 0x1d7:
			math_abort(info,1<<(SIGILL-1));
		case 0x1e0:                     // FCHS - 改变ST符号位。即ST = -ST。
			ST(0).exponent ^= 0x8000;
			return;
		case 0x1e1:                     // FABS - 取绝对值。即ST = |ST|。
			ST(0).exponent &= 0x7fff;
			return;
		case 0x1e2: case 0x1e3:         // 无效指令代码。发信号，退出。
			math_abort(info,1<<(SIGILL-1));
		case 0x1e4:                     // FTST - 测试TS，同时设置状态字中Cn。
			ftst(PST(0));
			return;
		case 0x1e5:                     // FXAM - 检查TS值，同时修改状态字中Cn。
			printk("fxam not implemented\n\r");     // 未实现。发信号退出。
			math_abort(info,1<<(SIGILL-1));
		case 0x1e6: case 0x1e7:         // 无效指令代码。发信号，退出。
			math_abort(info,1<<(SIGILL-1));
		case 0x1e8:                     // FLD1 - 加载常数1.0到累加器ST。
			fpush();
			ST(0) = CONST1;
			return;
		case 0x1e9:                     // FLDL2T - 加载常数Log2(10)到累加器ST。
			fpush();
			ST(0) = CONSTL2T;
			return;
		case 0x1ea:                     // FLDL2E - 加载常数Log2(e)到累加器ST。
			fpush();
			ST(0) = CONSTL2E;
			return;
		case 0x1eb:                     // FLDPI - 加载常数Pi到累加器ST。
			fpush();
			ST(0) = CONSTPI;
			return;
		case 0x1ec:                     // FLDLG2 - 加载常数Log10(2)到累加器ST。
			fpush();
			ST(0) = CONSTLG2;
			return;
		case 0x1ed:                     // FLDLN2 - 加载常数Loge(2)到累加器ST。
			fpush();
			ST(0) = CONSTLN2;
			return;
		case 0x1ee:                     // FLDZ - 加载常数0.0到累加器ST。
			fpush();
			ST(0) = CONSTZ;
			return;
		case 0x1ef:                     // 无效和未实现仿真指令代码。发信号，退出。
			math_abort(info,1<<(SIGILL-1));
		case 0x1f0: case 0x1f1: case 0x1f2: case 0x1f3:
		case 0x1f4: case 0x1f5: case 0x1f6: case 0x1f7:
		case 0x1f8: case 0x1f9: case 0x1fa: case 0x1fb:
		case 0x1fc: case 0x1fd: case 0x1fe: case 0x1ff:
			printk("%04x fxxx not implemented\n\r",code + 0xc800);
			math_abort(info,1<<(SIGILL-1));
		case 0x2e9:                     // FUCOMPP - 无次序比较。
			fucom(PST(1),PST(0));
			fpop(); fpop();
			return;
		case 0x3d0: case 0x3d1:         // FNOP - 对387。！！应该是0x3e0，0x3e1。
			return;
		case 0x3e2:                     // FCLEX - 清状态字中异常标志。
			I387.swd &= 0x7f00;
			return;
		case 0x3e3:                     // FINIT - 初始化协处理器。
			I387.cwd = 0x037f;
			I387.swd = 0x0000;
			I387.twd = 0x0000;
			return;
		case 0x3e4:                     // FNOP - 对80387。
			return;
		case 0x6d9:                     // FCOMPP - ST(i)与ST比较，出栈操作两次。
			fcom(PST(1),PST(0));
			fpop(); fpop();
			return;
		case 0x7e0:                     // FSTSW AX - 保存当前状态字到AX寄存器中。
			*(short *) &EAX = I387.swd;
			return;
	}
// 下面开始处理第2字节最后3位是REG的指令。即11011,XXXXXXXX，REG形式的代码。
	switch (code >> 3) {
		case 0x18:                      // FADD ST, ST(i)。
			fadd(PST(0),PST(code & 7),&tmp);
			real_to_real(&tmp,&ST(0));
			return;
		case 0x19:                      // FMUL ST, ST(i)。
			fmul(PST(0),PST(code & 7),&tmp);
			real_to_real(&tmp,&ST(0));
			return;
		case 0x1a:                      // FCOM ST(i)。
			fcom(PST(code & 7),&tmp);
			real_to_real(&tmp,&ST(0));
			return;
		case 0x1b:                      // FCOMP ST(i)。
			fcom(PST(code & 7),&tmp);
			real_to_real(&tmp,&ST(0));
			fpop();
			return;
		case 0x1c:                      // FSUB ST, ST(i)。
			real_to_real(&ST(code & 7),&tmp);
			tmp.exponent ^= 0x8000;
			fadd(PST(0),&tmp,&tmp);
			real_to_real(&tmp,&ST(0));
			return;
		case 0x1d:                      // FSUBR ST, ST(i)。
			ST(0).exponent ^= 0x8000;
			fadd(PST(0),PST(code & 7),&tmp);
			real_to_real(&tmp,&ST(0));
			return;
		case 0x1e:                      // FDIV ST, ST(i)。
			fdiv(PST(0),PST(code & 7),&tmp);
			real_to_real(&tmp,&ST(0));
			return;
		case 0x1f:                      // FDIVR ST, ST(i)。
			fdiv(PST(code & 7),PST(0),&tmp);
			real_to_real(&tmp,&ST(0));
			return;
		case 0x38:                      // FLD ST(i)。
			fpush();
			ST(0) = ST((code & 7)+1);
			return;
		case 0x39:                      // FXCH ST(i)。
			fxchg(&ST(0),&ST(code & 7));
			return;
		case 0x3b:                      // FSTP ST(i)。
			ST(code & 7) = ST(0);
			fpop();
			return;
		case 0x98:                      // FADD ST(i)， ST。
			fadd(PST(0),PST(code & 7),&tmp);
			real_to_real(&tmp,&ST(code & 7));
			return;
		case 0x99:                      // FMUL ST(i)， ST。
			fmul(PST(0),PST(code & 7),&tmp);
			real_to_real(&tmp,&ST(code & 7));
			return;
		case 0x9a:                      // FCOM ST(i)。
			fcom(PST(code & 7),PST(0));
			return;
		case 0x9b:                      // FCOMP ST(i)。
			fcom(PST(code & 7),PST(0));
			fpop();
			return;			
		case 0x9c:                      // FSUBR ST(i), ST。
			ST(code & 7).exponent ^= 0x8000;
			fadd(PST(0),PST(code & 7),&tmp);
			real_to_real(&tmp,&ST(code & 7));
			return;
		case 0x9d:                      // FSUB ST(i), ST。
			real_to_real(&ST(0),&tmp);
			tmp.exponent ^= 0x8000;
			fadd(PST(code & 7),&tmp,&tmp);
			real_to_real(&tmp,&ST(code & 7));
			return;
		case 0x9e:                      // FDIVR ST(i), ST。
			fdiv(PST(0),PST(code & 7),&tmp);
			real_to_real(&tmp,&ST(code & 7));
			return;
		case 0x9f:                      // FDIV ST(i), ST。
			fdiv(PST(code & 7),PST(0),&tmp);
			real_to_real(&tmp,&ST(code & 7));
			return;
		case 0xb8:                      // FFREE ST(i), ST。未实现。
			printk("ffree not implemented\n\r");
			math_abort(info,1<<(SIGILL-1));
		case 0xb9:                      // FXCH ST(i)。
			fxchg(&ST(0),&ST(code & 7));
			return;
		case 0xba:                      // FST ST(i)。
			ST(code & 7) = ST(0);
			return;
		case 0xbb:                      // FSTP ST(i)。
			ST(code & 7) = ST(0);
			fpop();
			return;
		case 0xbc:                      // FUCOM ST(i)。
			fucom(PST(code & 7),PST(0));
			return;
		case 0xbd:                      // FUCOMP ST(i)。
			fucom(PST(code & 7),PST(0));
			fpop();
			return;
		case 0xd8:                      // FADDP ST(i), ST。
			fadd(PST(code & 7),PST(0),&tmp);
			real_to_real(&tmp,&ST(code & 7));
			fpop();
			return;
		case 0xd9:                      // FMULP ST(i), ST。
			fmul(PST(code & 7),PST(0),&tmp);
			real_to_real(&tmp,&ST(code & 7));
			fpop();
			return;
		case 0xda:                      // FCOMP ST(i)。
			fcom(PST(code & 7),PST(0));
			fpop();
			return;
		case 0xdc:                      // FSUBRP ST(i)。
			ST(code & 7).exponent ^= 0x8000;
			fadd(PST(0),PST(code & 7),&tmp);
			real_to_real(&tmp,&ST(code & 7));
			fpop();
			return;
		case 0xdd:                      // FSUBP ST(I), ST。
			real_to_real(&ST(0),&tmp);
			tmp.exponent ^= 0x8000;
			fadd(PST(code & 7),&tmp,&tmp);
			real_to_real(&tmp,&ST(code & 7));
			fpop();
			return;
		case 0xde:                      // FDIVRP ST(i), ST。
			fdiv(PST(0),PST(code & 7),&tmp);
			real_to_real(&tmp,&ST(code & 7));
			fpop();
			return;
		case 0xdf:                      // FDIVP ST(i), ST。
			fdiv(PST(code & 7),PST(0),&tmp);
			real_to_real(&tmp,&ST(code & 7));
			fpop();
			return;
		case 0xf8:                      // FFREE ST(i)。未实现。
			printk("ffree not implemented\n\r");
			math_abort(info,1<<(SIGILL-1));
			fpop();
			return;
		case 0xf9:                      // FXCH ST(i)。
			fxchg(&ST(0),&ST(code & 7));
			return;
		case 0xfa:                      // FSTP ST(i)。
		case 0xfb:                      // FSTP ST(i)。
			ST(code & 7) = ST(0);
			fpop();
			return;
	}
// 处理第2个字节位7--6是MOD、位2--0是R/M的指令，即11011,XXX，MOD，XXX，R/M形式的代码。MOD在各子程序中处理，因此这里首先
// 让代码与上0xe7（0b11100111）屏蔽掉MOD。
	switch ((code>>3) & 0xe7) {
		case 0x22:
//			put_short_real(PST(0),info,code);
                    panic("kernel/math/math_emulate.c->do_emu(),394");
			return;
		case 0x23:                      // FSTP - 保存单精度实数（短实数）。
			put_short_real(PST(0),info,code);
			fpop();
			return;
		case 0x24:                      // FLDENV - 加载协处理器状态和控制寄存器等。
			address = ea(info,code);
			for (code = 0 ; code < 7 ; code++) {
				((long *) & I387)[code] =
				   get_fs_long((unsigned long *) address);
				address += 4;
			}
			return;
		case 0x25:                      // FLDCW - 加载控制字。
			address = ea(info,code);
			*(unsigned short *) &I387.cwd =
				get_fs_word((unsigned short *) address);
			return;
		case 0x26:                      // FSTENV - 储存协处理器状态和控制寄存器等。
			address = ea(info,code);
			verify_area(address,28);
			for (code = 0 ; code < 7 ; code++) {
				put_fs_long( ((long *) & I387)[code],
					(unsigned long *) address);
				address += 4;
			}
			return;
		case 0x27:                      // FSTCW - 存储控制字。
			address = ea(info,code);
			verify_area(address,2);
			put_fs_word(I387.cwd,(short *) address);
			return;
		case 0x62:                      // FIST - 存储短整形数。
			put_long_int(PST(0),info,code);
			return;
		case 0x63:                      // FISTP - 存储短整型数。
			put_long_int(PST(0),info,code);
			fpop();
			return;
		case 0x65:                      // FLD - 加载扩展（临时）实数。
			fpush();
			get_temp_real(&tmp,info,code);
			real_to_real(&tmp,&ST(0));
			return;
		case 0x67:                      // FSTP - 储存扩展实数。
			put_temp_real(PST(0),info,code);
			fpop();
			return;
		case 0xa2:                      // FST - 储存双精度实数。
			put_long_real(PST(0),info,code);
			return;
		case 0xa3:                      // FSTP - 存储双精度实数。
			put_long_real(PST(0),info,code);
			fpop();
			return;
		case 0xa4:                      // FRSTOR - 恢复所有108字节的寄存器内容。
			address = ea(info,code);
			for (code = 0 ; code < 27 ; code++) {
				((long *) & I387)[code] =
				   get_fs_long((unsigned long *) address);
				address += 4;
			}
			return;
		case 0xa6:                      // FSAVE - 保存所有108字节寄存器内容。
			address = ea(info,code);
			verify_area(address,108);
			for (code = 0 ; code < 27 ; code++) {
				put_fs_long( ((long *) & I387)[code],
					(unsigned long *) address);
				address += 4;
			}
			I387.cwd = 0x037f;
			I387.swd = 0x0000;
			I387.twd = 0x0000;
			return;
		case 0xa7:                      // FSTSW - 保存状态状态字。
			address = ea(info,code);
			verify_area(address,2);
			put_fs_word(I387.swd,(short *) address);
			return;
		case 0xe2:                      // FIST - 保存短整型数。
			put_short_int(PST(0),info,code);
			return;
		case 0xe3:                      // FISTP - 保存短整型数。
			put_short_int(PST(0),info,code);
			fpop();
			return;
		case 0xe4:                      // FBLD - 加载BCD类型数。
			fpush();
			get_BCD(&tmp,info,code);
			real_to_real(&tmp,&ST(0));
			return;
		case 0xe5:                      // FILD - 加载长整型数。
			fpush();
			get_longlong_int(&tmp,info,code);
			real_to_real(&tmp,&ST(0));
			return;
		case 0xe6:                      // FBSTP - 保存BCD类型数。
			put_BCD(PST(0),info,code);
			fpop();
			return;
		case 0xe7:                      // BISTP - 保存长整型数。
			put_longlong_int(PST(0),info,code);
			fpop();
			return;
	}
// 下面处理第2类浮点指令。首先根据指令代码的位10--9的MF值取指定类型的数，然后根据OPA和OPB的组合值进行分别处理。即处理11011，
// MF，000,XXX，R/M形式的指令代码。
	switch (code >> 9) {
		case 0:                 // MF = 00，短实数（32位整数）。
			get_short_real(&tmp,info,code);
			break;
		case 1:                 // MF = 01，短整数（32位整数）。
			get_long_int(&tmp,info,code);
			break;
		case 2:                 // MF = 10，长实数（64位实数）。
			get_long_real(&tmp,info,code);
			break;
		case 4:                 // MF = 11，长整数（64位整数）！应该是case 3。
			get_short_int(&tmp,info,code);
	}
	switch ((code>>3) & 0x27) {
		case 0:                 // FADD。
			fadd(&tmp,PST(0),&tmp);
			real_to_real(&tmp,&ST(0));
			return;
		case 1:                 // FMUL。
			fmul(&tmp,PST(0),&tmp);
			real_to_real(&tmp,&ST(0));
			return;
		case 2:                 // FCOM。
			fcom(&tmp,PST(0));
			return;
		case 3:                 // FCOMP。
			fcom(&tmp,PST(0));
			fpop();
			return;
		case 4:                 // FSUB。
			tmp.exponent ^= 0x8000;
			fadd(&tmp,PST(0),&tmp);
			real_to_real(&tmp,&ST(0));
			return;
		case 5:                 // FSUBR。
			ST(0).exponent ^= 0x8000;
			fadd(&tmp,PST(0),&tmp);
			real_to_real(&tmp,&ST(0));
			return;
		case 6:                 // FDIV。
			fdiv(PST(0),&tmp,&tmp);
			real_to_real(&tmp,&ST(0));
			return;
		case 7:                 // FDIVR。
			fdiv(&tmp,PST(0),&tmp);
			real_to_real(&tmp,&ST(0));
			return;
	}
// 处理形如11011,XX,1,XX，000,R/M的指令代码。
	if ((code & 0x138) == 0x100) {
			fpush();
			real_to_real(&tmp,&ST(0));
			return;
	}
// 其余均为无效指令。
	printk("Unknown math-insns: %04x:%08x %04x\n\r",CS,EIP,code);
	math_abort(info,1<<(SIGFPE-1));
}

// CPU异常中断int 7调用的80387仿真接口函数。
// 若当前进程没有使用过协处理器，就设置使用协处理器标志used_math，然后初始化80387的控制字、状态字和特征字。最后使用中断
// int 7调用本函数的返回地址指针作为参数调用浮点指令仿真主函数do_emu()。
// 参数____false是_orig_eip。
void math_emulate(long ___false)
{
	if (!current->used_math) {
		current->used_math = 1;
		I387.cwd = 0x037f;
		I387.swd = 0x0000;
		I387.twd = 0x0000;
	}
/* &___false points to info->___orig_eip, so subtract 1 to get info */
	do_emu((struct info *) ((&___false) - 1));
}

// 终止仿真操作。
// 当处理到无效指令代码或者未实现的指令代码时，该函数首先恢复程序的原EIP，并发送指定信号给当前进程。最后将栈指针指向中断
// int 7处理过程调用本函数的返回地址，直接返回到中断处理过程中。
void __math_abort(struct info * info, unsigned int signal)
{
	EIP = ORIG_EIP;
	current->signal |= signal;
	__asm__("movl %0,%%esp ; ret"::"g" ((long) info));
}

// 累加器栈弹出操作。
// 将状态字TOP字段值加1，并以7取模。
static void fpop(void)
{
	unsigned long tmp;

	tmp = I387.swd & 0xffffc7ff;
	I387.swd += 0x00000800;
	I387.swd &= 0x00003800;
	I387.swd |= tmp;
}

// 累加器栈入栈操作。
// 将状态字TOP字段减1（即加7），并以7取模。
static void fpush(void)
{
	unsigned long tmp;

	tmp = I387.swd & 0xffffc7ff;
	I387.swd += 0x00003800;
	I387.swd &= 0x00003800;
	I387.swd |= tmp;
}

// 交换两个累加器的值。
static void fxchg(temp_real_unaligned * a, temp_real_unaligned * b)
{
	temp_real_unaligned c;

	c = *a;
	*a = *b;
	*b = c;
}

// 取ST(i)的内存指针。
// 取状态字中TOP字段值。加上指定的物理数据寄存器号并取模，最后返回ST(i)对应的指针。
static temp_real_unaligned * __st(int i)
{
	i += I387.swd >> 11;            // 取状态字中TOP字段值。
	i &= 7;
	return (temp_real_unaligned *) (i*10 + (char *)(I387.st_space));
}