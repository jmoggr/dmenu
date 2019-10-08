/* See LICENSE file for copyright and license details. */
#include <ctype.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#include <X11/Xft/Xft.h>

#include "drw.h"
#include "util.h"

/* macros */
#define INTERSECT(x,y,w,h,r)  (MAX(0, MIN((x)+(w),(r).x_org+(r).width)  - MAX((x),(r).x_org)) \
                             * MAX(0, MIN((y)+(h),(r).y_org+(r).height) - MAX((y),(r).y_org)))
#define LENGTH(X)             (sizeof X / sizeof X[0])
#define TEXTW(X)              (drw_fontset_getwidth(drw, (X)) + lrpad)

/* enums */
enum { SchemeNorm, SchemeSel, SchemeOut, SchemeMisc, SchemeLast }; /* color schemes */

struct item {
	char *text;
	struct item *left, *right;
	int out;
};

static int item_count = 0;
static int max_lines = 0;
static char text[BUFSIZ] = "";
static char *embed;
static int bh, mw, mh;
static int dmx = 0; /* put dmenu at this x offset */
static int dmy = 0; /* put dmenu at this y offset (measured from the bottom if topbar is 0) */
static unsigned int dmw = 0; /* make dmenu this wide */
static int inputw = 0, promptw;
static int lrpad; /* sum of left and right padding */
static size_t cursor;
static struct item *items = NULL;
static struct item *matches, *matchend;
static struct item *prev, *curr, *next, *sel;
static int mon = -1, screen;
static int centerx = 0, centery = 0, usemaxtextw = 0;
static int quick_select = 0;

static Atom clip, utf8;
static Display *dpy;
static Window root, parentwin, win, bwin;
static XIC xic;

static XVisualInfo vinfo;
static Colormap colormap;

static Drw *drw;
static Clr *scheme[SchemeLast];

#include "config.h"

static int (*fstrncmp)(const char *, const char *, size_t) = strncmp;
static char *(*fstrstr)(const char *, const char *) = strstr;

static void
appenditem(struct item *item, struct item **list, struct item **last)
{
	if (*last)
		(*last)->right = item;
	else
		*list = item;

	item->left = *last;
	item->right = NULL;
	*last = item;
}

static int
itemlistlen(struct item *list)
{
	if (!list)
		return 0;

	int i;
	struct item *item;
	for(i = 0, item = list; item; item = item->right)
		i++;

	return i;
}

static void
calcoffsets(void)
{
	int i, n;

	if (lines > 0)
		n = lines * bh;
	else
		n = mw - (promptw + inputw + TEXTW("<") + TEXTW(">"));
	/* calculate which items will begin the next page and previous page */
	for (i = 0, next = curr; next; next = next->right)
		if ((i += (lines > 0) ? bh : MIN(TEXTW(next->text), n)) > n)
			break;
	for (i = 0, prev = curr; prev && prev->left; prev = prev->left)
		if ((i += (lines > 0) ? bh : MIN(TEXTW(prev->left->text), n)) > n)
			break;
}

static int
max_textw(void)
{
	int len = 0;
	for (struct item *item = items; item && item->text; item++)
		len = MAX(TEXTW(item->text), len);
	return len;
}


static void
cleanup(void)
{
	size_t i;

	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	for (i = 0; i < SchemeLast; i++)
		free(scheme[i]);
	drw_free(drw);
	XSync(dpy, False);
	XCloseDisplay(dpy);
}

static char *
cistrstr(const char *s, const char *sub)
{
	size_t len;

	for (len = strlen(sub); *s; s++)
		if (!strncasecmp(s, sub, len))
			return (char *)s;
	return NULL;
}

static int
drawitem(struct item *item, int x, int y, int w)
{
	if (item == sel)
		drw_setscheme(drw, scheme[SchemeSel]);
	else if (item->out)
		drw_setscheme(drw, scheme[SchemeOut]);
	else
		drw_setscheme(drw, scheme[SchemeNorm]);

	return drw_text(drw, x, y, w, bh, lrpad / 2, item->text, 0);
}

