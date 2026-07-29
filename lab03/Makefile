CC = gcc
CFLAGS = -O2 -Wall -Wextra -pthread
TARGET = dns_server
SRCS = main.c dns_server.c dns_records.c dns_parser.c threadpool.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	sudo ./$(TARGET) -p 53 -u 8.8.8.8 -t 4

run-user: $(TARGET)
	./$(TARGET) -p 9999 -u 8.8.8.8 -t 4

runjava:
	javac UdpBroadcastSender.java && java UdpBroadcastSender

clean:
	rm -f $(TARGET) $(OBJS) udp_test

.PHONY: all run run-user runjava clean