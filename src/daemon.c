#include "daemon.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

int daemonize(void)
{
    pid_t pid;

    /* 1. 第一次 fork，父进程退出，使子进程不是进程组组长 */
    pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) _exit(0);          /* 父进程退出 */

    /* 2. 创建新会话，脱离原控制终端 */
    if (setsid() < 0) return -1;

    /* 3. 第二次 fork，确保进程不再是会话首进程，永远无法获得控制终端 */
    pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) _exit(0);

    /* 4. 重设文件掩码（不保留工作目录切换，便于使用 ./files 相对路径） */
    umask(0);

    /* 5. 把标准输入/输出/错误重定向到 /dev/null */
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) close(fd);
    }
    return 0;
}
