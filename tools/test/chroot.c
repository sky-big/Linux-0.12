#define __LIBRARY__
#include <stdio.h>
#include <unistd.h>

_syscall1(int, chroot, const char *, filename)

int main(int argc, char *argv[]) {

    int i;
    if (i = chroot(argv[1])) {
        printf("error:%d,filename:%s", i, argv[1]);
    }
    return (0);
}
