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

static const int blurradius = 30;

#define CPU_THREADS 4

static const char *font_name = "-adobe-courier-bold-r-normal--34-240-100-100-m-200-iso8859-1";
static const char *text_color = "#ffffff";

