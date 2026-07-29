#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/times.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void
msleep(long ms)
{
	struct timespec delay = { ms / 1000, ms % 1000 * 1000000L };

	while (nanosleep(&delay, &delay) < 0 && errno == EINTR)
		;
}

static void
emit(const char *scenario)
{
	const char *const ascii_moves[] = {
		"\033[2;3H", "\033[2;5H", "\033[2;8H", "\033[2;11H",
		"\033[2;8H", "\033[2;5H", "\033[2;3H", "\033[2;1H",
	};
	const char *const cjk_moves[] = {
		"\033[3;3H", "\033[3;6H", "\033[4;2H", "\033[4;7H",
		"\033[3;6H", "\033[3;3H", "\033[4;2H", "\033[4;1H",
	};
	const char *const *moves;
	size_t i, nmoves;

	msleep(100);
	if (strcmp(scenario, "ascii") == 0 || strcmp(scenario, "idle") == 0) {
		fputs("\033[?25l\033[2J\033[HASCII cursor trail\r\n0123456789ABCDEF"
		      "\033[2;1H", stdout);
		moves = ascii_moves;
		nmoves = sizeof ascii_moves / sizeof ascii_moves[0];
	} else {
		if (strcmp(scenario, "alt") == 0)
			fputs("\033[?1049h", stdout);
		fputs("\033[?25l\033[2J\033[H窄窗口\r\nA你B好C\r\n倒数第二行"
		      "\033[4;1H末行XYZ\033[4;1H", stdout);
		moves = cjk_moves;
		nmoves = sizeof cjk_moves / sizeof cjk_moves[0];
	}
	fflush(stdout);
	if (strcmp(scenario, "idle") == 0) {
		msleep(1200);
		return;
	}
	msleep(600);

	fputs("\033[?25h", stdout);
	for (i = 0; i < nmoves; i++) {
		fputs(moves[i], stdout);
		fflush(stdout);
		msleep(25);
	}

	/* Let both effects expire naturally, then remove only the live cursor. */
	msleep(200);
	fputs("\033[?25l", stdout);
	fflush(stdout);
	/* No output after the live cursor is hidden. */
	msleep(400);
}

static Window
findwindow(Display *dpy, Window parent, pid_t pid)
{
	Window root, owner, *children = NULL, found = None;
	unsigned int i, nchildren = 0;
	Atom actual, pidatom;
	int format;
	unsigned long nitems, left;
	unsigned char *value = NULL;

	pidatom = XInternAtom(dpy, "_NET_WM_PID", False);
	if (XGetWindowProperty(dpy, parent, pidatom, 0, 1, False,
	                       XA_CARDINAL, &actual, &format, &nitems, &left,
	                       &value) == Success && value) {
		if (actual == XA_CARDINAL && format == 32 && nitems == 1 &&
		    *(unsigned long *)value == (unsigned long)pid)
			found = parent;
		XFree(value);
		if (found != None)
			return found;
	}
	if (!XQueryTree(dpy, parent, &root, &owner, &children, &nchildren))
		return None;
	for (i = 0; i < nchildren && found == None; i++)
		found = findwindow(dpy, children[i], pid);
	if (children)
		XFree(children);
	return found;
}

static void
dumpwindows(Display *dpy, Window parent, int depth)
{
	Window root, owner, *children = NULL;
	unsigned int i, nchildren = 0;
	char *name = NULL;

	if (XFetchName(dpy, parent, &name) && name) {
		fprintf(stderr, "X window depth=%d id=%lu name=%s\n",
		        depth, (unsigned long)parent, name);
		XFree(name);
	}
	if (depth >= 4 ||
	    !XQueryTree(dpy, parent, &root, &owner, &children, &nchildren))
		return;
	for (i = 0; i < nchildren; i++)
		dumpwindows(dpy, children[i], depth + 1);
	if (children)
		XFree(children);
}

static XImage *
windowimage(Display *dpy, Window window)
{
	XWindowAttributes attr;

	if (!XGetWindowAttributes(dpy, window, &attr) ||
	    attr.map_state != IsViewable)
		return NULL;
	return XGetImage(dpy, window, 0, 0, attr.width, attr.height,
	                 AllPlanes, ZPixmap);
}

