#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "append_buffer.h"

void ab_append(AppendBuffer *ab, const char *s, int len) {
  char *new = realloc(ab->buffer, ab->len + len);

  if (new == NULL) {
    return;
  }

  memcpy(&new[ab->len], s, len);
  ab->buffer = new;
  ab->len += len;
}

void ab_free(AppendBuffer *ab) {
  free(ab->buffer);
}

void ab_reset(AppendBuffer *ab) {
  ab->buffer = "";
  ab->len = 0;
}
