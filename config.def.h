/* See LICENSE file for copyright and license details. */
/* Default settings; can be overriden by command line. */

static int topbar = 1;                      /* -b  option; if 0, dmenu appears at bottom     */
/* -fn option overrides fonts[0]; default X11 font or font set */
static const char *fonts[] = {
	"monospace:size=10"
};
static const char *prompt      = NULL;      /* -p  option; prompt to the left of input field */
static const char *colors[SchemeLast][2] = {
	/*     fg         bg       */
	[SchemeNorm] = { "#bbbbbb", "#222222" },
	[SchemeSel] = { "#eeeeee", "#005577" },
	[SchemeOut] = { "#000000", "#00ffff" },
	[SchemeMarker] = { "#a1b56c", "#222222" },
};
/* -l option; if nonzero, dmenu uses vertical list with given number of lines */
static unsigned int lines      = 0;

/* strings that form the page up/down markers
 *   pageupmarker = 'pagemarker1+ npagesbefore pagemarker2+'
 *   pagedownmarker = 'pagemarker2+ npagesafter pagemarker1+'
 * where '+' represents repeating to fill half of the marker
 * markers are colored using SchemeMarker */
static const char pagemarker1[] = "      >      ";
static const char pagemarker2[] = "      <      ";

/*
 * Characters not considered part of a word while deleting words
 * for example: " /?\"&[]"
 */
static const char worddelimiters[] = " ";
