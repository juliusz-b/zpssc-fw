/**
 * sysmem.c - _sbrk dla newlib (sterta miedzy 'end' a stosem)
 * Juliusz Bojarczuk, Politechnika Warszawska
 */
#include <errno.h>
#include <stdint.h>

extern char end asm("end");

void *_sbrk(ptrdiff_t incr)
{
  static char *heap_end = 0;
  char *prev_heap_end;
  register char *sp asm("sp");

  if (heap_end == 0) {
    heap_end = &end;
  }
  prev_heap_end = heap_end;

  if (heap_end + incr > sp) {
    errno = ENOMEM;
    return (void *)-1;
  }
  heap_end += incr;
  return (void *)prev_heap_end;
}
