#include "common.h"

int clip_u8(int a) {
  if (a <= 0) return 0;
  if (a >= 255) return 255;
  return a;
}