#ifndef DAEMONIZE_H
#define DAEMONIZE_H

/*
 * 将当前进程变为守护进程：
 *  1. fork 后父进程退出，子进程脱离控制终端
 *  2. setsid 创建新会话，成为会话首进程
 *  3. 再 fork 一次，确保不会重新获得控制终端
 *  4. chdir("/") 切换工作目录(可选，这里保留运行目录)
 *  5. 重设 umask，关闭/重定向标准流到 /dev/null
 * 成功返回 0。
 */
int daemonize(void);

#endif /* DAEMONIZE_H */
