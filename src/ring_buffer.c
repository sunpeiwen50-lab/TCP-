#include "ring_buffer.h"

ring_buffer_t *rb_create(size_t size)
{
    ring_buffer_t *rb = calloc(1, sizeof(*rb));
    if (!rb) return NULL;
    rb->buf = malloc(size);
    if (!rb->buf) {
        free(rb);
        return NULL;
    }
    rb->size = size;
    rb->head = rb->tail = rb->used = 0;
    return rb;
}

void rb_destroy(ring_buffer_t *rb)
{
    if (!rb) return;
    free(rb->buf);
    free(rb);
}

size_t rb_used(const ring_buffer_t *rb) { return rb->used; }
size_t rb_free(const ring_buffer_t *rb) { return rb->size - rb->used; }

int rb_write(ring_buffer_t *rb, const void *data, size_t len)
{
    if (len > rb_free(rb)) return -1;             /* 空间不足 */

    const char *p = data;
    /* tail 到缓冲区末尾的连续空间 */
    size_t tail_to_end = rb->size - rb->tail;
    if (len <= tail_to_end) {
        memcpy(rb->buf + rb->tail, p, len);       /* 一次拷贝 */
    } else {
        memcpy(rb->buf + rb->tail, p, tail_to_end);            /* 先填到末尾 */
        memcpy(rb->buf, p + tail_to_end, len - tail_to_end);   /* 再绕回开头 */
    }
    rb->tail = (rb->tail + len) % rb->size;
    rb->used += len;
    return (int)len;
}

int rb_peek(const ring_buffer_t *rb, void *out, size_t len)
{
    if (len > rb->used) return -1;                /* 数据不足 */

    char  *p = out;
    size_t head_to_end = rb->size - rb->head;
    if (len <= head_to_end) {
        memcpy(p, rb->buf + rb->head, len);
    } else {
        memcpy(p, rb->buf + rb->head, head_to_end);
        memcpy(p + head_to_end, rb->buf, len - head_to_end);
    }
    return (int)len;
}

int rb_drop(ring_buffer_t *rb, size_t len)
{
    if (len > rb->used) return -1;
    rb->head = (rb->head + len) % rb->size;
    rb->used -= len;
    return (int)len;
}

int rb_read(ring_buffer_t *rb, void *out, size_t len)
{
    if (rb_peek(rb, out, len) < 0) return -1;
    rb_drop(rb, len);
    return (int)len;
}
