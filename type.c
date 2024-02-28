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

struct editorRow {
  ssize_t size;
  char *content;
};

struct editorConfig {
  struct termios term;
  struct editorRow *rows;
  int num_rows;
  int cursorX;
  int cursorY;
  int window_height;
  int window_width;
};

struct editorConfig state;

void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

void getScreenSize() {
  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  
  state.window_height = w.ws_row;
  state.window_width = w.ws_col;
}

void disableRawMode() { 
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &state.term)) {
    die("tcsetattr");
  }
}

void enableRawMode() {
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

void editorAppendRow(char *s, ssize_t len) {
  state.rows = realloc(state.rows, sizeof(struct editorRow) * (state.num_rows + 1));

  if (state.rows == NULL) {
    die("Realloc failed");
  }

  int at = state.num_rows;
  state.rows[at].size = len;
  state.rows[at].content = malloc(len + 1);
  memcpy(state.rows[at].content, s, len);
  state.rows[at].content[len] = '\0';
  state.num_rows++;
}

void editorDrawRows() {
  struct editorRow *row;
  ssize_t row_number;
  
  for (row_number = 0; row_number < state.window_height; row_number++) {
    if (row_number < state.num_rows) {
      row = &state.rows[row_number];
      char *line = NULL;
      int size;
      size = asprintf(&line, "%d\t%s\r\n", (int) row_number + 1, row->content);
      write(STDOUT_FILENO, line, size);
    } else {
      write(STDOUT_FILENO, "~\r\n", 3);
    }
  }
}

void editorOpen(char *path) {
  FILE *f = fopen(path, "r");

  if (f == NULL) {
    perror("File open error");
  }

  char *line = NULL;
  size_t len = 0;
  ssize_t size;

  while ((size = getline(&line, &len, f)) != -1) {
    editorAppendRow(line, size);
  }

  fclose(f);
  if (line) {
    free(line);
  }
}

void editorMoveCursor(int row, int col) { printf("\x1b[%d;%df", row, col); }

void editorRefreshScreen() {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  // editorMoveCursor(5, 5);
}

char readKeypress() {
  ssize_t nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }
  return c;
}

void processKeypress() {
  char c = readKeypress();

  switch (c) {
  case CTRL_KEY('q'):
    exit(0);
    break;
  }
  if (iscntrl(c)) {
    printf("%d\r\n", c);
  } else {
    printf("%d ('%c')\r\n", c, c);
  }
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    printf("Usage: type [FILE]");
    return 0;
  }

  state.cursorX = 0;
  state.cursorY = 0;
  state.num_rows = 0;
  state.rows = NULL;

  enableRawMode();
  editorOpen(argv[1]);
  getScreenSize();

  while (1) {
    editorRefreshScreen();
    editorDrawRows();
    processKeypress();
  }

  // fwrite(buffer, strlen(buffer), 1, f);
  // fclose(f);

  return 0;
}
