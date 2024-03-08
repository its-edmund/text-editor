#include "editor.h"
#include "append_buffer.h"
#include "command.h"
#include <ctype.h>
#include <errno.h>
#include <ncurses.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define EDITOR_INFO_INIT ((struct EditorInfo){NULL, 0})

// struct EditorRow {
//   ssize_t size;
//   char *content;
// };
//
struct Cursor {
  int x;
  int y;
  int previous_x;
};

void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  disable_raw_mode();

  perror(s);
  exit(1);
}

void getScreenSize(int *window_height, int *window_width) {
  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

  *window_height = w.ws_row;
  *window_width = w.ws_col;
}

void disable_raw_mode(void) {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, term)) {
    die("tcsetattr");
  }
}

void enable_raw_mode(void) {
  if (tcgetattr(STDIN_FILENO, term) == -1) {
    die("tcgetattr");
  }
  atexit(disable_raw_mode);

  struct termios raw = *term;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

void editor_delete_row(struct EditorConfig *e, ssize_t index) {
  if (index < e->num_rows - 1) {
    memmove(&e->rows[index], &e->rows[index + 1],
            (e->num_rows - index + 1) * sizeof(AppendBuffer));
  }
  e->rows = realloc(e->rows, sizeof(AppendBuffer) * (e->num_rows - 1));
  e->num_rows--;
}

void editor_insert_row(struct EditorConfig *e, ssize_t index) {
  e->rows = realloc(e->rows, sizeof(AppendBuffer) * (e->num_rows + 1));

  if (e->rows == NULL) {
    die("Realloc failed");
  }

  memmove(&e->rows[index + 1], &e->rows[index],
          (e->num_rows - index) * sizeof(AppendBuffer));
  e->rows[index].len = 0;
  e->rows[index].buffer = malloc(1);
  e->rows[index].buffer[0] = '\0';
  e->num_rows++;
}

void editor_append_row(struct EditorConfig *e, char *s, ssize_t len) {
  e->rows = realloc(e->rows, sizeof(AppendBuffer) * (e->num_rows + 1));

  if (e->rows == NULL) {
    die("Realloc failed");
  }

  int at = e->num_rows;
  e->rows[at].len = (int)len;
  e->rows[at].buffer = malloc(len + 1);
  memcpy(e->rows[at].buffer, s, len);
  e->rows[at].buffer[len] = '\0';
  e->num_rows++;
}

void editor_draw_rows(struct EditorConfig *e, AppendBuffer *ab) {
  AppendBuffer *row;
  ssize_t row_number;

  for (row_number = 0; row_number < e->window_height; row_number++) {
    if (row_number < e->num_rows) {
      row = &e->rows[row_number];
      char *line = NULL;
      int size;
      size = asprintf(&line, "%d  %s\r\n", (int)row_number + 1, row->buffer);
      ab_append(ab, line, size);
    } else if (row_number == e->window_height - 1) {
      if (e->info->len > 0) {
        ab_append(ab, e->info->buffer, 6);
      } else if (e->mode == NORMAL) {
        ab_append(ab, "NORMAL", 6);
      } else if (e->mode == INSERT) {
        ab_append(ab, "INSERT", 6);
      } else if (e->mode == COMMAND) {
        char *command;
        int command_len = asprintf(&command, ":%s", e->info->buffer);
        ab_append(ab, command, command_len);
      }
    } else {
      ab_append(ab, "~\r\n", 3);
    }
  }
}

void editor_open(struct EditorConfig *e, char *path) {
  FILE *f = fopen(path, "r");

  if (f == NULL) {
    perror("File open error");
  }

  char *line = NULL;
  size_t len = 0;
  ssize_t size;

  while ((size = getline(&line, &len, f)) != -1) {
    while (size > 0 && (line[size - 1] == '\n' || line[size - 1] == '\r')) {
      size--;
    }
    editor_append_row(e, line, size);
  }

  e->cursor->y = 0;
  e->cursor->x = 0;

  if (e->num_rows > 0) {
    e->cursor->y = e->num_rows - 1;
    e->cursor->x = (int)e->rows[e->num_rows - 1].len;
  }
  e->cursor->previous_x = e->cursor->x;

  fclose(f);
  if (line) {
    free(line);
  }
}

void editor_refresh_screen(struct EditorConfig *e) {
  AppendBuffer ab = APPEND_BUFFER_INIT;

  ab_append(&ab, "\x1b[2J", 4);
  ab_append(&ab, "\x1b[H", 3);
  editor_draw_rows(e, &ab);
  write(STDOUT_FILENO, ab.buffer, ab.len);
  char *cursor_position;
  size_t len;

  if (e->mode == INSERT) {
    write(STDOUT_FILENO, "\033[6 q", 5);
  } else {
    write(STDOUT_FILENO, "\033[2 q", 5);
  }
  len = asprintf(&cursor_position, "\033[%d;%dH", e->cursor->y + 1,
                 e->cursor->x + 4);
  write(STDOUT_FILENO, cursor_position, len);

  ab_free(&ab);
}

char read_keypress(void) {
  ssize_t nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }
  return c;
}

