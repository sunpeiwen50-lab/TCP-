#include "net_util.h"
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>

int set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

ssize_t writen(int fd, const void *buf, size_t len)
{
    const char *p = buf;
    size_t left = len;
    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n > 0) {
            p += n;
            left -= (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;                       /* 被信号打断，重试 */
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            /* 发送缓冲区满，等待可写 */
            struct pollfd pfd = { .fd = fd, .events = POLLOUT };
            if (poll(&pfd, 1, 5000) <= 0) return -1;   /* 超时/出错 */
        } else {
            return -1;                      /* 真正的错误 */
        }
    }
    return (ssize_t)len;
}

ssize_t readn(int fd, void *buf, size_t len)
{
    char *p = buf;
    size_t left = len;
    while (left > 0) {
        ssize_t n = read(fd, p, left);
        if (n > 0) {
            p += n;
            left -= (size_t)n;
        } else if (n == 0) {
            return 0;                       /* 对端关闭 */
        } else if (errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return (ssize_t)len;
}