static void
drawmenu(void)
{
	unsigned int curpos;
	struct item *item;
	int x = 0, y = 0, w, i;

	drw_setscheme(drw, scheme[SchemeNorm]);
	drw_rect(drw, 0, 0, mw, mh, 1, 1);

	if (prompt && *prompt) {
		drw_setscheme(drw, scheme[SchemeSel]);
		x = drw_text(drw, x, 0, promptw, bh, lrpad / 2, prompt, 0);
	}
	/* draw input field */
	w = (lines > 0 || !matches) ? mw - x : inputw;
	drw_setscheme(drw, scheme[SchemeNorm]);
	drw_text(drw, x, 0, w, bh, lrpad / 2, text, 0);

	curpos = TEXTW(text) - TEXTW(&text[cursor]);
	if ((curpos += lrpad / 2 - 1) < w) {
		drw_setscheme(drw, scheme[SchemeNorm]);
		drw_rect(drw, x + curpos, 2, 2, bh - 4, 1, 0);
	}

	if (lines > 0) {
		/* draw vertical list */
		char nmatchstr[13];
		int nmatches = itemlistlen(matches);
		sprintf(nmatchstr, "%4d matches", nmatches % 1000);
		drw_setscheme(drw, scheme[SchemeSel]);
		drw_text(drw, mw - TEXTW(nmatchstr), y, TEXTW(nmatchstr), bh, lrpad / 2, nmatchstr, 0);

		for (item = curr, i = 0; item != next; i += 1, item = item->right)
			if (i < strlen(quick_select_order) && quick_select) {
				char quick_char_string[2] = {quick_select_order[i], '\0'};
				float lpad = bh/2.0 - (TEXTW(quick_char_string) - lrpad)/2.0;
				drw_setscheme(drw, scheme[SchemeOut]);
				if (x > bh) {
					drw_text(drw, x - bh, y + bh, bh, bh, lpad, quick_char_string, 0);
					drawitem(item, x, y += bh, mw - x);
				} else {
					drw_text(drw, 0, y + bh, bh, bh, lpad, quick_char_string, 0);
					drawitem(item, x + bh, y += bh, mw - x);
				}
			}
			else
				drawitem(item, x, y += bh, mw - x);
	} else if (matches) {
		/* draw horizontal list */
		x += inputw;
		w = TEXTW("<");
		if (curr->left) {
			drw_setscheme(drw, scheme[SchemeNorm]);
			drw_text(drw, x, 0, w, bh, lrpad / 2, "<", 0);
		}
		x += w;
			// x = drawitem(item, x, 0, MIN(TEXTW(item->text), mw - x - TEXTW("W")), NULL);
		for (item = curr, i = 0; item != next; i += 1, item = item->right) {
			if (i < strlen(quick_select_order) && quick_select) {
				char quick_char_string[2] = {quick_select_order[i], '\0'};
				float lpad = bh/2.0 - (TEXTW(quick_char_string) - lrpad)/2.0;
				drw_setscheme(drw, scheme[SchemeOut]);
				x = drw_text(drw, x, 0, bh, bh, lpad, quick_char_string, 0);
				x = drawitem(item, x, 0, MIN(TEXTW(item->text), mw - x - TEXTW(">")));
			} else
				x = drawitem(item, x, 0, MIN(TEXTW(item->text), mw - x - TEXTW(">")));
		}

		if (next) {
			w = TEXTW(">");
			drw_setscheme(drw, scheme[SchemeNorm]);
			drw_text(drw, mw - w, 0, w, bh, lrpad / 2, ">", 0);
		}
	}
	drw_map(drw, win, 0, 0, mw, mh);
}

static void
grabfocus(void)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000  };
	Window focuswin;
	int i, revertwin;

	for (i = 0; i < 100; ++i) {
		XGetInputFocus(dpy, &focuswin, &revertwin);
		if (focuswin == win)
			return;
		XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
		nanosleep(&ts, NULL);
	}
	die("cannot grab focus");
}

