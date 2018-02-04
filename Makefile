
CFLAGS=-D BQ_USR_DEF

test: test.c src/buffer_queue.c
	gcc $(CFLAGS) -g3 -O0 -w -Isrc $^ -o $@ -lpthread -lm

clean:
	rm test