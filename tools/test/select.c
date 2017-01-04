#include <stdio.h>
#include <unistd.h>
#include <time.h>

int select(int nd, fd_set * in, fd_set *out, fd_set *ex, struct timeval *tv)
{
	long __res;
	register long __fooebx __asm__("bx") ;
	__fooebx=(long) &nd;

	__asm__ volatile ("int $0x80"
		: "=a" (__res)
		: "0" (__NR_select),"r" (__fooebx));
	if(__res>=0)
		return (int) __res;
	errno = -__res;
	return -1;
}


int main(int argc, char *argv[]) {
	fd_set *in;
	struct timeval *tvp;
/*	tvp.tv_sec=100; */

	in = (fd_set *)malloc(sizeof(fd_set));
        *in=7;
	tvp = (struct timeval*)malloc(sizeof(struct timeval));
	tvp->tv_sec=100;

	select(4,in,NULL,NULL,tvp);
        free(in);
	return(0);
}
