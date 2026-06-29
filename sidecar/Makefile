CC      = cc
CFLAGS  = -Wall -Wextra -O2 -I. $(shell pkg-config --cflags openssl 2>/dev/null)
LDFLAGS = $(shell pkg-config --libs openssl 2>/dev/null || echo "-lssl -lcrypto")
SRCS    = main.c ur/hashcons.c ur/bitstream.c ur/serial.c ur/murmur3.c
OBJS    = $(SRCS:.c=.o)
TARGET  = tcp-sidecar

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET) stress-test stress-test.o

asan: clean
	$(MAKE) CFLAGS="-Wall -Wextra -O0 -g -fsanitize=address -I. $(shell pkg-config --cflags openssl 2>/dev/null)" LDFLAGS="-fsanitize=address $(shell pkg-config --libs openssl 2>/dev/null || echo '-lssl -lcrypto')"

stress-test: stress-test.o ur/hashcons.o ur/bitstream.o ur/serial.o ur/murmur3.o
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: clean asan
