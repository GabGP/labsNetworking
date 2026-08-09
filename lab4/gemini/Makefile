CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu99 -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lm

TARGET = ntp_client
TEST_TARGET = test_ntp

SRCS = ntp_packet.c ntp_time.c ntp_net.c sync.c log.c main.c
OBJS = $(SRCS:.c=.o)

TEST_SRCS = ntp_packet.c ntp_time.c test_ntp.c
TEST_OBJS = $(TEST_SRCS:.c=.o)

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(TEST_TARGET): $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f $(OBJS) $(TEST_OBJS) $(TARGET) $(TEST_TARGET)
