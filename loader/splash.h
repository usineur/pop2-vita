#ifndef __SPLASH_H__
#define __SPLASH_H__

typedef struct {
  unsigned char *buf;
  int w;
  int h;
  int n;
} splash_img;

splash_img get_splashscreen();

#endif
