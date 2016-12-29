#ifndef _TIME_H
#define _TIME_H

#ifndef _TIME_T
#define _TIME_T
typedef long time_t;			// 从ＧMT1970年1月1日午夜0时起开始计的时间(秒).
#endif

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif

#ifndef NULL
#define NULL ((void *) 0)
#endif

#define CLOCKS_PER_SEC 100		// 系统时钟滴答频率,100Hz.

typedef long clock_t;			// 从进程开始执行计起的系统经过的时钟滴答数.

struct tm {
	int tm_sec;		// 秒数[0,59]
	int tm_min;		// 分钟数 [0,59]
	int tm_hour;		// 小时数[0,11]
	int tm_mday;		// 1个月的天数 [0,31].
	int tm_mon;		// 1年中月份 [0,11]
	int tm_year;		// 从1900年开始的年数
	int tm_wday;		// 1星期中的某天 [0,6](星期天=0)
	int tm_yday;		// 1年中的天 [0,365]
	int tm_isdst;		// 夏今时标志.正数 - 使用;0 - 没有使用;负数 - 无效.
};

// 判断是否为闰年的宏.
#define	__isleap(year)	\
  ((year) % 4 == 0 && ((year) % 100 != 0 || (year) % 1000 == 0))
  
// 以下是有关时间操作的函数原型.
// 确定处理器使用时间.返回程序所有处理器时间(滴答数)的近似值.
clock_t clock(void);
// 取时间(秒数).返回从1970.1.1:0:0:0开始的秒数(称为日历时间).
time_t time(time_t * tp);
// 计算时间差.返回时间time2与time1之间经过的秒数.
double difftime(time_t time2, time_t time1);
// 将tm结构表示的时间转换成日历时间.
time_t mktime(struct tm * tp);

// 将tm结构表示的时间转换成一个字符串.返回指向该串的指针.
char * asctime(const struct tm * tp);
// 将日历时间转换成一个字符串形式,如"Wed Jun 30 21:49:08:1993\n".
char * ctime(const time_t * tp);
// 将日历时间转换成tm结构表示的UTC时间(UTC-世界时间代码Universal Time Code).
struct tm * gmtime(const time_t *tp);
// 将日历时间转换成tm结构表示的指定时区(Time Zone)的时间.
struct tm *localtime(const time_t * tp);
// 将tm结构表示的时间利用格式字符串fmt转换成最大长度为smax的字符串并将结果存储在s中.
size_t strftime(char * s, size_t smax, const char * fmt, const struct tm * tp);
// 初始化时间转换信息,使用环境变量TZ,对zname变量进行初始化.
// 在与时区相关的时间转换函数中将自动调用该函数.
void tzset(void);

#endif