static void
grabkeyboard(void)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000  };
	int i;

	if (embed)
		return;
	/* try to grab keyboard, we may have to wait for another process to ungrab */
	for (i = 0; i < 1000; i++) {
		if (XGrabKeyboard(dpy, DefaultRootWindow(dpy), True, GrabModeAsync,
		                  GrabModeAsync, CurrentTime) == GrabSuccess)
			return;
		nanosleep(&ts, NULL);
	}
	die("cannot grab keyboard");
}

static void
match(void)
{
	static char **tokv = NULL;
	static int tokn = 0;

	char buf[sizeof text], *s;
	int i, tokc = 0;
	size_t len, textsize;
	struct item *item, *lprefix, *lsubstr, *prefixend, *substrend;

	strcpy(buf, text);
	/* separate input text into tokens to be matched individually */
	for (s = strtok(buf, " "); s; tokv[tokc - 1] = s, s = strtok(NULL, " "))
		if (++tokc > tokn && !(tokv = realloc(tokv, ++tokn * sizeof *tokv)))
			die("cannot realloc %u bytes:", tokn * sizeof *tokv);
	len = tokc ? strlen(tokv[0]) : 0;

	if (use_prefix) {
		matches = lprefix = matchend = prefixend = NULL;
		textsize = strlen(text);
	} else {
		matches = lprefix = lsubstr = matchend = prefixend = substrend = NULL;
		textsize = strlen(text) + 1;
	}
	for (item = items; item && item->text; item++) {
		for (i = 0; i < tokc; i++)
			if (!fstrstr(item->text, tokv[i]))
				break;
		if (i != tokc) /* not all tokens match */
			continue;
		/* exact matches go first, then prefixes, then substrings */
		if (!tokc || !fstrncmp(text, item->text, textsize))
			appenditem(item, &matches, &matchend);
		else if (!fstrncmp(tokv[0], item->text, len))
			appenditem(item, &lprefix, &prefixend);
		else if (!use_prefix)
			appenditem(item, &lsubstr, &substrend);
	}
	if (lprefix) {
		if (matches) {
			matchend->right = lprefix;
			lprefix->left = matchend;
		} else
			matches = lprefix;
		matchend = prefixend;
	}
	if (!use_prefix && lsubstr) {
		if (matches) {
			matchend->right = lsubstr;
			lsubstr->left = matchend;
		} else
			matches = lsubstr;
		matchend = substrend;
	}
	curr = sel = matches;
	calcoffsets();
}

static void
insert(const char *str, ssize_t n)
{
	if (strlen(text) + n > sizeof text - 1)
		return;
	/* move existing text out of the way, insert new text, and update cursor */
	memmove(&text[cursor + n], &text[cursor], sizeof text - cursor - MAX(n, 0));
	if (n > 0)
		memcpy(&text[cursor], str, n);
	cursor += n;
	match();
}

static size_t
nextrune(int inc)
{
	ssize_t n;

	/* return location of next utf8 rune in the given direction (+1 or -1) */
	for (n = cursor + inc; n + inc >= 0 && (text[n] & 0xc0) == 0x80; n += inc)
		;
	return n;
}

static void
movewordedge(int dir)
{
	if (dir < 0) { /* move cursor to the start of the word*/
		while (cursor > 0 && strchr(worddelimiters, text[nextrune(-1)]))
			cursor = nextrune(-1);
		while (cursor > 0 && !strchr(worddelimiters, text[nextrune(-1)]))
			cursor = nextrune(-1);
	} else { /* move cursor to the end of the word */
		while (text[cursor] && strchr(worddelimiters, text[cursor]))
			cursor = nextrune(+1);
		while (text[cursor] && !strchr(worddelimiters, text[cursor]))
			cursor = nextrune(+1);
	}
}

