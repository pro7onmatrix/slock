/* user and group to drop privileges to */
static const char *user  = "nobody";
static const char *group = "nobody";

static const char *colorname[NUMCOLS] = {
	[INIT] =   "black",     /* after initialization */
	[INPUT] =  "#005577",   /* during input */
	[FAILED] = "#CC3333",   /* wrong password */
};

/* treat a cleared input like a wrong password (color) */
static const int failonclear = 1;

/* default message */
static const char * message = "Logged in as $USER, locked at $(date '+%a %b %d %H:%M')";

/* text color */
static const char * text_color = "#d8dee9";

/* text size (must be a valid size) */
static const char * font_name = "8x13";
