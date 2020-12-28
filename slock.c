/* See LICENSE file for license details. */
#include <X11/X.h>
#include <pthread.h>
#define _XOPEN_SOURCE 500
#if HAVE_SHADOW_H
#include <shadow.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/dpms.h>
#include <X11/extensions/Xinerama.h>
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "stackblur.h"

#include "arg.h"
#include "util.h"

char *argv0;

enum {
  INIT,
  INPUT,
  FAILED,
  NUMCOLS
};

struct lock {
  int screen;
  Window root, win;
  Pixmap pmap;
  unsigned long colors[NUMCOLS];
  XImage *image, *originalimage;
};

struct TintThreadParams {
  int tid; // Thread-ID
  unsigned char tr, tg, tb; // Tinting RGB values
  XImage *image; // Pointer to image
};

struct TimeThreadParams {
  Display *dpy;
  struct lock *lock;
  struct tm *current_time;
  pthread_mutex_t *mutex;
  pthread_cond_t *cond;
  int exit;
};

struct xrandr {
  int active;
  int evbase;
  int errbase;
};

#include "config.h"

static void *
tintline(void *args)
{
  struct TintThreadParams *params = (struct TintThreadParams *) args;

  unsigned long x, y;
  unsigned long pixel;

  unsigned char pr, pg, pb;

  for (y = params->tid; y < params->image->height; y += CPU_THREADS) {
    for (x = 0; x < params->image->width; x++) {
      pixel = XGetPixel(params->image, x, y);

      pr = ((pixel & params->image->red_mask) >> 16) * params->tr / 255;
      pg = ((pixel & params->image->green_mask) >> 8) * params->tg / 255;
      pb = (pixel & params->image->blue_mask) * params->tb / 255;

      pixel = pr << 16 | pg << 8 | pb;

      XPutPixel(params->image, x, y, pixel);
    }
  }

  pthread_exit(NULL);
}

static void
blurlockwindow(Display *dpy, struct lock *lock, int color)
{
  // get original image
  memcpy(lock->image->data, lock->originalimage->data, sizeof(char) * lock->originalimage->bytes_per_line * lock->originalimage->height);

  // tint the image
  unsigned char tr = (lock->colors[color] & lock->image->red_mask) >> 16;
  unsigned char tg = (lock->colors[color] & lock->image->green_mask) >> 8;
  unsigned char tb = (lock->colors[color] & lock->image->blue_mask);

  pthread_t *tint_threads = malloc(CPU_THREADS * sizeof(pthread_t));
  struct TintThreadParams *tintparams = malloc(CPU_THREADS * sizeof(struct TintThreadParams));

  int i;

  for (i = 0; i < CPU_THREADS; i++) {
    tintparams[i].tid = i;
    tintparams[i].tr = tr;
    tintparams[i].tg = tg;
    tintparams[i].tb = tb;
    tintparams[i].image = lock->image;

    pthread_create(&tint_threads[i], NULL, tintline, &tintparams[i]);
  }

  for (i = 0; i < CPU_THREADS; i++)
    pthread_join(tint_threads[i], NULL);

  free(tint_threads);
  free(tintparams);
}

