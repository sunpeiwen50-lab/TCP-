#include "user_manager.h"
#include "net_util.h"
#include "log.h"

/* glib 销毁 conn_t 的回调 */
static void conn_free(gpointer data)
{
    conn_t *c = data;
    if (!c) return;
    if (c->recv_fp) fclose(c->recv_fp);
    if (c->rb) rb_destroy(c->rb);
    pthread_mutex_destroy(&c->wlock);
    free(c);
}

user_mgr_t *um_create(void)
{
    user_mgr_t *um = calloc(1, sizeof(*um));
    if (!um) return NULL;
    /* key 直接用整数 fd，value 用 conn_free 释放 */
    um->table = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                       NULL, conn_free);
    pthread_mutex_init(&um->lock, NULL);
    return um;
}

void um_destroy(user_mgr_t *um)
{
    if (!um) return;
    g_hash_table_destroy(um->table);
    pthread_mutex_destroy(&um->lock);
    free(um);
}

conn_t *um_add(user_mgr_t *um, int fd)
{
    conn_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->fd = fd;
    c->login = 0;
    c->last_active = time(NULL);
    c->rb = rb_create(RING_BUF_SIZE);
    pthread_mutex_init(&c->wlock, NULL);
    if (!c->rb) { free(c); return NULL; }

    pthread_mutex_lock(&um->lock);
    g_hash_table_insert(um->table, GINT_TO_POINTER(fd), c);
    pthread_mutex_unlock(&um->lock);
    return c;
}

void um_remove(user_mgr_t *um, int fd)
{
    pthread_mutex_lock(&um->lock);
    g_hash_table_remove(um->table, GINT_TO_POINTER(fd));  /* 触发 conn_free */
    pthread_mutex_unlock(&um->lock);
}

conn_t *um_find_by_name(user_mgr_t *um, const char *name)
{
    conn_t *found = NULL;
    GHashTableIter it;
    gpointer k, v;
    pthread_mutex_lock(&um->lock);
    g_hash_table_iter_init(&it, um->table);
    while (g_hash_table_iter_next(&it, &k, &v)) {
        conn_t *c = v;
        if (c->login && strcmp(c->name, name) == 0) { found = c; break; }
    }
    pthread_mutex_unlock(&um->lock);
    return found;
}

int um_name_exists(user_mgr_t *um, const char *name)
{
    return um_find_by_name(um, name) != NULL;
}

int um_online_list(user_mgr_t *um, char *out, size_t outlen)
{
    int count = 0;
    size_t pos = 0;
    out[0] = '\0';
    GHashTableIter it;
    gpointer k, v;
    pthread_mutex_lock(&um->lock);
    g_hash_table_iter_init(&it, um->table);
    while (g_hash_table_iter_next(&it, &k, &v)) {
        conn_t *c = v;
        if (!c->login) continue;
        int n = snprintf(out + pos, outlen - pos,
                         "%s%s", (count ? "," : ""), c->name);
        if (n < 0 || (size_t)n >= outlen - pos) break;
        pos += (size_t)n;
        count++;
    }
    pthread_mutex_unlock(&um->lock);
    return count;
}

int conn_send(conn_t *c, const void *buf, size_t len)
{
    int ret;
    pthread_mutex_lock(&c->wlock);
    ret = (writen(c->fd, buf, len) == (ssize_t)len) ? (int)len : -1;
    pthread_mutex_unlock(&c->wlock);
    return ret;
}

void um_broadcast(user_mgr_t *um, const void *buf, size_t len, int except_fd)
{
    GHashTableIter it;
    gpointer k, v;
    /* 锁顺序：先全局锁，再在 conn_send 内加 wlock，避免死锁 */
    pthread_mutex_lock(&um->lock);
    g_hash_table_iter_init(&it, um->table);
    while (g_hash_table_iter_next(&it, &k, &v)) {
        conn_t *c = v;
        if (!c->login || c->fd == except_fd) continue;
        conn_send(c, buf, len);
    }
    pthread_mutex_unlock(&um->lock);
}

int um_scan_timeout(user_mgr_t *um, int timeout, int *dead, int max)
{
    int n = 0;
    time_t now = time(NULL);
    GHashTableIter it;
    gpointer k, v;
    pthread_mutex_lock(&um->lock);
    g_hash_table_iter_init(&it, um->table);
    while (g_hash_table_iter_next(&it, &k, &v) && n < max) {
        conn_t *c = v;
        if (now - c->last_active > timeout)
            dead[n++] = c->fd;
    }
    pthread_mutex_unlock(&um->lock);
    return n;
}