void process_keypress(struct EditorConfig *e) {
  char c = read_keypress();

  if (e->mode == NORMAL) {
    switch (c) {
    case CTRL_KEY('q'):
      exit(0);
      break;
    case 'i':
      e->mode = INSERT;
      break;
    case 'a':
      e->cursor->x++;
      e->mode = INSERT;
      break;
    case ':':
      e->mode = COMMAND;
      ab_reset(e->info);
      break;
    case 'j':
      if (e->cursor->y < e->num_rows - 1) {
        if (e->rows[e->cursor->y + 1].len < e->cursor->previous_x) {
          e->cursor->x = e->rows[e->cursor->y + 1].len;
        } else {
          e->cursor->x = e->cursor->previous_x;
        }
        e->cursor->y++;
      }
      break;
    case 'k':
      if (e->cursor->y > 0) {
        if (e->rows[e->cursor->y - 1].len < e->cursor->previous_x) {
          e->cursor->x = e->rows[e->cursor->y - 1].len;
        } else {
          e->cursor->x = e->cursor->previous_x;
        }
        e->cursor->y--;
      }
      break;
    case 'h':
      if (e->cursor->x > 0) {
        e->cursor->x--;
        e->cursor->previous_x = e->cursor->x;
      }
      break;
    case 'l':
      if (e->cursor->x < e->rows[e->cursor->y].len) {
        e->cursor->x++;
        e->cursor->previous_x = e->cursor->x;
      }
      break;
    }
  } else if (e->mode == INSERT) {
    switch (c) {
    case '\r':
    case '\n':
      editor_insert_row(e, e->cursor->y + 1);
      e->cursor->y++;
      e->cursor->x = 0;
      break;
    case '\033':
      if (e->cursor->x > 0) {

        e->cursor->x--;
      }
      e->mode = NORMAL;
      break;
    case '\b':
    case '\x7f':
      if (e->cursor->x == 0) {
        if (e->cursor->y > 0) {
          editor_delete_row(e, e->cursor->y);
          e->cursor->y--;
        }
      } else {
        ab_remove(&e->rows[e->cursor->y]);
        e->cursor->x--;
      }
      break;
    default:
      ab_append(&e->rows[e->cursor->y], (const char[2]){c, '\0'}, 1);
      e->cursor->x++;
      e->cursor->previous_x = e->cursor->x;
      break;
    }
  } else if (e->mode == COMMAND) {
    switch (c) {
    case '\r':
    case '\n':
      process_command(e);
      e->mode = NORMAL;
      break;
    case '\033':
      e->mode = NORMAL;
      ab_reset(e->info);
      break;
    case '\b':
    case '\x7f':
      ab_remove(e->info);
      break;
    default:
      ab_append(e->info, (const char[2]){c, '\0'}, 1);
      break;
    }
  }
  // if (iscntrl(c)) {
  //   printf("%d\r\n", c);
  // } else {
  //   printf("%d ('%c')\r\n", c, c);
  // }
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    printf("Usage: type [FILE]");
    return 0;
  }

  static struct EditorConfig *e;
  term = malloc(sizeof(struct termios));

  e = malloc(sizeof(struct EditorConfig));
  e->num_rows = 0;
  e->rows = NULL;
  e->cursor = malloc(sizeof(struct Cursor));
  e->cursor->x = 0;
  e->cursor->y = 0;
  e->cursor->previous_x = 0;
  e->info = malloc(sizeof(AppendBuffer));
  e->info->buffer = malloc(1);
  e->info->buffer[0] = '\0';
  e->info->len = 0;
  e->mode = NORMAL;
  e->file_path = argv[1];

  enable_raw_mode();
  editor_open(e, argv[1]);
  getScreenSize(&e->window_height, &e->window_width);

  while (1) {
    editor_refresh_screen(e);
    process_keypress(e);
  }

  // fwrite(buffer, strlen(buffer), 1, f);
  // fclose(f);

  return 0;
}
