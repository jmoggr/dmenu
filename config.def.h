/* See LICENSE file for copyright and license details. */
/* Default settings; can be overriden by command line. */

static int topbar = 1;                      /* -b  option; if 0, dmenu appears at bottom     */
static int interactive = 0;                 /* -I option; if 1, dmenu continuously reads stdin */
static int dimmed = 0;                      /* -d option; if not 0, surrounding screen is dimmed */
static unsigned long dimcolor = 0x66101010; /* dimming color */
/* -fn option overrides fonts[0]; default X11 font or font set */
static const char *fonts[] = {
	"monospace:size=10"
};
static const char *stoptoken  = NULL;       /* -st option; stops the menu when a matching string is read from stdin */
static const char *cleartoken  = NULL;      /* -ct option; resets the menu when a matching string is read from stdin */
static const char *prompt      = NULL;      /* -p  option; prompt to the left of input field */
static const char *colors[SchemeLast][2] = {
	/*     fg         bg       */
	[SchemeNorm] = { "#bbbbbb", "#222222" },
	[SchemeSel] = { "#eeeeee", "#005577" },
	[SchemeOut] = { "#000000", "#00ffff" },
	[SchemeBorder] = { "#000000", "#00ffff" }, /* only fg color is used */
};
/* -l option; if nonzero, dmenu uses vertical list with given number of lines */
static unsigned int lines      = 0;

/* -b2 option; width of border around dmenu window */
static unsigned int borderwidth = 0;

/*
 * Characters not considered part of a word while deleting words
 * for example: " /?\"&[]"
 */
static const char worddelimiters[] = " ";
