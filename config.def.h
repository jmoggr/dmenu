/* See LICENSE file for copyright and license details. */
/* Default settings; can be overriden by command line. */

static int topbar = 1;                      /* -b  option; if 0, dmenu appears at bottom     */
static int interactive = 0;                 /* -I option; if 1, dmenu continuously reads stdin */
static int dimmed = 1;                      /* -d option; if not 0, surrounding screen is dimmed */
/* -fn option overrides fonts[0]; default X11 font or font set */
static const char *fonts[] = {
	"monospace:size=10"
};
static const char *stoptoken   = NULL;      /* -st option; stops the menu when a matching string is read from stdin */
static const char *cleartoken  = NULL;      /* -ct option; resets the menu when a matching string is read from stdin */
static const char *prompt      = NULL;      /* -p  option; prompt to the left of input field */
static const char *colors[SchemeLast][2] = {
	/*     fg         bg       */
	[SchemeNorm] = { "#fff4d4cc", "#ee0b3441" },
	[SchemeSel] = { "#fff4d4cc", "#ee328079" },
	[SchemeOut] = { "#ff000000", "#ff124A69" },
	[SchemeMisc] = { "#ff124A69", "#66101010" }, /* fg is used for border, bg is used for dimcolor */
};
/* -l option; if nonzero, dmenu uses vertical list with given number of lines */
static unsigned int lines      = 0;

/* -bw option; width of border around dmenu window */
static unsigned int borderwidth = 2;

/*
 * Characters not considered part of a word while deleting words
 * for example: " /?\"&[]"
 */
static const char worddelimiters[] = " ";