static void
keypress(XKeyEvent *ev)
{
	char buf[32];
	int len;
	struct item * item;
	KeySym ksym;
	Status status;

	len = XmbLookupString(xic, ev, buf, sizeof buf, &ksym, &status);
	switch (status) {
	default: /* XLookupNone, XBufferOverflow */
		return;
	case XLookupChars:
		goto insert;
	case XLookupKeySym:
	case XLookupBoth:
		break;
	}

	if (ev->state & ControlMask) {
		switch(ksym) {
		case XK_a: ksym = XK_Home;      break;
		case XK_b: ksym = XK_Left;      break;
		case XK_c: ksym = XK_Escape;    break;
		case XK_d: ksym = XK_Delete;    break;
		case XK_e: ksym = XK_End;       break;
		case XK_f: ksym = XK_Right;     break;
		case XK_g: ksym = XK_Escape;    break;
		case XK_h: ksym = XK_BackSpace; break;
		case XK_i: ksym = XK_Tab;       break;
		case XK_j: /* fallthrough */
		case XK_J: /* fallthrough */
		case XK_m: /* fallthrough */
		case XK_M: ksym = XK_Return; ev->state &= ~ControlMask; break;
		case XK_n: ksym = XK_Down;      break;
		case XK_p: ksym = XK_Up;        break;

		case XK_s:
			quick_select = (quick_select == 1) ? 0 : 1;
			goto draw;
		case XK_k: /* delete right */
			text[cursor] = '\0';
			match();
			break;
		case XK_u: /* delete left */
			insert(NULL, 0 - cursor);
			break;
		case XK_w: /* delete word */
			while (cursor > 0 && strchr(worddelimiters, text[nextrune(-1)]))
				insert(NULL, nextrune(-1) - cursor);
			while (cursor > 0 && !strchr(worddelimiters, text[nextrune(-1)]))
				insert(NULL, nextrune(-1) - cursor);
			break;
		case XK_y: /* paste selection */
		case XK_Y:
			XConvertSelection(dpy, (ev->state & ShiftMask) ? clip : XA_PRIMARY,
			                  utf8, utf8, win, CurrentTime);
			return;
		case XK_Left:
			movewordedge(-1);
			goto draw;
		case XK_Right:
			movewordedge(+1);
			goto draw;
		case XK_Return:
		case XK_KP_Enter:
			break;
		case XK_bracketleft:
			cleanup();
			exit(1);
		default:
			return;
		}
	} else if (ev->state & Mod1Mask) {
		switch(ksym) {
		case XK_b:
			movewordedge(-1);
			goto draw;
		case XK_f:
			movewordedge(+1);
			goto draw;
		case XK_g: ksym = XK_Home;  break;
		case XK_G: ksym = XK_End;   break;
		case XK_h: ksym = XK_Up;    break;
		case XK_j: ksym = XK_Next;  break;
		case XK_k: ksym = XK_Prior; break;
		case XK_l: ksym = XK_Down;  break;
		default:
			return;
		}
	}

	switch(ksym) {
	default:
insert:
		if (!iscntrl(*buf))
			if (quick_select) {
				char quick_char = buf[0];

				int i = 0;
				struct item *item;
				for (item = curr; item != next, i < strlen(quick_select_order); i += 1, item = item->right)
					if (quick_char == quick_select_order[i]) {
						puts(item->text);
						cleanup();
						exit(0);
					}

				return;
			} else
				insert(buf, len);
		break;
	case XK_Delete:
		if (text[cursor] == '\0')
			return;
		cursor = nextrune(+1);
		/* fallthrough */
	case XK_BackSpace:
		if (cursor == 0)
			return;
		insert(NULL, nextrune(-1) - cursor);
		break;
	case XK_End:
		if (text[cursor] != '\0') {
			cursor = strlen(text);
			break;
		}
		if (next) {
			/* jump to end of list and position items in reverse */
			curr = matchend;
			calcoffsets();
			curr = prev;
			calcoffsets();
			while (next && (curr = curr->right))
				calcoffsets();
		}
		sel = matchend;
		break;
	case XK_Escape:
		cleanup();
		exit(1);
	case XK_Home:
		if (sel == matches) {
			cursor = 0;
			break;
		}
		sel = curr = matches;
		calcoffsets();
		break;
	case XK_Left:
		if (cursor > 0 && (!sel || !sel->left || lines > 0)) {
			cursor = nextrune(-1);
			break;
		}
		if (lines > 0)
			return;
		/* fallthrough */
	case XK_Up:
		if (sel && sel->left && (sel = sel->left)->right == curr) {
			curr = prev;
			calcoffsets();
		}
		break;
	case XK_Next:
		if (!next)
			return;
		sel = curr = next;
		calcoffsets();
		break;
	case XK_Prior:
		if (!prev)
			return;
		sel = curr = prev;
		calcoffsets();
		break;
	case XK_Return:
	case XK_KP_Enter:
		puts((sel && !(ev->state & ShiftMask)) ? sel->text : text);
		if (!(ev->state & ControlMask) && !interactive) {
			cleanup();
			exit(0);
		}
		if (sel)
			sel->out = 1;
		break;
	case XK_Right:
		if (text[cursor] != '\0') {
			cursor = nextrune(+1);
			break;
		}
		if (lines > 0)
			return;
		/* fallthrough */
	case XK_Down:
		if (sel && sel->right && (sel = sel->right) == next) {
			curr = next;
			calcoffsets();
		}
		break;
	case XK_Tab:
		if (!matches) break; /* cannot complete no matches */
		strncpy(text, matches->text, sizeof text - 1);
		text[sizeof text - 1] = '\0';
		len = cursor = strlen(text); /* length of longest common prefix */
		for (item = matches; item && item->text; item = item->right) {
			cursor = 0;
			while (cursor < len && text[cursor] == item->text[cursor])
				cursor++;
			len = cursor;
		}
		memset(text + len, '\0', strlen(text) - len);
		break;
	}

draw:
	drawmenu();
}

