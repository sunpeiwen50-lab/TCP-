#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include "common.h"

/*
 * 环形缓冲区(循环队列)
 *  - 用一段连续内存模拟首尾相接的环
 *  - head 读指针，tail 写指针，used 当前已用字节数
 *  - 不区分满/空歧义：用 used 字段判断
 */
typedef struct {
    char  *buf;     /* 数据区 */
    size_t size;    /* 容量 */
    size_t head;    /* 读位置 */
    size_t tail;    /* 写位置 */
    size_t used;    /* 已使用字节数 */
} ring_buffer_t;

/* 创建/销毁 */
ring_buffer_t *rb_create(size_t size);
void           rb_destroy(ring_buffer_t *rb);

/* 状态查询 */
size_t rb_used(const ring_buffer_t *rb);   /* 已用字节 */
size_t rb_free(const ring_buffer_t *rb);   /* 空闲字节 */

/*
 * 写入 len 字节。成功返回 len，空间不足返回 -1(不做部分写入)。
 */
int rb_write(ring_buffer_t *rb, const void *data, size_t len);

/*
 * 从读指针“偷看”len 字节到 out，但不移动读指针。
 * 数据不足返回 -1。用于先读包头判断整包是否到齐。
 */
int rb_peek(const ring_buffer_t *rb, void *out, size_t len);

/*
 * 真正读取 len 字节到 out，并前移读指针。数据不足返回 -1。
 */
int rb_read(ring_buffer_t *rb, void *out, size_t len);

/* 丢弃(跳过)len 字节 */
int rb_drop(ring_buffer_t *rb, size_t len);

#endif /* RING_BUFFER_H */
