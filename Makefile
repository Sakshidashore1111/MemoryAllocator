CC     = gcc
CFLAGS = -Wall -Wextra -g -Iinclude

all: test

test: src/allocator.c tests/test_basic.c
	$(CC) $(CFLAGS) src/allocator.c tests/test_basic.c -o test_basic
	./test_basic

clean:
	rm -f test_basic