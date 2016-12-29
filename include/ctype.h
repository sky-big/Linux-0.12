#ifndef _CTYPE_H
#define _CTYPE_H

#define _U	0x01	/* upper */	// 该比特位用于大写字符[A-Z]
#define _L	0x02	/* lower */	// 该比特位用于小写字符[a-z]
#define _D	0x04	/* digit */	// 该比特位用于数字[0-9]
#define _C	0x08	/* cntrl */	// 该比特位用于控制字符
#define _P	0x10	/* punct */	// 该比特位用于标点字符
#define _S	0x20	/* white space (space/lf/tab) */	// 空白字符,如空格,\t,\n等
#define _X	0x40	/* hex digit */	// 该比特位用于十六进制数字
#define _SP	0x80	/* hard space (0x20) */	// 该比特位用于空格字符(0x20)

extern unsigned char _ctype[];	// 字符特性数组(表),定义各个字符对应上面的属性.
extern char _ctmp;	// 一个临时字符变量(在定义lib/ctype.c中).

// 下面是一些确定字符类型的宏
#define isalnum(c) ((_ctype+1)[c]&(_U|_L|_D))		// 是字符或数字[A-Z],[a-z]或[0-9]
#define isalpha(c) ((_ctype+1)[c]&(_U|_L))		// 是字符
#define iscntrl(c) ((_ctype+1)[c]&(_C))			// 是控制字符
#define isdigit(c) ((_ctype+1)[c]&(_D))			// 是数字
#define isgraph(c) ((_ctype+1)[c]&(_P|_U|_L|_D))	// 是图形字符
#define islower(c) ((_ctype+1)[c]&(_L))			// 是小写字符
#define isprint(c) ((_ctype+1)[c]&(_P|_U|_L|_D|_SP))	// 是可打印字符
#define ispunct(c) ((_ctype+1)[c]&(_P))			// 是标点符号
#define isspace(c) ((_ctype+1)[c]&(_S))			// 是空白字符如空格,\f,\n,\r,\t,\w
#define isupper(c) ((_ctype+1)[c]&(_U))			// 是大写字符
#define isxdigit(c) ((_ctype+1)[c]&(_D|_X))		// 是十六进制数字

// 下面两个定义中,宏参数前使用了前缀(unsigned),因此c应该加括号,即表示成(c).因为在程序中c可能是一个复杂的表达式.
// 例如参数是a + b,若不加括号,则在宏定义中变成了:(unsigned) a + b.这显然不对.加了括号就能正确表示成(unsigned)(a + b)
#define isascii(c) (((unsigned) (c))<=0x7f)	// 是ASCII字符
#define toascii(c) (((unsigned) (c))&0x7f)	// 转换成ASCII字符

// 以下两个宏定义中使用一个临时变量_ctmp的原因是:在宏定义中,宏的参数只能被使用一次.但对于多线程来说这是不安全的,因为两个或多个
// 线程可能在同一时刻使用这个公共临时亦是.
// 因此从Linux 2.2.x版本开始更改为使用两个函数来取代这从个宏定义.
#define tolower(c) (_ctmp=c,isupper(_ctmp)?_ctmp-('A'-'a'):_ctmp)	// 转换成小写字符
#define toupper(c) (_ctmp=c,islower(_ctmp)?_ctmp-('a'-'A'):_ctmp)	// 转换成大写字符

#endif
