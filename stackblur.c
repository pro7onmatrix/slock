#include "stackblur.h"
#include <stdlib.h>

void *
HStackRenderingThread(void *arg)
{
  StackBlurRenderingParams *rp = (StackBlurRenderingParams *) arg;
  int rinsum, ginsum, binsum,
      routsum, goutsum, boutsum,
      rsum, gsum, bsum,
      x, y, i, yi, yw, rbs, p, sp;

  int div = 2 * rp->radius + 1;
  int *stackr = malloc(div * sizeof(int));
  int *stackg = malloc(div * sizeof(int));
  int *stackb = malloc(div * sizeof(int));
  yw = yi = rp->y * rp->w;
  int r1 = rp->radius + 1;

  for (y = rp->y; y < rp->y2; y++) {
    rinsum = ginsum = binsum = routsum = goutsum = boutsum = rsum = gsum = bsum = 0;
    for(i = -rp->radius; i <= rp->radius; i++) {
      p = (yi + MIN(rp->wm, MAX(i, 0))) * 4;
      sp = i + rp->radius;
      stackr[sp] = rp->pix[p];
      stackg[sp] = rp->pix[p + 1];
      stackb[sp] = rp->pix[p + 2];
      rbs = r1 - abs(i);
      rsum += stackr[sp] * rbs;
      gsum += stackg[sp] * rbs;
      bsum += stackb[sp] * rbs;
      if (i > 0) {
        rinsum += stackr[sp];
        ginsum += stackg[sp];
        binsum += stackb[sp];
      } else {
        routsum += stackr[sp];
        goutsum += stackg[sp];
        boutsum += stackb[sp];
      }
    }
    int stackpointer;
    int stackstart;
    stackpointer = rp->radius;

    for (x = rp->x; x < rp->w; x++) {
      rp->r[yi] = rp->dv[rsum];
      rp->g[yi] = rp->dv[gsum];
      rp->b[yi] = rp->dv[bsum];

      rsum -= routsum;
      gsum -= goutsum;
      bsum -= boutsum;

      stackstart = stackpointer - rp->radius + div;
      sp = stackstart % div;

      routsum -= stackr[sp];
      goutsum -= stackg[sp];
      boutsum -= stackb[sp];

      p = (yw + rp->vminx[x]) * 4;
      stackr[sp] = rp->pix[p];
      stackg[sp] = rp->pix[p + 1];
      stackb[sp] = rp->pix[p + 2];

      rinsum += stackr[sp];
      ginsum += stackg[sp];
      binsum += stackb[sp];

      rsum += rinsum;
      gsum += ginsum;
      bsum += binsum;

      stackpointer = (stackpointer + 1) % div;
      sp = stackpointer;

      routsum += stackr[sp];
      goutsum += stackg[sp];
      boutsum += stackb[sp];

      rinsum -= stackr[sp];
      ginsum -= stackg[sp];
      binsum -= stackb[sp];

      yi++;
    }
    yw += rp->w;
  }

  free(stackr);
  free(stackg);
  free(stackb);
  stackr = stackg = stackb = NULL;
  pthread_exit(NULL);
}

