#include "common.h"
#include "protocol.h"
#include "user_manager.h"
#include "net_util.h"
#include "daemon.h"
#include "log.h"

#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>
#include <getopt.h>
#include <glib.h>

/* ===== 服务器上下文：作为线程池 user_data 共享 ===== */
typedef struct {
    int          epfd;        /* epoll 实例 */
    int          listen_fd;   /* 监听 socket */
    user_mgr_t  *um;          /* 用户表 */
    GThreadPool *pool;        /* glib 线程池 */
} server_ctx_t;

static volatile sig_atomic_t g_running = 1;

/* 每个工作线程独立的打包/收包缓冲，避免竞争 */
static __thread char t_packbuf[PROTO_HEADER_LEN + MAX_BODY_LEN];
static __thread char t_msgbuf [PROTO_HEADER_LEN + MAX_BODY_LEN];

static void on_signal(int sig) { UNUSED(sig); g_running = 0; }

/* 打包并发送一条消息给单个连接 */
static int s_send(conn_t *c, msg_type_t type, const char *from,
                  const char *to, const void *body, uint32_t blen)
{
    int len = proto_pack(t_packbuf, type, from, to, body, blen);
    if (len < 0) return -1;
    return conn_send(c, t_packbuf, (size_t)len);
}

/* ============ 业务处理 ============ */

static void handle_login(server_ctx_t *ctx, conn_t *c, proto_header_t *h)
{
    if (h->from[0] == '\0') {
        s_send(c, MSG_LOGIN_ACK, "server", "", "ERR:用户名为空", 18);
        return;
    }
    if (um_name_exists(ctx->um, h->from)) {
        s_send(c, MSG_LOGIN_ACK, "server", "", "ERR:用户名已被占用", 0 +
               (uint32_t)strlen("ERR:用户名已被占用"));
        return;
    }
    strncpy(c->name, h->from, NAME_LEN - 1);
    c->login = 1;
    s_send(c, MSG_LOGIN_ACK, "server", c->name, "OK", 2);
    LOG_I("用户 [%s] 登录, fd=%d", c->name, c->fd);

    /* 广播上线通知 */
    char notify[128];
    int n = snprintf(notify, sizeof(notify), "%s 进入了聊天室", c->name);
    um_broadcast(ctx->um, t_packbuf,
                 (size_t)proto_pack(t_packbuf, MSG_NOTIFY, "server", "", notify, (uint32_t)n),
                 c->fd);
}

static void handle_chat(server_ctx_t *ctx, conn_t *c, const char *body, uint32_t blen)
{
    if (!c->login) return;
    int len = proto_pack(t_packbuf, MSG_CHAT, c->name, "", body, blen);
    um_broadcast(ctx->um, t_packbuf, (size_t)len, c->fd);   /* 群聊不回发自己 */
    LOG_I("[群聊] %s: %.*s", c->name, (int)blen, body);
}

static void handle_private(server_ctx_t *ctx, conn_t *c, proto_header_t *h,
                           const char *body, uint32_t blen)
{
    if (!c->login) return;
    conn_t *target = um_find_by_name(ctx->um, h->to);
    if (!target) {
        char err[128];
        int n = snprintf(err, sizeof(err), "用户 [%s] 不在线", h->to);
        s_send(c, MSG_ERROR, "server", c->name, err, (uint32_t)n);
        return;
    }
    s_send(target, MSG_PRIVATE, c->name, h->to, body, blen);
    LOG_I("[私聊] %s -> %s: %.*s", c->name, h->to, (int)blen, body);
}

static void handle_user_list(server_ctx_t *ctx, conn_t *c)
{
    char list[MAX_BODY_LEN];
    int cnt = um_online_list(ctx->um, list, sizeof(list));
    s_send(c, MSG_USER_LIST, "server", c->name, list, (uint32_t)strlen(list));
    LOG_D("发送在线列表给 %s, 共 %d 人", c->name, cnt);
}

