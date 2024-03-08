#include "command.h"
#include "editor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void process_command(struct EditorConfig *e) {
  char *command = e->info->buffer;

  if (command[0] == 'q') {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
  } else if (command[0] == 'w') {
    save_file(e);
  }
}

void save_file(struct EditorConfig *e) {
  FILE *f = fopen(e->file_path, "w");
  int total_len = 0;

  for (int i = 0; i < e->num_rows; i++) {
    total_len += e->rows[i].len;
  }

  char *save_buffer = malloc(total_len + 1);
  if (!save_buffer) {
    perror("Failed to allocate memory");
    disable_raw_mode();
    exit(EXIT_FAILURE);
  }

  char *curr = save_buffer;
  for (int i = 0; i < e->num_rows; i++) {
    char *new_line = malloc(e->rows[i].len + 1);
    int status = sprintf(new_line, "%s", e->rows[i].buffer);
    new_line[e->rows[i].len] = '\n';
    memcpy(curr, new_line, e->rows[i].len + 1);

    free(new_line);

    curr += e->rows[i].len + 1;
  }

  fprintf(f, "%s", save_buffer);

  if (ferror(f)) {
    perror("Failed to write to file");
  }

  fclose(f);
}
