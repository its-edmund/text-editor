#ifndef APPEND_BUFFER_H
#define APPEND_BUFFER_H

#define APPEND_BUFFER_INIT                                                     \
  { NULL, 0 }

typedef struct {
  char *buffer;
  int len;
} AppendBuffer;

void ab_append(AppendBuffer *ab, const char *s, int len);
void ab_remove(AppendBuffer *ab);
void ab_reset(AppendBuffer *ab);
void ab_free(AppendBuffer *ab);

#endif
