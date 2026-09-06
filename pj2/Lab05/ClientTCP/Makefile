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

# Verifica el formato del log (Requerimiento 2). No necesita root ni servidor.
test: test_log
	./test_log

test_log: test_log.c tcp_session.c ip_header.o tcp_header.o checksum.o raw_socket.o
	$(CC) $(CFLAGS) -o test_log test_log.c ip_header.o tcp_header.o checksum.o raw_socket.o $(LDFLAGS)

clean:
	rm -f *.o $(TARGET) test_log

.PHONY: all clean test