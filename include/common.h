#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>

/* ======== 全局配置 ======== */
#define SERVER_PORT      8888          /* 默认监听端口 */
#define MAX_EVENTS       1024          /* epoll 一次返回的最大事件数 */
#define BACKLOG          128           /* listen 队列长度 */
#define THREAD_POOL_NUM  8             /* glib 线程池工作线程数 */

#define NAME_LEN         32            /* 用户名最大长度 */
#define MAX_BODY_LEN     (64 * 1024)   /* 单条消息 body 最大长度 64KB */
#define RING_BUF_SIZE    (256 * 1024)  /* 每连接环形缓冲区大小 256KB */

#define FILE_CHUNK_SIZE  (32 * 1024)   /* 文件传输每块大小 32KB */
#define FILE_STORE_DIR   "./files"     /* 服务端文件存储目录 */

#define HEARTBEAT_INTERVAL  5          /* 客户端心跳发送间隔(秒) */
#define HEARTBEAT_TIMEOUT   15         /* 服务端心跳超时(秒)，超过则踢下线 */

/* ======== 通用工具宏 ======== */
#define UNUSED(x) ((void)(x))

/* 错误打印并退出 */
#define ERR_EXIT(msg) do { perror(msg); exit(EXIT_FAILURE); } while (0)

#endif /* COMMON_H */