static void
displayimagetime(Display *dpy, struct lock *lock, struct tm *time)
{
  int len, width, height, s_width, s_height, i;
  XGCValues gr_values;
  XFontStruct *fontinfo;
  XColor color, dummy;
  XineramaScreenInfo *xsi;
  GC gc;

  fontinfo = XLoadQueryFont(dpy, font_name);

  XAllocNamedColor(dpy, DefaultColormap(dpy, lock->screen),
     text_color, &color, &dummy);

  gr_values.font = fontinfo->fid;
  gr_values.foreground = color.pixel;
  gc = XCreateGC(dpy, lock->win, GCFont + GCForeground, &gr_values);

  XWindowAttributes attr;
  XGetWindowAttributes(dpy, lock->root, &attr);

  XPutImage(dpy, lock->win, gc, lock->image, 0, 0, 0, 0, attr.width, attr.height);

  /*  To prevent "Uninitialized" warnings. */
  xsi = NULL;

  /* Start formatting and drawing text */
  char message[32];
  sprintf(message, "%02d:%02d:%02d", time->tm_hour, time->tm_min, time->tm_sec);
  len = strlen(message);

  if (XineramaIsActive(dpy)) {
    xsi = XineramaQueryScreens(dpy, &i);
    s_width = xsi[0].width;
    s_height = xsi[0].height;
  } else {
    s_width = DisplayWidth(dpy, lock->screen);
    s_height = DisplayHeight(dpy, lock->screen);
  }

  height = s_height / 2;
  width  = (s_width - XTextWidth(fontinfo, message, len)) / 2;

  XDrawString(dpy, lock->win, gc, width, height, message, len);

  /* xsi should not be NULL anyway if Xinerama is active, but to be safe */
  if (XineramaIsActive(dpy) && xsi != NULL)
    XFree(xsi);

  XFlush(dpy);
  XFreeGC(dpy, gc);
}

static void *
updatetime(void *args)
{
  struct TimeThreadParams *params = (struct TimeThreadParams *) args;

  time_t rawtime;

  // for pthread_cond_timedwait
  struct timespec timeout;
  clock_gettime(CLOCK_MONOTONIC, &timeout);

  XEvent e;

  pthread_mutex_lock(params->mutex);

  while (!params->exit) {
    timeout.tv_sec += 1;

    // get current time
    time(&rawtime);
    localtime_r(&rawtime, params->current_time);

    // send Expose event
    memset(&e, 0, sizeof(XEvent));
    e.type = Expose;
    e.xexpose.window = params->lock->win;
    XSendEvent(params->dpy, params->lock->win, 0, ExposureMask, &e);
    XFlush(params->dpy);

    // sleep for 1 second
    pthread_cond_timedwait(params->cond, params->mutex, &timeout);
  }

  pthread_mutex_unlock(params->mutex);
  printf("Exiting...\n");
  pthread_exit(NULL);
}

static void
die(const char *errstr, ...)
{
  va_list ap;

  va_start(ap, errstr);
  vfprintf(stderr, errstr, ap);
  va_end(ap);
  exit(1);
}

#ifdef __linux__
#include <fcntl.h>
#include <linux/oom.h>

static void
dontkillme(void)
{
  FILE *f;
  const char oomfile[] = "/proc/self/oom_score_adj";

  if (!(f = fopen(oomfile, "w"))) {
    if (errno == ENOENT)
      return;
    die("slock: fopen %s: %s\n", oomfile, strerror(errno));
  }
  fprintf(f, "%d", OOM_SCORE_ADJ_MIN);
  if (fclose(f)) {
    if (errno == EACCES)
      die("slock: unable to disable OOM killer. "
          "Make sure to suid or sgid slock.\n");
    else
      die("slock: fclose %s: %s\n", oomfile, strerror(errno));
  }
}
#endif

static const char *
gethash(void)
{
  const char *hash;
  struct passwd *pw;

  /* Check if the current user has a password entry */
  errno = 0;
  if (!(pw = getpwuid(getuid()))) {
    if (errno)
      die("slock: getpwuid: %s\n", strerror(errno));
    else
      die("slock: cannot retrieve password entry\n");
  }
  hash = pw->pw_passwd;

#if HAVE_SHADOW_H
  if (!strcmp(hash, "x")) {
    struct spwd *sp;
    if (!(sp = getspnam(pw->pw_name)))
      die("slock: getspnam: cannot retrieve shadow entry. "
          "Make sure to suid or sgid slock.\n");
    hash = sp->sp_pwdp;
  }
#else
  if (!strcmp(hash, "*")) {
#ifdef __OpenBSD__
    if (!(pw = getpwuid_shadow(getuid())))
      die("slock: getpwnam_shadow: cannot retrieve shadow entry. "
          "Make sure to suid or sgid slock.\n");
    hash = pw->pw_passwd;
#else
    die("slock: getpwuid: cannot retrieve shadow entry. "
        "Make sure to suid or sgid slock.\n");
#endif /* __OpenBSD__ */
  }
#endif /* HAVE_SHADOW_H */

  return hash;
}

