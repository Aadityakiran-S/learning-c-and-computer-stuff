#include "exercise.h"
#include <string.h>
#include <stdlib.h>

int smart_append(TextBuffer *dest, const char *src) {
  if(dest == NULL || src == NULL) return 1;

  int max_buffer_size = 64;
  int len_src = strlen(src); int len_buffer = dest->length;
  int free_space = abs(max_buffer_size - len_buffer -1);

  if(len_src > free_space){
    strncat(dest->buffer, src, free_space); dest->length = 63; return 1;
  }else{
    strcat(dest->buffer, src); dest->length += len_src; return 0;
  }
}