static void handle_file_req(conn_t *c, const char *body, uint32_t blen)
{
    if (blen < sizeof(file_meta_t)) return;
    const file_meta_t *meta = (const file_meta_t *)body;

    /* 防目录穿越：只取 basename */
    const char *base = strrchr(meta->filename, '/');
    base = base ? base + 1 : meta->filename;

    snprintf(c->recv_path, sizeof(c->recv_path), "%s/%s", FILE_STORE_DIR, base);
    c->recv_total = meta->filesize;

    /* 断点续传：查看本地已存在的大小 */
    struct stat st;
    uint64_t existing = 0;
    if (stat(c->recv_path, &st) == 0)
        existing = (uint64_t)st.st_size;
    if (existing > c->recv_total) existing = 0;   /* 异常则重传 */
    c->recv_got = existing;

    /* 以 r+（存在则在中间写）或 w（新建）打开，定位到续传偏移 */
    c->recv_fp = fopen(c->recv_path, existing ? "r+b" : "wb");
    if (!c->recv_fp) {
        s_send(c, MSG_ERROR, "server", c->name, "服务端无法创建文件", 24);
        return;
    }
    fseek(c->recv_fp, (long)existing, SEEK_SET);

    /* 把已接收偏移返回给客户端，客户端据此续传 */
    s_send(c, MSG_FILE_ACK, "server", c->name, &existing, sizeof(existing));
    LOG_I("接收文件 [%s] 总大小=%lu 续传起点=%lu",
          base, (unsigned long)c->recv_total, (unsigned long)existing);
}

static void handle_file_data(conn_t *c, const char *body, uint32_t blen)
{
    if (!c->recv_fp) return;
    fwrite(body, 1, blen, c->recv_fp);
    c->recv_got += blen;
}

static void handle_file_end(conn_t *c)
{
    if (!c->recv_fp) return;
    fflush(c->recv_fp);
    fclose(c->recv_fp);
    c->recv_fp = NULL;
    LOG_I("文件接收完成 [%s] %lu/%lu 字节",
          c->recv_path, (unsigned long)c->recv_got, (unsigned long)c->recv_total);
    s_send(c, MSG_NOTIFY, "server", c->name, "文件传输完成", 18);
}

/* 处理一条完整消息，返回 0 正常，-1 表示需断开 */
static int dispatch(server_ctx_t *ctx, conn_t *c, proto_header_t *h,
                    const char *body, uint32_t blen)
{
    switch (h->type) {
    case MSG_LOGIN:      handle_login(ctx, c, h);                break;
    case MSG_LOGOUT:     return -1;
    case MSG_CHAT:       handle_chat(ctx, c, body, blen);        break;
    case MSG_PRIVATE:    handle_private(ctx, c, h, body, blen);  break;
    case MSG_USER_LIST:  handle_user_list(ctx, c);               break;
    case MSG_HEARTBEAT:  s_send(c, MSG_HEARTBEAT_ACK, "server", c->name, NULL, 0); break;
    case MSG_FILE_REQ:   handle_file_req(c, body, blen);         break;
    case MSG_FILE_DATA:  handle_file_data(c, body, blen);        break;
    case MSG_FILE_END:   handle_file_end(c);                     break;
    default:
        LOG_W("未知消息类型 %d (fd=%d)", h->type, c->fd);
        break;
    }
    return 0;
}

