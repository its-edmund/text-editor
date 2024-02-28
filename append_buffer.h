#ifndef APPEND_BUFFER
#define APPEND_BUFFER

#define APPEND_BUFFER_INIT {NULL, 0}

typedef struct {
  char *buffer;
  int len;
} AppendBuffer;

extern void ab_append(AppendBuffer *ab, const char *s, int len);
extern void ab_reset(AppendBuffer *ab);
extern void ab_free(AppendBuffer *ab);

#endif
