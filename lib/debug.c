// Linux0.12 打印信息相关
#include <stdarg.h>

#include <linux/kernel.h>

// 函数vsprintf()定义在linux/kernel/vsprintf.c中
extern int vsprintf(char * buf, const char * fmt, va_list args);

static char log_buf[1024];                       // 显示用临时缓冲区。

static unsigned short cur_log_level = LOG_INFO_TYPE;

// linux0.12 kernel log function
void Log(unsigned short log_level, const char *fmt, ...)
{
    if (log_level >= cur_log_level) {
        va_list args;                           // va_list实际上是一个字符指针类型.

        // 运行参数处理开始函数.然后使用格式串fmt将参数列表args输出到buf中.返回值i等于输出字符串的长度.再运行参数处理结束函数.最后调用控制台显示
        // 函数并返回显示字符数.
        va_start(args, fmt);
        vsprintf(log_buf, fmt, args);
        va_end(args);
        console_print(log_buf);                 // chr_drv/console.c
    }
}
