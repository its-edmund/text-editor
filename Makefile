type: editor.c
	$(CC) editor.c append_buffer.c command.c -o editor -Wall -Wextra -pedantic -std=c99 -g
