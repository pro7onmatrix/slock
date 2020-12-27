/* user and group to drop privileges to */
static const char *user  = "nobody";
static const char *group = "nobody";

static const char *colorname[NUMCOLS] = {
	[INIT]   = "#FFFFFF",   /* after initialization and cleared pw */
	[INPUT]  = "#00B6FF",   /* during input */
	[FAILED] = "#FF4040",   /* wrong password */
};

/* treat a cleared input like a wrong password (color) */
static const int failonclear = 0;

/* time in seconds before the monitor shuts down */
static const int monitortime = 10;

/* allow control key to trigger fail on clear */
static const int controlkeyclear = 0;


static const int blurradius = 30;

#define CPU_THREADS 4