static void
paste(void)
{
	char *p, *q;
	int di;
	unsigned long dl;
	Atom da;

	/* we have been given the current selection, now insert it into input */
	if (XGetWindowProperty(dpy, win, utf8, 0, (sizeof text / 4) + 1, False,
	                   utf8, &da, &di, &dl, &dl, (unsigned char **)&p)
	    == Success && p) {
		insert(p, (q = strchr(p, '\n')) ? q - p : (ssize_t)strlen(p));
		XFree(p);
	}
	drawmenu();
}

static void
updateitems(void)
{
	if (items)
		items[item_count].text = NULL;

	lines = MIN(max_lines, item_count);
	match();
	mh = (lines + 1) * bh;
	drw_resize(drw, mw, mh);
	XResizeWindow(dpy, win, mw, mh);
}

static void
readstdin(void)
{
	char buf[sizeof text], *p;
	size_t size = 0;
	unsigned int tmpmax = 0;

	/* read a line from stdin and add it to the item list */
	if(fgets(buf, sizeof buf, stdin)) {
		if (item_count + 1 >= size / sizeof *items)
			if (!(items = realloc(items, (size += BUFSIZ))))
				die("cannot realloc %u bytes:", size);
		if ((p = strchr(buf, '\n')))
			*p = '\0';
		if (cleartoken && !strcmp(cleartoken, buf)) {
			text[0] = '\0';
			cursor = 0;
			item_count = 0;
			return;
		} else if (stoptoken && !strcmp(stoptoken, buf)) {
			cleanup();
			exit(0);
		}
		if (!(items[item_count].text = strdup(buf)))
			die("cannot strdup %u bytes:", strlen(buf) + 1);
		items[item_count].out = 0;
		drw_font_getexts(drw->fonts, buf, strlen(buf), &tmpmax, NULL);
		if (tmpmax > inputw) {
			inputw = tmpmax;
		}

		item_count++;
	}

	appenditem(&items[item_count - 1], &matches, &matchend);
	if (!curr) {
		curr = sel = &items[0];
	}
	if (items)
		items[item_count].text = NULL;
}

