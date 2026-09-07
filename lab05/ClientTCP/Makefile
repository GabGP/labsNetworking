CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS =

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	LDFLAGS += -lpcap
endif

ifeq ($(shell id -u), 0)
	SUDO ?=
else
	SUDO ?= sudo
endif

SRC_DIR = src
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin

TARGET = $(BIN_DIR)/tcp_client
TEST_TARGET = $(BIN_DIR)/test_log

SRCS = $(addprefix $(SRC_DIR)/, tcp_client.c tcp_session.c ip_header.c tcp_header.c checksum.c raw_socket.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

TEST_SRCS = $(addprefix $(SRC_DIR)/, test_log.c ip_header.c tcp_header.c checksum.c raw_socket.c)
TEST_OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(TEST_SRCS))

.PHONY: all clean test run

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)
	@cp -f $@ tcp_client

$(TEST_TARGET): $(TEST_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS) $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/test_log.o: $(SRC_DIR)/tcp_session.c

# Ejecuta el cliente TCP. En Linux/macOS los raw sockets requieren privilegios (root/sudo).
# Permite pasar argumentos personalizados: make run ARGS="-s 127.0.0.1 -p 5001"
run: $(TARGET)
	$(SUDO) ./$(TARGET) $(ARGS)

# Verifica el formato del log (Requerimiento 2). No necesita root ni servidor.
test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -rf $(BUILD_DIR)
	rm -f *.o tcp_client test_log