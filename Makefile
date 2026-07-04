CC     = gcc
CFLAGS = -Wall -Wextra -g -Iinclude -pthread

.PHONY: all test test_basic test_threads clean

all: test

test: test_basic test_threads

test_basic: src/allocator.c tests/test_basic.c
	$(CC) $(CFLAGS) src/allocator.c tests/test_basic.c -o test_basic
	./test_basic

test_threads: src/allocator.c tests/test_threads.c
	$(CC) $(CFLAGS) src/allocator.c tests/test_threads.c -o test_threads
	./test_threads

clean:
	rm -f test_basic test_threads