static void
readXEvent(void)
{
	XEvent ev;

	while(XPending(dpy) && !XNextEvent(dpy, &ev)) {
		if (XFilterEvent(&ev, win))
			continue;
		switch(ev.type) {
		case DestroyNotify:
			if (ev.xdestroywindow.window != win)
				break;
			cleanup();
			exit(1);
		case Expose:
			if (ev.xexpose.count == 0)
				drw_map(drw, win, 0, 0, mw, mh);
			break;
		case FocusIn:
			/* regrab focus from parent window */
			if (ev.xfocus.window != win)
				grabfocus();
			break;
		case KeyPress:
			keypress(&ev.xkey);
			break;
		case SelectionNotify:
			if (ev.xselection.property == utf8)
				paste();
			break;
		case VisibilityNotify:
			if (ev.xvisibility.state != VisibilityUnobscured)
				XRaiseWindow(dpy, win);
			break;
		}
	}
}

static void
run(void) {
	fd_set fds;
	int x11_fd, n, nfds, flags;

	if(setvbuf(stdin, NULL, _IONBF, 4096))
		die("could not set stdin to no buffering");

	flags = fcntl(STDIN_FILENO, F_GETFL);
	flags |= O_NONBLOCK;
	fcntl(STDIN_FILENO, F_SETFL, flags);

	x11_fd = XConnectionNumber(dpy);
	nfds = MAX(STDIN_FILENO, x11_fd) + 1;

	/* timeout prevents menu from resizing for every line of input read */
	struct timeval timeout = {0, 16666}, *timeoutptr = &timeout;

	while(1) {
		FD_ZERO(&fds);
		if (!feof(stdin))
			FD_SET(STDIN_FILENO, &fds);
		FD_SET(x11_fd, &fds);

		n = select(nfds, &fds, NULL, NULL, timeoutptr);
		if (n < 0)
			die("cannot select\n");
        if (FD_ISSET(STDIN_FILENO, &fds)) {
			readstdin();
			timeout = (struct timeval) {0, 16666};
			timeoutptr = &timeout;
		}
		if (FD_ISSET(x11_fd, &fds))
			readXEvent();
		if (!n && timeoutptr) {
			updateitems();
			timeoutptr = NULL;
		}

		fflush(stdout);
		drawmenu();
	}
}

