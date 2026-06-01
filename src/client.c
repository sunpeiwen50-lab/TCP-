#include "common.h"
#include "protocol.h"
#include "net_util.h"

#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>

static int             g_sock    = -1;
static volatile int    g_running = 1;
static char            g_name[NAME_LEN];
static pthread_mutex_t g_send_lock = PTHREAD_MUTEX_INITIALIZER;

/* 文件续传：等待服务端返回的偏移量 */
static pthread_mutex_t g_ack_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_ack_cond = PTHREAD_COND_INITIALIZER;
static int             g_ack_ready = 0;
static uint64_t        g_ack_offset = 0;

/* 线程安全发送 */
static int cli_send(msg_type_t type, const char *to, const void *body, uint32_t blen)
{
    char buf[PROTO_HEADER_LEN + MAX_BODY_LEN];
    int len = proto_pack(buf, type, g_name, to, body, blen);
    if (len < 0) return -1;
    pthread_mutex_lock(&g_send_lock);
    ssize_t r = writen(g_sock, buf, (size_t)len);
    pthread_mutex_unlock(&g_send_lock);
    return r == len ? 0 : -1;
}

/* ===== 接收线程 ===== */
static void *recv_thread(void *arg)
{
    UNUSED(arg);
    char body[MAX_BODY_LEN + 1];
    while (g_running) {
        proto_header_t h;
        ssize_t r = readn(g_sock, &h, PROTO_HEADER_LEN);
        if (r <= 0) {
            printf("\n[系统] 与服务器断开连接\n");
            g_running = 0;
            exit(0);
        }
        if (h.magic != PROTO_MAGIC) { printf("\n[系统] 协议错误\n"); continue; }
        uint32_t blen = h.length;
        if (blen > MAX_BODY_LEN) blen = MAX_BODY_LEN;
        if (blen > 0 && readn(g_sock, body, blen) <= 0) { g_running = 0; exit(0); }
        body[blen] = '\0';

        switch (h.type) {
        case MSG_CHAT:
            printf("\r[群聊] %s: %s\n> ", h.from, body); fflush(stdout); break;
        case MSG_PRIVATE:
            printf("\r[私聊] %s 对你说: %s\n> ", h.from, body); fflush(stdout); break;
        case MSG_NOTIFY:
            printf("\r[通知] %s\n> ", body); fflush(stdout); break;
        case MSG_ERROR:
            printf("\r[错误] %s\n> ", body); fflush(stdout); break;
        case MSG_USER_LIST:
            printf("\r[在线用户] %s\n> ", body); fflush(stdout); break;
        case MSG_HEARTBEAT_ACK:
            /* 心跳正常，无需输出 */ break;
        case MSG_FILE_ACK:
            pthread_mutex_lock(&g_ack_lock);
            g_ack_offset = (blen >= sizeof(uint64_t)) ? *(uint64_t *)body : 0;
            g_ack_ready  = 1;
            pthread_cond_signal(&g_ack_cond);
            pthread_mutex_unlock(&g_ack_lock);
            break;
        default: break;
        }
    }
    return NULL;
}

/* ===== 心跳线程 ===== */
static void *hb_thread(void *arg)
{
    UNUSED(arg);
    while (g_running) {
        sleep(HEARTBEAT_INTERVAL);
        if (!g_running) break;
        cli_send(MSG_HEARTBEAT, "", NULL, 0);
    }
    return NULL;
}

