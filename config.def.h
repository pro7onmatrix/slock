/* user and group to drop privileges to */
static const char *user  = "nobody";
static const char *group = "nobody";

static const char *colorname[NUMCOLS] = {
	[INIT]   = "#4C566A",   /* after initialization and cleared pw */
	[INPUT]  = "#FFFFFF",   /* during input */
	[FAILED] = "#BF616A",   /* wrong password */
};

/* treat a cleared input like a wrong password (color) */
static const int failonclear = 0;

/* time in seconds before the monitor shuts down */
static const int monitortime = 300;

/* allow control key to trigger fail on clear */
static const int controlkeyclear = 0;

static const int blurradius = 15;

#define CPU_THREADS 4