static void
setup(void)
{
	int x, y, i, j;
	unsigned int du;
	XSetWindowAttributes swa;
	XIM xim;
	Window w, dw, *dws;
	XWindowAttributes wa;
	XClassHint ch = {"dmenu", "dmenu"};
#ifdef XINERAMA
	XineramaScreenInfo *info;
	Window pw;
	int a, di, n, area = 0;
#endif
	/* init appearance */
	for (j = 0; j < SchemeLast; j++)
		scheme[j] = drw_scm_create(drw, colors[j], 2);

	clip = XInternAtom(dpy, "CLIPBOARD",   False);
	utf8 = XInternAtom(dpy, "UTF8_STRING", False);

	/* calculate menu geometry */
	bh = drw->fonts->h + 2;
	mh = (max_lines + 1) * bh;
	promptw = (prompt && *prompt) ? TEXTW(prompt) - lrpad / 4 : 0;
#ifdef XINERAMA
	i = 0;
	if (parentwin == root && (info = XineramaQueryScreens(dpy, &n))) {
		XGetInputFocus(dpy, &w, &di);
		if (mon >= 0 && mon < n)
			i = mon;
		else if (w != root && w != PointerRoot && w != None) {
			/* find top-level window containing current input focus */
			do {
				if (XQueryTree(dpy, (pw = w), &dw, &w, &dws, &du) && dws)
					XFree(dws);
			} while (w != root && w != pw);
			/* find xinerama screen with which the window intersects most */
			if (XGetWindowAttributes(dpy, pw, &wa))
				for (j = 0; j < n; j++)
					if ((a = INTERSECT(wa.x, wa.y, wa.width, wa.height, info[j])) > area) {
						area = a;
						i = j;
					}
		}
		/* no focused window is on screen, so use pointer location instead */
		if (mon < 0 && !area && XQueryPointer(dpy, root, &dw, &dw, &x, &y, &di, &di, &du))
			for (i = 0; i < n; i++)
				if (INTERSECT(x, y, 1, 1, info[i]))
					break;

		if (usemaxtextw)
			mw = MIN(MAX(max_textw() + promptw, 100), info[i].width);
		else
			mw = (dmw>0 ? dmw : info[i].width);

		if (centerx)
			x = info[i].x_org + ((info[i].width  - mw) / 2);
		else
			x = info[i].x_org + dmx;

		if (centery)
			y = info[i].y_org + ((info[i].height - mh) / 2);
		else
			y = info[i].y_org + (topbar ? dmy : info[i].height - mh - dmy);

		XFree(info);
	} else
#endif
	{
		if (!XGetWindowAttributes(dpy, parentwin, &wa))
			die("could not get embedding window attributes: 0x%lx",
			    parentwin);

		if (usemaxtextw)
			mw = MIN(MAX(max_textw() + promptw, 100), wa.width);
		else
			mw = (dmw>0 ? dmw : wa.width);

		if (centerx)
			x = (wa.width  - mw) / 2;
		else
			x = dmx;

		if (centery)
			y = (wa.height - mh) / 2;
		else
			y = topbar ? dmy : wa.height - mh - dmy;
	}

	mw -= borderwidth * 2;

	promptw = (prompt && *prompt) ? TEXTW(prompt) - lrpad / 4 : 0;
	inputw = MIN(inputw, mw/3);
	match();

	/* create menu window */
	swa.override_redirect = True;
	swa.event_mask = ExposureMask | KeyPressMask | VisibilityChangeMask;
	swa.border_pixel = scheme[SchemeMisc][ColFg].pixel;
	swa.colormap = colormap;
	win = XCreateWindow(dpy, parentwin, x, y, mw, mh, 0,
	                    vinfo.depth, InputOutput, vinfo.visual,
	                    CWOverrideRedirect | CWBackPixel | CWEventMask | CWColormap | CWBorderPixel, &swa);

	XSetWindowBorderWidth(dpy, win, borderwidth);
	XSetClassHint(dpy, win, &ch);
	/* input methods */
	if ((xim = XOpenIM(dpy, NULL, NULL, NULL)) == NULL)
		die("XOpenIM failed: could not open input device");

	xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
	                XNClientWindow, win, XNFocusWindow, win, NULL);

	XMapRaised(dpy, win);
	if (embed) {
		XSelectInput(dpy, parentwin, FocusChangeMask | SubstructureNotifyMask);
		if (XQueryTree(dpy, parentwin, &dw, &w, &dws, &du) && dws) {
			for (i = 0; i < du && dws[i] != win; ++i)
				XSelectInput(dpy, dws[i], FocusChangeMask);
			XFree(dws);
		}
		grabfocus();
	}
	drw_resize(drw, mw, mh);
	drawmenu();
}

static void
dim_screen(void)
{
	XSetWindowAttributes swa;
	XWindowAttributes wa;

	if (!XGetWindowAttributes(dpy, parentwin, &wa))
		die("could not get embedding window attributes: 0x%lx",
		    parentwin);

	colormap = XCreateColormap(dpy, root, vinfo.visual, AllocNone);

	swa.override_redirect = True;
	swa.background_pixel = scheme[SchemeMisc][ColBg].pixel;
	swa.border_pixel = 0;
	swa.colormap = colormap;

	bwin = XCreateWindow(dpy, root, 0, 0, wa.width, wa.height, 0,
                         32, CopyFromParent, vinfo.visual,
                         CWOverrideRedirect | CWBorderPixel | CWBackPixel | CWColormap, &swa);

	XMapRaised(dpy, bwin);
}

static void
usage(void)
{
	fputs("usage: dmenu [-bivdXIs] [-l lines] [-p prompt] [-fn font] [-m monitor]\n"
	      "             [-bc color] [-bw pixels] [-dc color] [-qs characters]\n"
	      "             [-x {xoffset|'c'}] [-y {yoffset|'c'}] [-width {width|'t'}]\n"
	      "             [-nb color] [-nf color] [-sb color] [-sf color] [-w windowid]\n", stderr);
	exit(1);
}