static void
readpw(Display *dpy, struct xrandr *rr, struct lock **locks, int nscreens,
       const char *hash)
{
  XRRScreenChangeNotifyEvent *rre;
  char buf[32], passwd[256], *inputhash;
  int num, screen, running, failure, oldc;
  unsigned int len, color;
  KeySym ksym;
  XEvent ev;

  len = 0;
  running = 1;
  oldc = INIT;

  // display time with updates every second on main monitor
  XSelectInput(dpy, locks[0]->win, KeyPressMask | ExposureMask);

  struct tm *current_time = malloc(sizeof(struct tm));

  pthread_mutex_t mutex;
  pthread_mutex_init(&mutex, NULL);

  pthread_cond_t cond;
  pthread_condattr_t condattr;
  pthread_condattr_init(&condattr);
  pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC);
  pthread_cond_init(&cond, &condattr);

  struct TimeThreadParams *ttparams = malloc(sizeof(struct TimeThreadParams));
  ttparams->dpy = dpy;
  ttparams->lock = locks[0];
  ttparams->mutex = &mutex;
  ttparams->cond = &cond;
  ttparams->current_time = current_time;
  ttparams->exit = 0;

  pthread_t time_thread;
  pthread_create(&time_thread, NULL, updatetime, ttparams);

  for (screen = 0; screen < nscreens; screen++)
    displayimagetime(dpy, locks[screen], current_time);

  while (running && !XNextEvent(dpy, &ev)) {
    failure = 0;

    if (ev.type == KeyPress) {
      explicit_bzero(&buf, sizeof(buf));
      num = XLookupString(&ev.xkey, buf, sizeof(buf), &ksym, 0);
      if (IsKeypadKey(ksym)) {
        if (ksym == XK_KP_Enter)
          ksym = XK_Return;
        else if (ksym >= XK_KP_0 && ksym <= XK_KP_9)
          ksym = (ksym - XK_KP_0) + XK_0;
      }
      if (IsFunctionKey(ksym) ||
          IsKeypadKey(ksym) ||
          IsMiscFunctionKey(ksym) ||
          IsPFKey(ksym) ||
          IsPrivateKeypadKey(ksym))
        continue;
      switch (ksym) {
      case XK_Return:
        passwd[len] = '\0';
        errno = 0;
        if (!(inputhash = crypt(passwd, hash)))
          fprintf(stderr, "slock: crypt: %s\n", strerror(errno));
        else
          running = !!strcmp(inputhash, hash);
        if (running) {
          XBell(dpy, 100);
          failure = 1;
        }
        explicit_bzero(&passwd, sizeof(passwd));
        len = 0;
        break;
      case XK_Escape:
        explicit_bzero(&passwd, sizeof(passwd));
        len = 0;
        break;
      case XK_BackSpace:
        if (len)
          passwd[--len] = '\0';
        break;
      default:
        if (controlkeyclear && iscntrl((int)buf[0]))
          continue;
        if (num && (len + num < sizeof(passwd))) {
          memcpy(passwd + len, buf, num);
          len += num;
        }
        break;
      }
      color = len ? INPUT : ((failure || failonclear) ? FAILED : INIT);
      if (running && oldc != color) {
        for (screen = 0; screen < nscreens; screen++) {
          blurlockwindow(dpy, locks[screen], color);
          displayimagetime(dpy, locks[screen], current_time);
        }
        oldc = color;
      }
    } else if (ev.type == Expose) {
      displayimagetime(dpy, locks[0], current_time);
    } else if (rr->active && ev.type == rr->evbase + RRScreenChangeNotify) {
      rre = (XRRScreenChangeNotifyEvent*)&ev;
      for (screen = 0; screen < nscreens; screen++) {
        if (locks[screen]->win == rre->window) {
          if (rre->rotation == RR_Rotate_90 ||
              rre->rotation == RR_Rotate_270)
            XResizeWindow(dpy, locks[screen]->win,
                          rre->height, rre->width);
          else
            XResizeWindow(dpy, locks[screen]->win,
                          rre->width, rre->height);
          blurlockwindow(dpy, locks[screen], INIT);
          displayimagetime(dpy, locks[screen], current_time);
          XClearWindow(dpy, locks[screen]->win);
          break;
        }
      }
    } else {
      for (screen = 0; screen < nscreens; screen++)
        XRaiseWindow(dpy, locks[screen]->win);
    }
  }

  ttparams->exit = 1;
  pthread_cond_signal(ttparams->cond);
  pthread_join(time_thread, NULL);

  free(current_time);
  free(ttparams);
}

