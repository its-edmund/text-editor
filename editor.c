#include "append_buffer.h"
#include <ctype.h>
#include <errno.h>
#include <ncurses.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define EDITOR_INFO_INIT ((struct EditorInfo){NULL, 0})

enum editorMode {
  NORMAL,
  INSERT,
  COMMAND
};

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


struct EditorConfig {
  struct termios term;
  AppendBuffer *rows;
  AppendBuffer *info;
  struct Cursor *cursor;
  enum editorMode mode;
  int num_rows;
  int window_height;
  int window_width;
};

struct EditorConfig state;

void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

void getScreenSize(int *window_height, int *window_width) {
  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  
  *window_height = w.ws_row;
  *window_width = w.ws_col;
}

void disableRawMode() { 
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &state.term)) {
    die("tcsetattr");
  }
}

void enable_raw_mode() {
  if (tcgetattr(STDIN_FILENO, &state.term) == -1) {
    die("tcgetattr");
  }
  atexit(disableRawMode);

  struct termios raw = state.term;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

void editor_append_row(char *s, ssize_t len) {
  state.rows = realloc(state.rows, sizeof(AppendBuffer) * (state.num_rows + 1));

  if (state.rows == NULL) {
    die("Realloc failed");
  }

  int at = state.num_rows;
  state.rows[at].len = (int) len;
  state.rows[at].buffer = malloc(len + 1);
  memcpy(state.rows[at].buffer, s, len);
  state.rows[at].buffer[len] = '\0';
  state.num_rows++;
}

void editor_draw_rows(AppendBuffer *ab) {
  AppendBuffer *row;
  ssize_t row_number;
  
  for (row_number = 0; row_number < state.window_height; row_number++) {
    if (row_number < state.num_rows) {
      row = &state.rows[row_number];
      char *line = NULL;
      int size;
      size = asprintf(&line, "%d  %s\r\n", (int) row_number + 1, row->buffer);
      ab_append(ab, line, size);
    } else if (row_number == state.window_height - 1) {
      if (state.info->len > 0) {
        ab_append(ab, state.info->buffer, 6);
      } else if (state.mode == NORMAL) {
        ab_append(ab, "NORMAL", 6);
      } else if (state.mode == INSERT) {
        ab_append(ab, "INSERT", 6);
      } else if (state.mode == COMMAND) {
        char *command;
        int command_len = asprintf(&command, ":%s", state.info->buffer);
        ab_append(ab, command, command_len);
      }
    } else {
      ab_append(ab, "~\r\n", 4);
    }
  }
}

void editor_open(char *path) {
  FILE *f = fopen(path, "r");

  if (f == NULL) {
    perror("File open error");
  }

  char *line = NULL;
  size_t len = 0;
  ssize_t size;

  while ((size = getline(&line, &len, f)) != -1) {
    while(size > 0 && (line[size - 1] == '\n' || line[size - 1] == '\r')) {
      size--;
    }
    editor_append_row(line, size);
  }

  state.cursor->y = state.num_rows - 1;
  state.cursor->x = (int) state.rows[state.num_rows - 1].len + 2;

  fclose(f);
  if (line) {
    free(line);
  }
}

void editor_refresh_screen() {
  AppendBuffer ab = APPEND_BUFFER_INIT;

  ab_append(&ab, "\x1b[2J", 4);
  ab_append(&ab, "\x1b[H", 3);
  editor_draw_rows(&ab);
  write(STDOUT_FILENO, ab.buffer, ab.len);
  char *cursor_position;
  size_t len;

  if (state.mode == INSERT) {
    write(STDOUT_FILENO, "\033[6 q", 5);
  } else {
    write(STDOUT_FILENO, "\033[2 q", 5);
  }
  len = asprintf(&cursor_position, "\033[%d;%dH", state.cursor->y + 1, state.cursor->x + 4);
  write(STDOUT_FILENO, cursor_position, len);

  ab_free(&ab);
}

char read_keypress() {
  ssize_t nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }
  return c;
}

void process_keypress() {
  char c = read_keypress();

  if (state.mode == NORMAL) {
    switch (c) {
      case CTRL_KEY('q'):
        exit(0);
        break;
      case 'i':
        state.mode = INSERT;
        break;
      case ':':
        state.mode = COMMAND;
        ab_reset(state.info);
        break;
      case 'j':
        if (state.cursor->y < state.num_rows - 1) {
          if (state.rows[state.cursor->y + 1].len < state.cursor->previous_x) {
            state.cursor->x = state.rows[state.cursor->y + 1].len;
          } else {
            state.cursor->x = state.cursor->previous_x;
          }
          state.cursor->y++;
        }
        break;
      case 'k':
        if (state.cursor->y > 0) {
          if (state.rows[state.cursor->y - 1].len < state.cursor->previous_x) {
            state.cursor->x = state.rows[state.cursor->y - 1].len;
          } else {
            state.cursor->x = state.cursor->previous_x;
          }
          state.cursor->y--;
        }
        break;
      case 'h':
        if (state.cursor->x > 0) {
          state.cursor->x--;
          state.cursor->previous_x = state.cursor->x;
        }
        break;
      case 'l':
        if (state.cursor->x < state.rows[state.cursor->y].len) {
          state.cursor->x++;
          state.cursor->previous_x = state.cursor->x;
        }
        break;
    }
  } else if (state.mode == INSERT) {
    switch (c) {
      case '\033':
        state.mode = NORMAL;
        break;
      default:
        ab_append(&state.rows[state.cursor->y - 1], (const char[2]) {c, '\0'}, 1);
        break;
    }
  } else if (state.mode == COMMAND) {
    switch (c) {
      case '\n':
        ab_reset(state.info);
        break;
      case '\033':
        state.mode = NORMAL;
        ab_reset(state.info);
        break;
      default:
        ab_append(state.info, (const char[2]) {c, '\0'}, 1);
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

  state.num_rows = 0;
  state.rows = NULL;
  state.cursor = malloc(sizeof(struct Cursor));
  state.cursor->x = 0;
  state.cursor->y = 0;
  state.cursor->previous_x = 0;
  state.info = malloc(sizeof(AppendBuffer));
  state.info->buffer = malloc(1);
  state.info->buffer[0] = '\0';
  state.info->len = 0;
  state.mode = NORMAL;

  enable_raw_mode();
  editor_open(argv[1]);
  getScreenSize(&state.window_height, &state.window_width);

  while (1) {
    editor_refresh_screen();
    process_keypress();
  }

  // fwrite(buffer, strlen(buffer), 1, f);
  // fclose(f);

  return 0;
}