void *
VStackRenderingThread(void *arg)
{
  StackBlurRenderingParams *rp = (StackBlurRenderingParams *) arg;
  int rinsum, ginsum, binsum,
      routsum, goutsum, boutsum,
      rsum, gsum, bsum,
      x, y, i, yi, yp, rbs, p, sp;

  int div = 2 * rp->radius + 1;
  int *stackr = malloc(div * sizeof(int));
  int *stackg = malloc(div * sizeof(int));
  int *stackb = malloc(div * sizeof(int));
  int r1 = rp->radius + 1;
  int hm = rp->H - rp->y - 1;

  for (x = rp->x; x < rp->w; x++) {
    rinsum = ginsum = binsum = routsum = goutsum = boutsum = rsum = gsum = bsum = 0;
    yp = (rp->y - rp->radius) * rp->w;
    for (i = -rp->radius; i <= rp->radius; i++) {
      yi = MAX(0, yp) + x;
      sp = i + rp->radius;

      stackr[sp] = rp->r[yi];
      stackg[sp] = rp->g[yi];
      stackb[sp] = rp->b[yi];

      rbs = r1 - abs(i);

      rsum += rp->r[yi] * rbs;
      gsum += rp->g[yi] * rbs;
      bsum += rp->b[yi] * rbs;

      if (i > 0) {
        rinsum += stackr[sp];
        ginsum += stackg[sp];
        binsum += stackb[sp];
      } else {
        routsum += stackr[sp];
        goutsum += stackg[sp];
        boutsum += stackb[sp];
      }

      if (i < hm) {
        yp += rp->w;
      }
    }

    yi = rp->y * rp->w + x;
    int stackpointer;
    int stackstart;
    stackpointer = rp->radius;

    for (y = rp->y; y < rp->y2; y++) {
      p = yi * 4;
      rp->pix[p]     = (unsigned char) (rp->dv[rsum]);
      rp->pix[p + 1] = (unsigned char) (rp->dv[gsum]);
      rp->pix[p + 2] = (unsigned char) (rp->dv[bsum]);
      rp->pix[p + 3] = 0xff;

      rsum -= routsum;
      gsum -= goutsum;
      bsum -= boutsum;

      stackstart = stackpointer - rp->radius + div;
      sp = stackstart % div;

      routsum -= stackr[sp];
      goutsum -= stackg[sp];
      boutsum -= stackb[sp];

      p = x + rp->vminy[y];
      stackr[sp] = rp->r[p];
      stackg[sp] = rp->g[p];
      stackb[sp] = rp->b[p];

      rinsum += stackr[sp];
      ginsum += stackg[sp];
      binsum += stackb[sp];

      rsum += rinsum;
      gsum += ginsum;
      bsum += binsum;

      stackpointer = (stackpointer + 1) % div;
      sp = stackpointer;

      routsum += stackr[sp];
      goutsum += stackg[sp];
      boutsum += stackb[sp];

      rinsum -= stackr[sp];
      ginsum -= stackg[sp];
      binsum -= stackb[sp];

      yi += rp->w;
    }
  }

  free(stackr);
  free(stackg);
  free(stackb);
  stackr = stackg = stackb = NULL;
  pthread_exit(NULL);
}

void
stackblur(XImage *image, int x, int y, int w, int h, int radius, unsigned int num_threads)
{
  if (radius < 1)
    return;
  char *pix = image->data;
  int wh = w * h;
  int *r = malloc(wh * sizeof(int));
  int *g = malloc(wh * sizeof(int));
  int *b = malloc(wh * sizeof(int));
  int i;

  int divsum = (radius + 1) * (radius + 1);
  int *dv = malloc(256 * divsum * sizeof(int));
  for (i = 0; i < 256 * divsum; i++) {
    dv[i] = (i / divsum);
  }

  int *vminx = malloc(w * sizeof(int));
  for (i = 0; i < w; i++)
    vminx[i] = MIN(i + radius + 1, w - 1);
  int *vminy = malloc(h * sizeof(int));
  for (i = 0; i < h; i++)
    vminy[i] = MIN(i + radius + 1, h - 1) * w;

  pthread_t *pthh = malloc(num_threads * sizeof(pthread_t));
  StackBlurRenderingParams *rp = malloc(num_threads * sizeof(StackBlurRenderingParams));
  int threadY = y;
  int threadH = (h / num_threads);

  for (i = 0; i < num_threads; i++) {
    rp[i].pix = (unsigned char *) pix;
    rp[i].x = x;
    rp[i].w = w;
    rp[i].y = threadY;
    rp[i].y2 = (i == num_threads - 1) ? (y + h) : (threadY + threadH);
    rp[i].H = h;
    rp[i].wm = rp[i].w - 1;
    rp[i].wh = wh;
    rp[i].r = r;
    rp[i].g = g;
    rp[i].b = b;
    rp[i].dv = dv;
    rp[i].radius = radius;
    rp[i].vminx = vminx;
    rp[i].vminy = vminy;
    pthread_create(&pthh[i], NULL, HStackRenderingThread, &rp[i]);
    threadY += threadH;
  }

  for (i = 0; i < num_threads; i++)
    pthread_join(pthh[i], NULL);

  pthread_t *pthv = malloc(num_threads * sizeof(pthread_t));
  for (i = 0; i < num_threads; i++) {
    pthread_create(&pthv[i], NULL, VStackRenderingThread, &rp[i]);
  }

  for (i = 0; i < num_threads; i++)
    pthread_join(pthv[i], NULL);

  free(vminx);
  free(vminy);
  free(rp);
  free(r);
  free(g);
  free(b);
  free(dv);
  free(pthh);
  free(pthv);
  rp = NULL;
  dv = vminx = vminy = r = g = b = NULL;
  pthh = pthv = NULL;
}