/* ===== 文件上传(支持断点续传) ===== */
static void send_file(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) { printf("[错误] 文件不存在: %s\n", path); return; }

    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    file_meta_t meta;
    memset(&meta, 0, sizeof(meta));
    strncpy(meta.filename, base, sizeof(meta.filename) - 1);
    meta.filesize = (uint64_t)st.st_size;

    /* 1. 发送传输请求，等待服务端返回续传偏移 */
    g_ack_ready = 0;
    cli_send(MSG_FILE_REQ, "", &meta, sizeof(meta));

    pthread_mutex_lock(&g_ack_lock);
    while (!g_ack_ready) pthread_cond_wait(&g_ack_cond, &g_ack_lock);
    uint64_t offset = g_ack_offset;
    pthread_mutex_unlock(&g_ack_lock);

    if (offset > 0)
        printf("[文件] 检测到断点, 从 %lu 字节处续传\n", (unsigned long)offset);

    /* 2. 从偏移处开始发送文件块 */
    FILE *fp = fopen(path, "rb");
    if (!fp) { printf("[错误] 打开文件失败\n"); return; }
    fseek(fp, (long)offset, SEEK_SET);

    char chunk[FILE_CHUNK_SIZE];
    uint64_t sent = offset;
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        if (cli_send(MSG_FILE_DATA, "", chunk, (uint32_t)n) < 0) {
            printf("[错误] 文件发送中断\n"); fclose(fp); return;
        }
        sent += n;
        printf("\r[文件] 上传进度 %lu/%lu 字节 (%.1f%%)",
               (unsigned long)sent, (unsigned long)meta.filesize,
               meta.filesize ? 100.0 * sent / meta.filesize : 100.0);
        fflush(stdout);
    }
    printf("\n");
    fclose(fp);

    /* 3. 通知传输结束 */
    cli_send(MSG_FILE_END, "", NULL, 0);
}

static void print_help(void)
{
    printf("==================== 命令帮助 ====================\n");
    printf("  直接输入文字         群聊\n");
    printf("  /to <用户名> <内容>  私聊\n");
    printf("  /who                 查看在线用户\n");
    printf("  /file <文件路径>     上传文件(自动断点续传)\n");
    printf("  /help                显示帮助\n");
    printf("  /quit                退出\n");
    printf("==================================================\n");
}

int main(int argc, char *argv[])
{
    const char *ip   = "127.0.0.1";
    int         port = SERVER_PORT;
    if (argc >= 2) ip   = argv[1];
    if (argc >= 3) port = atoi(argv[2]);

    signal(SIGPIPE, SIG_IGN);

    /* 输入用户名 */
    printf("请输入用户名: ");
    if (!fgets(g_name, sizeof(g_name), stdin)) return 1;
    g_name[strcspn(g_name, "\n")] = '\0';
    if (g_name[0] == '\0') { printf("用户名不能为空\n"); return 1; }

    /* 连接服务器 */
    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    if (connect(g_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        ERR_EXIT("connect");

    /* 登录握手（同步等待 ACK，此时接收线程尚未启动） */
    cli_send(MSG_LOGIN, "", NULL, 0);
    proto_header_t h;
    char ack[256];
    if (readn(g_sock, &h, PROTO_HEADER_LEN) <= 0) ERR_EXIT("login");
    if (h.length > 0) readn(g_sock, ack, h.length);
    ack[h.length] = '\0';
    if (strncmp(ack, "OK", 2) != 0) {
        printf("登录失败: %s\n", ack);
        close(g_sock);
        return 1;
    }
    printf("登录成功! 欢迎 %s\n", g_name);
    print_help();
    printf("> "); fflush(stdout);

    /* 启动接收线程和心跳线程 */
    pthread_t tid_recv, tid_hb;
    pthread_create(&tid_recv, NULL, recv_thread, NULL);
    pthread_create(&tid_hb,   NULL, hb_thread,   NULL);

    /* 主线程：读取键盘输入 */
    char line[MAX_BODY_LEN];
    while (g_running && fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0') { printf("> "); fflush(stdout); continue; }

        if (strcmp(line, "/quit") == 0) {
            cli_send(MSG_LOGOUT, "", NULL, 0);
            break;
        } else if (strcmp(line, "/help") == 0) {
            print_help();
        } else if (strcmp(line, "/who") == 0) {
            cli_send(MSG_USER_LIST, "", NULL, 0);
        } else if (strncmp(line, "/to ", 4) == 0) {
            char *p = line + 4;
            char *sp = strchr(p, ' ');
            if (!sp) { printf("用法: /to <用户名> <内容>\n"); }
            else {
                *sp = '\0';
                cli_send(MSG_PRIVATE, p, sp + 1, (uint32_t)strlen(sp + 1));
            }
        } else if (strncmp(line, "/file ", 6) == 0) {
            send_file(line + 6);
        } else {
            cli_send(MSG_CHAT, "", line, (uint32_t)strlen(line));
        }
        printf("> "); fflush(stdout);
    }

    g_running = 0;
    close(g_sock);
    printf("已退出\n");
    return 0;
}
