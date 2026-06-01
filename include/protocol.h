#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "common.h"

/*
 * ============ 自定义应用层协议 ============
 *
 *  +----------------------------------------------------+
 *  |                  proto_header_t                    |
 *  |  magic(2) type(2) length(4) from(32) to(32)        |  固定 72 字节
 *  +----------------------------------------------------+
 *  |                  body (length 字节)                 |  变长
 *  +----------------------------------------------------+
 *
 *  - magic  : 魔数，用于校验包的合法性、快速定位包边界
 *  - type   : 消息类型，见 msg_type_t
 *  - length : body 的字节数（不含包头），解决 TCP 粘包/半包
 *  - from   : 发送方用户名
 *  - to     : 接收方用户名（群聊为空，私聊/文件为目标用户）
 *  - body   : 具体负载（文本、用户列表、文件块等）
 */

#define PROTO_MAGIC  0x4348u   /* 'C''H' = ChatRoom */

/* 消息类型 */
typedef enum {
    MSG_LOGIN = 1,      /* C->S 登录请求，body=空，from=用户名         */
    MSG_LOGIN_ACK,      /* S->C 登录结果，body="OK" 或 错误原因         */
    MSG_LOGOUT,         /* C->S 主动退出                              */
    MSG_CHAT,           /* C->S->C 群聊，body=文本                     */
    MSG_PRIVATE,        /* C->S->C 私聊，to=目标用户，body=文本         */
    MSG_USER_LIST,      /* C->S 请求 / S->C 返回在线列表(body=用逗号分隔)*/
    MSG_HEARTBEAT,      /* C->S 心跳                                  */
    MSG_HEARTBEAT_ACK,  /* S->C 心跳应答                              */
    MSG_FILE_REQ,       /* C->S 文件传输请求，body=file_meta_t          */
    MSG_FILE_ACK,       /* S->C 允许传输，body=已存在的字节数(断点续传)   */
    MSG_FILE_DATA,      /* C->S 文件数据块，body=原始字节               */
    MSG_FILE_END,       /* C->S 文件传输结束                           */
    MSG_NOTIFY,         /* S->C 系统通知(上线/下线等)，body=文本         */
    MSG_ERROR,          /* S->C 错误信息，body=文本                     */
} msg_type_t;

/* 协议头：定长，强制 1 字节对齐，方便跨平台收发 */
#pragma pack(push, 1)
typedef struct {
    uint16_t magic;          /* 魔数 PROTO_MAGIC */
    uint16_t type;           /* msg_type_t */
    uint32_t length;         /* body 长度 */
    char     from[NAME_LEN]; /* 发送方 */
    char     to[NAME_LEN];   /* 接收方 */
} proto_header_t;

/* 文件传输元信息，作为 MSG_FILE_REQ 的 body */
typedef struct {
    char     filename[256];  /* 文件名(不含路径) */
    uint64_t filesize;       /* 文件总大小 */
} file_meta_t;
#pragma pack(pop)

#define PROTO_HEADER_LEN  (sizeof(proto_header_t))

/*
 * 组装一条完整协议消息到 out 缓冲区。
 * 返回写入的总字节数(头+体)，失败返回 -1。
 * out 需保证至少 PROTO_HEADER_LEN + body_len 字节。
 */
int proto_pack(char *out, msg_type_t type,
               const char *from, const char *to,
               const void *body, uint32_t body_len);

#endif /* PROTOCOL_H */