static struct lock *
lockscreen(Display *dpy, struct xrandr *rr, int screen)
{
  char curs[] = {0, 0, 0, 0, 0, 0, 0, 0};
  int i, ptgrab, kbgrab;
  struct lock *lock;
  XColor color, dummy;
  XSetWindowAttributes wa;
  Cursor invisible;

  if (dpy == NULL || screen < 0 || !(lock = malloc(sizeof(struct lock))))
    return NULL;

  lock->screen = screen;
  lock->root = RootWindow(dpy, lock->screen);

  for (i = 0; i < NUMCOLS; i++) {
    XAllocNamedColor(dpy, DefaultColormap(dpy, lock->screen),
                     colorname[i], &color, &dummy);
    lock->colors[i] = color.pixel;
  }

  /* init */
  wa.override_redirect = 1;
  wa.background_pixel = lock->colors[INIT];
  lock->win = XCreateWindow(dpy, lock->root, 0, 0,
                            DisplayWidth(dpy, lock->screen),
                            DisplayHeight(dpy, lock->screen),
                            0, DefaultDepth(dpy, lock->screen),
                            CopyFromParent,
                            DefaultVisual(dpy, lock->screen),
                            CWOverrideRedirect | CWBackPixel, &wa);
  lock->pmap = XCreateBitmapFromData(dpy, lock->win, curs, 8, 8);
  invisible = XCreatePixmapCursor(dpy, lock->pmap, lock->pmap,
                                  &color, &color, 0, 0);
  XDefineCursor(dpy, lock->win, invisible);

  XWindowAttributes attr;
  XGetWindowAttributes(dpy, lock->root, &attr);
  lock->originalimage = XGetImage(dpy, lock->root, attr.x, attr.y, attr.width, attr.height, AllPlanes, ZPixmap);
  stackblur(lock->originalimage, 0, 0, lock->originalimage->width, lock->originalimage->height, blurradius, CPU_THREADS);

  lock->image = malloc(sizeof(XImage));
  memcpy(lock->image, lock->originalimage, sizeof(XImage));
  lock->image->data = malloc(sizeof(char) * lock->image->bytes_per_line * lock->image->height);
  blurlockwindow(dpy, lock, INIT);

  /* Try to grab mouse pointer *and* keyboard for 600ms, else fail the lock */
  for (i = 0, ptgrab = kbgrab = -1; i < 6; i++) {
    if (ptgrab != GrabSuccess) {
      ptgrab = XGrabPointer(dpy, lock->root, False,
                            ButtonPressMask | ButtonReleaseMask |
                            PointerMotionMask, GrabModeAsync,
                            GrabModeAsync, None, invisible, CurrentTime);
    }
    if (kbgrab != GrabSuccess) {
      kbgrab = XGrabKeyboard(dpy, lock->root, True,
                             GrabModeAsync, GrabModeAsync, CurrentTime);
    }

    /* input is grabbed: we can lock the screen */
    if (ptgrab == GrabSuccess && kbgrab == GrabSuccess) {
      XMapRaised(dpy, lock->win);
      if (rr->active)
        XRRSelectInput(dpy, lock->win, RRScreenChangeNotifyMask);

      XSelectInput(dpy, lock->root, SubstructureNotifyMask);
      return lock;
    }

    /* retry on AlreadyGrabbed but fail on other errors */
    if ((ptgrab != AlreadyGrabbed && ptgrab != GrabSuccess) ||
        (kbgrab != AlreadyGrabbed && kbgrab != GrabSuccess))
      break;

    usleep(100000);
  }

  /* we couldn't grab all input: fail out */
  if (ptgrab != GrabSuccess)
    fprintf(stderr, "slock: unable to grab mouse pointer for screen %d\n",
            screen);
  if (kbgrab != GrabSuccess)
    fprintf(stderr, "slock: unable to grab keyboard for screen %d\n",
            screen);
  return NULL;
}