int
main(int argc, char *argv[])
{
	XWindowAttributes wa;
	int i;

	for (i = 1; i < argc; i++)
		/* these options take no arguments */
		if (!strcmp(argv[i], "-v")) {      /* prints version information */
			puts("dmenu-"VERSION);
			exit(0);
		} else if (!strcmp(argv[i], "-b")) /* appears at the bottom of the screen */
			topbar = 0;
		else if (!strcmp(argv[i], "-d"))   /* dims the surrounding screen */
			dimmed = 1;
		else if (!strcmp(argv[i], "-i")) { /* case-insensitive item matching */
			fstrncmp = strncasecmp;
			fstrstr = cistrstr;
		} else if (!strcmp(argv[i], "-I")) /* dynamically add items from stdin after startup */
			interactive = 1;
		else if (!strcmp(argv[i], "-X"))   /* invert use_prefix */
			use_prefix = !use_prefix;
		else if (!strcmp(argv[i], "-s")) /* start in quick select mode */
			quick_select = 1;
		else if (i + 1 == argc)
			usage();
		/* these options take one argument */
		else if (!strcmp(argv[i], "-ct")) /* resets the menu when a matching string is read from stdin */
			cleartoken = argv[++i];
		else if (!strcmp(argv[i], "-st")) /* exits the menu when a matching string is read from stdin */
			stoptoken = argv[++i];
		else if (!strcmp(argv[i], "-l"))   /* number of lines in vertical list */
			max_lines = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-x"))   /* window x offset */
			if (*argv[++i] == 'c')
				centerx = True;
			else
				dmx = atoi(argv[i]);
		else if (!strcmp(argv[i], "-y"))   /* window y offset (from bottom up if -b) */
			if (*argv[++i] == 'c')
				centery = True;
			else
				dmy = atoi(argv[i]);
		else if (!strcmp(argv[i], "-width"))   /* make dmenu this wide */
			if (*argv[++i] == 't')
				usemaxtextw = True;
			else
				dmw = atoi(argv[i]);
		else if (!strcmp(argv[i], "-m"))
			mon = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-p"))   /* adds prompt to left of input field */
			prompt = argv[++i];
		else if (!strcmp(argv[i], "-fn"))  /* font or font set */
			fonts[0] = argv[++i];
		else if (!strcmp(argv[i], "-nb"))  /* normal background color */
			colors[SchemeNorm][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-nf"))  /* normal foreground color */
			colors[SchemeNorm][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-sb"))  /* selected background color */
			colors[SchemeSel][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-sf"))  /* selected foreground color */
			colors[SchemeSel][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-bw"))  /* selected border width*/
			borderwidth = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-bc"))  /* selected border color */
			colors[SchemeMisc][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-dc")) {/* dimmed color */
			colors[SchemeMisc][ColBg] = argv[++i];
            dimmed = 1;
        } else if (!strcmp(argv[i], "-w"))   /* embedding window id */
			embed = argv[++i];
		else if (!strcmp(argv[i], "-qs"))  /* quick select order */
			quick_select_order = argv[++i];
		else
			usage();

	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fputs("warning: no locale support\n", stderr);
	if (!(dpy = XOpenDisplay(NULL)))
		die("cannot open display");


	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);

	if (!XMatchVisualInfo(dpy, screen, 32, TrueColor, &vinfo))
		die("cannot get visual info");

	colormap = XCreateColormap(dpy, root, vinfo.visual, AllocNone);
	if (!embed || !(parentwin = strtol(embed, NULL, 0)))
		parentwin = root;
	if (!XGetWindowAttributes(dpy, parentwin, &wa))
		die("could not get embedding window attributes: 0x%lx",
		    parentwin);
	drw = drw_create(dpy, screen, root, &vinfo, colormap, wa.width, wa.height);
	if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
		die("no fonts could be loaded.");
	lrpad = drw->fonts->h;

#ifdef __OpenBSD__
	if (pledge("stdio rpath", NULL) == -1)
		die("pledge");
#endif

	grabkeyboard();
	setup();

	if (dimmed && !embed)
		dim_screen();

	XMapRaised(dpy, win);

	run();

	return 1; /* unreachable */
}
