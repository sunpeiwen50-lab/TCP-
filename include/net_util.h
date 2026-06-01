#ifndef NET_UTIL_H
#define NET_UTIL_H

#include "common.h"

/* 设置 fd 为非阻塞 */
int set_nonblock(int fd);

/*
 * 可靠发送：保证把 len 字节全部写出。
 * 处理 EINTR；遇到 EAGAIN 时用 poll 等待可写(最多 5 秒)。
 * 成功返回 len，失败返回 -1。
 */
ssize_t writen(int fd, const void *buf, size_t len);

/*
 * 可靠接收：读满 len 字节（阻塞 socket 使用，客户端用）。
 * 对端关闭返回 0，出错返回 -1，成功返回 len。
 */
ssize_t readn(int fd, void *buf, size_t len);

#endif /* NET_UTIL_H */