static void
usage(void)
{
  die("usage: slock [-v] [cmd [arg ...]]\n");
}

int
main(int argc, char **argv) {
  struct xrandr rr;
  struct lock **locks;
  struct passwd *pwd;
  struct group *grp;
  uid_t duid;
  gid_t dgid;
  const char *hash;
  Display *dpy;
  int s, nlocks, nscreens;
  CARD16 standby, suspend, off;

  ARGBEGIN {
  case 'v':
    fprintf(stderr, "slock-"VERSION"\n");
    return 0;
  default:
    usage();
  } ARGEND

  /* validate drop-user and -group */
  errno = 0;
  if (!(pwd = getpwnam(user)))
    die("slock: getpwnam %s: %s\n", user,
        errno ? strerror(errno) : "user entry not found");
  duid = pwd->pw_uid;
  errno = 0;
  if (!(grp = getgrnam(group)))
    die("slock: getgrnam %s: %s\n", group,
        errno ? strerror(errno) : "group entry not found");
  dgid = grp->gr_gid;

#ifdef __linux__
  dontkillme();
#endif

  hash = gethash();
  errno = 0;
  if (!crypt("", hash))
    die("slock: crypt: %s\n", strerror(errno));

  if (!(dpy = XOpenDisplay(NULL)))
    die("slock: cannot open display\n");

  /* drop privileges */
  if (setgroups(0, NULL) < 0)
    die("slock: setgroups: %s\n", strerror(errno));
  if (setgid(dgid) < 0)
    die("slock: setgid: %s\n", strerror(errno));
  if (setuid(duid) < 0)
    die("slock: setuid: %s\n", strerror(errno));

  /* check for Xrandr support */
  rr.active = XRRQueryExtension(dpy, &rr.evbase, &rr.errbase);

  /* get number of screens in display "dpy" and blank them */
  nscreens = ScreenCount(dpy);
  if (!(locks = calloc(nscreens, sizeof(struct lock *))))
    die("slock: out of memory\n");
  for (nlocks = 0, s = 0; s < nscreens; s++) {
    if ((locks[s] = lockscreen(dpy, &rr, s)) != NULL)
      nlocks++;
    else
      break;
  }
  XSync(dpy, 0);

  /* did we manage to lock everything? */
  if (nlocks != nscreens)
    return 1;

  /* DPMS magic to disable the monitor */
  if (!DPMSCapable(dpy))
    die("slock: DPMSCapable failed\n");
  if (!DPMSEnable(dpy))
    die("slock: DPMSEnable failed\n");
  if (!DPMSGetTimeouts(dpy, &standby, &suspend, &off))
    die("slock: DPMSGetTimeouts failed\n");
  if (!standby || !suspend || !off)
    die("slock: at least one DPMS variable is zero\n");
  if (!DPMSSetTimeouts(dpy, monitortime, monitortime, monitortime))
    die("slock: DPMSSetTimeouts failed\n");

  XSync(dpy, 0);

  /* run post-lock command */
  if (argc > 0) {
    switch (fork()) {
    case -1:
      die("slock: fork failed: %s\n", strerror(errno));
    case 0:
      if (close(ConnectionNumber(dpy)) < 0)
        die("slock: close: %s\n", strerror(errno));
      execvp(argv[0], argv);
      fprintf(stderr, "slock: execvp %s: %s\n", argv[0], strerror(errno));
      _exit(1);
    }
  }

  /* everything is now blank. Wait for the correct password */
  readpw(dpy, &rr, locks, nscreens, hash);

  /* reset DPMS values to inital ones */
  DPMSSetTimeouts(dpy, standby, suspend, off);
  XSync(dpy, 0);

  return 0;
}
