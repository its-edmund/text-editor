#ifndef EDITOR_H
#define EDITOR_H

#include "append_buffer.h"
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

enum editorMode {
  NORMAL,
  INSERT,
  COMMAND
};

struct EditorConfig {
  AppendBuffer *rows;
  AppendBuffer *info;
  struct Cursor *cursor;
  enum editorMode mode;
  char *file_path;
  int num_rows;
  int window_height;
  int window_width;
};

static struct termios *term;

void die(const char *s);
void editor_append_row(struct EditorConfig *e, char *s, ssize_t len);
void disable_raw_mode(void);


#endif // !EDITOR_H