/* 从环形缓冲区中拆出所有完整消息并分发。返回 0 正常，-1 需断开 */
static int parse_buffer(server_ctx_t *ctx, conn_t *c)
{
    while (1) {
        if (rb_used(c->rb) < PROTO_HEADER_LEN) break;   /* 包头不全 */

        proto_header_t h;
        rb_peek(c->rb, &h, PROTO_HEADER_LEN);
        if (h.magic != PROTO_MAGIC) {
            LOG_W("非法魔数 0x%x, 断开 fd=%d", h.magic, c->fd);
            return -1;
        }
        if (h.length > MAX_BODY_LEN) {
            LOG_W("body 过长 %u, 断开 fd=%d", h.length, c->fd);
            return -1;
        }
        size_t total = PROTO_HEADER_LEN + h.length;
        if (rb_used(c->rb) < total) break;              /* 整包未到齐 */

        rb_read(c->rb, t_msgbuf, total);                /* 取出整包 */
        proto_header_t *ph = (proto_header_t *)t_msgbuf;
        char *body = t_msgbuf + PROTO_HEADER_LEN;
        if (dispatch(ctx, c, ph, body, ph->length) < 0)
            return -1;
    }
    return 0;
}

/* 清理并关闭一条连接 */
static void close_conn(server_ctx_t *ctx, conn_t *c)
{
    epoll_ctl(ctx->epfd, EPOLL_CTL_DEL, c->fd, NULL);
    close(c->fd);

    char name[NAME_LEN];
    int was_login = c->login;
    strncpy(name, c->name, NAME_LEN);
    int fd = c->fd;

    um_remove(ctx->um, fd);   /* 此后 c 已被释放，不可再访问 */

    if (was_login) {
        LOG_I("用户 [%s] 下线, fd=%d", name, fd);
        char notify[128];
        int n = snprintf(notify, sizeof(notify), "%s 离开了聊天室", name);
        um_broadcast(ctx->um, t_packbuf,
                     (size_t)proto_pack(t_packbuf, MSG_NOTIFY, "server", "", notify, (uint32_t)n),
                     -1);
    }
}

/* ===== 线程池工作函数：处理一个可读连接 ===== */
static void worker(gpointer data, gpointer user_data)
{
    conn_t       *c   = data;
    server_ctx_t *ctx = user_data;

    int dead = 0;
    char tmp[16 * 1024];

    /* 非阻塞读，把内核数据全部搬进环形缓冲区 */
    while (1) {
        ssize_t n = read(c->fd, tmp, sizeof(tmp));
        if (n > 0) {
            c->last_active = time(NULL);
            /* 缓冲区可能不足，先解析腾空间再写 */
            if ((size_t)n > rb_free(c->rb)) {
                if (parse_buffer(ctx, c) < 0) { dead = 1; break; }
            }
            if (rb_write(c->rb, tmp, (size_t)n) < 0) {
                LOG_W("环形缓冲溢出, fd=%d", c->fd);
                dead = 1; break;
            }
        } else if (n == 0) {
            dead = 1; break;                 /* 对端关闭 */
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;                           /* 数据读完 */
        } else if (errno == EINTR) {
            continue;
        } else {
            dead = 1; break;                 /* 读错误 */
        }
    }

    if (!dead && parse_buffer(ctx, c) < 0) dead = 1;

    if (dead) {
        close_conn(ctx, c);
        return;
    }

    /* 重新武装 EPOLLONESHOT，等待下一次事件 */
    struct epoll_event ev;
    ev.events   = EPOLLIN | EPOLLONESHOT;
    ev.data.ptr = c;
    epoll_ctl(ctx->epfd, EPOLL_CTL_MOD, c->fd, &ev);
}

/* ===== 接受新连接 ===== */
static void do_accept(server_ctx_t *ctx)
{
    while (1) {
        struct sockaddr_in cli;
        socklen_t len = sizeof(cli);
        int fd = accept(ctx->listen_fd, (struct sockaddr *)&cli, &len);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            LOG_E("accept 失败: %s", strerror(errno));
            break;
        }
        set_nonblock(fd);
        conn_t *c = um_add(ctx->um, fd);
        if (!c) { close(fd); continue; }

        struct epoll_event ev;
        ev.events   = EPOLLIN | EPOLLONESHOT;
        ev.data.ptr = c;
        epoll_ctl(ctx->epfd, EPOLL_CTL_ADD, fd, &ev);
        LOG_I("新连接 fd=%d 来自 %s:%d", fd,
              inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));
    }
}

