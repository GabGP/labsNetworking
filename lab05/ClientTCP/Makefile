CC = gcc
CFLAGS = -Wall -Wextra -g
TARGET = tcp_client
OBJS = tcp_client.o tcp_session.o ip_header.o tcp_header.o checksum.o raw_socket.o

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	LDFLAGS += -lpcap
endif

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(TARGET)

.PHONY: all clean