static int
imagesame(XImage *a, XImage *b, const char *scenario)
{
	int different = 0, minx, miny, maxx = -1, maxy = -1, x, y;

	if (!a || !b || a->width != b->width || a->height != b->height)
		return 0;
	minx = a->width;
	miny = a->height;
	for (y = 0; y < a->height; y++) {
		for (x = 0; x < a->width; x++) {
			if (XGetPixel(a, x, y) == XGetPixel(b, x, y))
				continue;
			different++;
			minx = x < minx ? x : minx;
			miny = y < miny ? y : miny;
			maxx = x > maxx ? x : maxx;
			maxy = y > maxy ? y : maxy;
		}
	}
	if (different)
		fprintf(stderr,
		        "%s: %d different pixels in rectangle %d,%d-%d,%d\n",
		        scenario, different, minx, miny, maxx, maxy);
	return different == 0;
}

static int
runcase(Display *dpy, const char *self, const char *scenario,
        const char *geometry, int expectmove)
{
	Window window = None;
	XImage *baseline, *moving, *restored;
	char title[96];
	pid_t child;
	clock_t clocks, started;
	struct tms finished, initial;
	int failed = 0, i, status;

	started = times(&initial);
	snprintf(title, sizeof title, "st-cursor-trail-%s-%ld",
	         scenario, (long)getpid());
	child = fork();
	if (child < 0) {
		perror("fork");
		return 1;
	}
	if (child == 0) {
		execl("./st", "st", "-T", title, "-g", geometry,
		      "-e", self, "--emit", scenario, (char *)NULL);
		perror("exec ./st");
		_exit(127);
	}

	for (i = 0; i < 300 && window == None; i++) {
		pid_t exited;

		XSync(dpy, False);
		window = findwindow(dpy, DefaultRootWindow(dpy), child);
		exited = waitpid(child, &status, WNOHANG);
		if (exited == child) {
			fprintf(stderr, "FAIL: %s st exited early with status %d\n",
			        scenario, status);
			return 1;
		}
		if (window == None)
			msleep(10);
	}
	if (window == None) {
		fprintf(stderr, "FAIL: %s window did not appear\n", scenario);
		dumpwindows(dpy, DefaultRootWindow(dpy), 0);
		kill(child, SIGTERM);
		waitpid(child, &status, 0);
		return 1;
	}

	msleep(350);
	baseline = windowimage(dpy, window);
	msleep(400);
	moving = windowimage(dpy, window);
	msleep(400);
	restored = windowimage(dpy, window);
	waitpid(child, &status, 0);
	times(&finished);
	clocks = finished.tms_cutime + finished.tms_cstime -
	         initial.tms_cutime - initial.tms_cstime;

	if (!baseline || !moving || !restored) {
		fprintf(stderr, "FAIL: %s could not capture window pixels\n", scenario);
		if (baseline)
			XDestroyImage(baseline);
		if (moving)
			XDestroyImage(moving);
		if (restored)
			XDestroyImage(restored);
		return 1;
	}
	if (expectmove && imagesame(moving, baseline, scenario)) {
		fprintf(stderr, "FAIL: %s cursor did not visibly move\n", scenario);
		failed = 1;
	}
	if (!expectmove && !imagesame(moving, baseline, scenario)) {
		fprintf(stderr, "FAIL: %s changed while idle\n", scenario);
		failed = 1;
	}
	if (!imagesame(restored, baseline, scenario)) {
		fprintf(stderr, "FAIL: %s pixels were not restored\n", scenario);
		failed = 1;
	}
	XDestroyImage(baseline);
	XDestroyImage(moving);
	XDestroyImage(restored);
	if (started != (clock_t)-1 && clocks >= 0)
		printf("cursor-trail %s CPU: %.3fs\n", scenario,
		       (double)clocks / sysconf(_SC_CLK_TCK));
	if (!failed)
		printf("cursor-trail %s: ok\n", scenario);
	return failed;
}

static int
ignorexerror(Display *dpy, XErrorEvent *event)
{
	(void)dpy;
	(void)event;
	return 0;
}

int
main(int argc, char **argv)
{
	Display *dpy;
	int failed = 0;

	if (argc == 3 && strcmp(argv[1], "--emit") == 0) {
		emit(argv[2]);
		return 0;
	}
	if (argc != 1) {
		fprintf(stderr, "usage: %s\n", argv[0]);
		return 2;
	}
	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "cursor-trail: no X display; skipped\n");
		return 77;
	}
	XSetErrorHandler(ignorexerror);

	failed |= runcase(dpy, argv[0], "idle", "20x4", 0);
	failed |= runcase(dpy, argv[0], "ascii", "20x4", 1);
	failed |= runcase(dpy, argv[0], "cjk", "12x4", 1);
	failed |= runcase(dpy, argv[0], "alt", "12x4", 1);
	XCloseDisplay(dpy);
	return failed ? 1 : 0;
}
