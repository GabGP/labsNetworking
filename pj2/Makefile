CC = gcc
CFLAGS = -Wall -Wextra -O2
TARGET = traceroute
OBJS = src/traceroute.o src/packet.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

src/%.o: src/%.c src/packet.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