/* ===== 心跳检测线程：定期把超时连接 shutdown，由事件循环统一回收 ===== */
static void *heartbeat_thread(void *arg)
{
    server_ctx_t *ctx = arg;
    while (g_running) {
        sleep(HEARTBEAT_INTERVAL);
        int dead[256];
        int n = um_scan_timeout(ctx->um, HEARTBEAT_TIMEOUT, dead, 256);
        for (int i = 0; i < n; i++) {
            LOG_W("心跳超时, 关闭 fd=%d", dead[i]);
            shutdown(dead[i], SHUT_RDWR);   /* 触发可读事件 -> worker 读到 0 -> 回收 */
        }
    }
    return NULL;
}

static int create_listen_socket(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
    if (listen(fd, BACKLOG) < 0) { close(fd); return -1; }
    set_nonblock(fd);
    return fd;
}

int main(int argc, char *argv[])
{
    int  port = SERVER_PORT;
    int  as_daemon = 0;
    const char *logfile = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "p:dl:h")) != -1) {
        switch (opt) {
        case 'p': port = atoi(optarg); break;
        case 'd': as_daemon = 1;       break;
        case 'l': logfile = optarg;    break;
        case 'h':
        default:
            printf("用法: %s [-p 端口] [-d 守护进程] [-l 日志文件]\n", argv[0]);
            return 0;
        }
    }

    if (as_daemon) {
        if (!logfile) logfile = "chatroom.log";
        if (daemonize() < 0) { fprintf(stderr, "daemonize 失败\n"); return 1; }
    }
    log_init(logfile, LOG_DEBUG);     /* 守护进程写文件，前台写 stderr */

    signal(SIGPIPE, SIG_IGN);         /* 防止向已关闭 socket 写导致进程退出 */
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    mkdir(FILE_STORE_DIR, 0755);      /* 确保文件存储目录存在 */

    server_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.listen_fd = create_listen_socket(port);
    if (ctx.listen_fd < 0) { LOG_E("监听 %d 失败: %s", port, strerror(errno)); return 1; }
    ctx.epfd = epoll_create1(0);
    ctx.um   = um_create();

    GError *gerr = NULL;
    ctx.pool = g_thread_pool_new(worker, &ctx, THREAD_POOL_NUM, FALSE, &gerr);
    if (!ctx.pool) { LOG_E("线程池创建失败: %s", gerr->message); return 1; }

    /* 监听 fd 用 data.ptr=&ctx 作为哨兵区分 */
    struct epoll_event ev;
    ev.events   = EPOLLIN;            /* 监听 fd 用水平触发，主线程直接 accept */
    ev.data.ptr = &ctx;
    epoll_ctl(ctx.epfd, EPOLL_CTL_ADD, ctx.listen_fd, &ev);

    pthread_t hb;
    pthread_create(&hb, NULL, heartbeat_thread, &ctx);

    LOG_I("服务器启动, 端口=%d, 线程池=%d, 模式=%s",
          port, THREAD_POOL_NUM, as_daemon ? "守护进程" : "前台");

    struct epoll_event events[MAX_EVENTS];
    while (g_running) {
        int nfds = epoll_wait(ctx.epfd, events, MAX_EVENTS, 1000);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            LOG_E("epoll_wait: %s", strerror(errno));
            break;
        }
        for (int i = 0; i < nfds; i++) {
            if (events[i].data.ptr == &ctx) {
                do_accept(&ctx);                         /* 监听 fd */
            } else {
                conn_t *c = events[i].data.ptr;          /* 客户端 fd */
                g_thread_pool_push(ctx.pool, c, NULL);   /* 交给线程池 */
            }
        }
    }

    LOG_I("服务器正在关闭...");
    g_running = 0;
    pthread_join(hb, NULL);
    g_thread_pool_free(ctx.pool, FALSE, TRUE);
    um_destroy(ctx.um);
    close(ctx.listen_fd);
    close(ctx.epfd);
    log_close();
    return 0;
}
