#include "append_buffer.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

void ab_append(AppendBuffer *ab, const char *s, int len) {
  char *new = realloc(ab->buffer, ab->len + len);

  if (new == NULL) {
    return;
  }

  memcpy(&new[ab->len], s, len);
  ab->buffer = new;
  ab->len += len;
}

void ab_remove(AppendBuffer *ab) {
  if (ab->len < 1) {
    return;
  }
  char *new = realloc(ab->buffer, ab->len);

  if (new == NULL) {
    return;
  }

  ab->len--;
  ab->buffer[ab->len] = '\0';
  ab->buffer = new;
}

void ab_free(AppendBuffer *ab) { free(ab->buffer); }

void ab_reset(AppendBuffer *ab) {
  ab->buffer = realloc(ab->buffer, 1);
  ab->buffer[0] = '\0';
  ab->len = 0;
}
