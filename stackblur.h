/**
 * Multithreaded Gaussian blur, originally written in Processing
 * (Java) by Mario Klingemann <http://incubator.quasimondo.com>
 * and ported to C by Aario Shahbany <https://github.com/aario>.
 */

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

typedef struct {
	unsigned char *pix;
	int x;
	int y;
	int w;
	int y2;
	int H;
	int wm;
	int wh;
	int *r;
	int *g;
	int *b;
	int *dv;
	int radius;
	int *vminx;
	int *vminy;
} StackBlurRenderingParams;

#include <pthread.h>

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

void *HStackRenderingThread(void *arg);
void *VStackRenderingThread(void *arg);
void stackblur(XImage *image, int x, int y, int w, int h, int radius, unsigned int num_threads);
