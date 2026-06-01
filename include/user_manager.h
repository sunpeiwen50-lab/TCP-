#ifndef USER_MANAGER_H
#define USER_MANAGER_H

#include "common.h"
#include "ring_buffer.h"
#include <pthread.h>
#include <time.h>
#include <glib.h>

/*
 * 一条客户端连接的完整状态。
 * 指针保存在 epoll_event.data.ptr 中，实现 O(1) 取用。
 */
typedef struct conn_s {
    int             fd;                 /* socket 描述符 */
    char            name[NAME_LEN];     /* 登录用户名，未登录为空串 */
    int             login;              /* 是否已登录 */
    time_t          last_active;        /* 最近一次收到数据/心跳的时间 */
    ring_buffer_t  *rb;                 /* 接收环形缓冲区，用于拆包 */
    pthread_mutex_t wlock;              /* 发送锁，串行化对本连接的写 */

    /* ===== 文件接收上下文(支持断点续传) ===== */
    FILE     *recv_fp;                  /* 正在接收的文件句柄 */
    char      recv_path[512];           /* 落盘路径 */
    uint64_t  recv_total;               /* 文件总大小 */
    uint64_t  recv_got;                 /* 已接收字节 */
} conn_t;

/* 用户表(线程安全) */
typedef struct {
    GHashTable     *table;   /* key: GINT(fd)  value: conn_t* */
    pthread_mutex_t lock;
} user_mgr_t;

/* 生命周期 */
user_mgr_t *um_create(void);
void        um_destroy(user_mgr_t *um);

/* 连接增删查（内部加锁） */
conn_t *um_add(user_mgr_t *um, int fd);          /* accept 后调用，创建 conn */
void    um_remove(user_mgr_t *um, int fd);       /* 关闭连接，释放 conn */
conn_t *um_find_by_name(user_mgr_t *um, const char *name);
int     um_name_exists(user_mgr_t *um, const char *name);

/* 在线用户名列表，逗号分隔，写入 out。返回在线人数 */
int     um_online_list(user_mgr_t *um, char *out, size_t outlen);

/*
 * 向所有已登录用户广播 buf(len 字节)。
 * except_fd 为 -1 表示发给所有人，否则跳过该 fd（通常是发送者自己）。
 */
void    um_broadcast(user_mgr_t *um, const void *buf, size_t len, int except_fd);

/* 心跳超时扫描：踢掉超过 timeout 秒没活动的连接，结果 fd 写入 dead[] */
int     um_scan_timeout(user_mgr_t *um, int timeout, int *dead, int max);

/* 线程安全地向单个连接发送数据，返回发送字节数或 -1 */
int     conn_send(conn_t *c, const void *buf, size_t len);

#endif /* USER_MANAGER_H */
