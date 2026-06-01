# ============ TCP 聊天室 Makefile ============
CC      := gcc
CFLAGS  := -Wall -Wextra -g -O2 -Iinclude $(shell pkg-config --cflags glib-2.0)
LDFLAGS := $(shell pkg-config --libs glib-2.0) -lpthread

SRC_DIR := src
BIN_DIR := bin

# 公共模块（服务端、客户端共用）
COMMON_SRC := $(SRC_DIR)/ring_buffer.c $(SRC_DIR)/protocol.c $(SRC_DIR)/net_util.c $(SRC_DIR)/log.c

# 服务端独有模块
SERVER_SRC := $(SRC_DIR)/server.c $(SRC_DIR)/user_manager.c $(SRC_DIR)/daemon.c $(COMMON_SRC)

# 客户端模块
CLIENT_SRC := $(SRC_DIR)/client.c $(COMMON_SRC)

.PHONY: all clean

all: $(BIN_DIR)/server $(BIN_DIR)/client

$(BIN_DIR)/server: $(SERVER_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/client: $(CLIENT_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BIN_DIR) *.log
	@echo "清理完成"
