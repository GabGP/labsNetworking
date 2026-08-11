CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu99 -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lm

SRC_DIR = src
BIN_DIR = bin

TARGET = $(BIN_DIR)/ntp_client
TEST_TARGET = $(BIN_DIR)/test_ntp

SRCS = $(addprefix $(SRC_DIR)/, ntp_packet.c ntp_time.c ntp_net.c sync.c log.c main.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BIN_DIR)/%.o,$(SRCS))

TEST_SRCS = $(addprefix $(SRC_DIR)/, ntp_packet.c ntp_time.c test_ntp.c)
TEST_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BIN_DIR)/%.o,$(TEST_SRCS))

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(TEST_TARGET): $(TEST_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS) $(LDFLAGS)

$(BIN_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f $(OBJS) $(TEST_OBJS) $(TARGET) $(TEST_TARGET)
