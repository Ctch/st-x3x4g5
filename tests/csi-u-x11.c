#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
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

static int
reportready(const char *path)
{
	char buf[1024];
	FILE *fp;
	size_t len;

	if (!(fp = fopen(path, "r")))
		return 0;
	len = fread(buf, 1, sizeof buf - 1, fp);
	fclose(fp);
	buf[len] = '\0';
	return strstr(buf, "READY\n") != NULL;
}

static void
key(Display *dpy, KeySym sym, int press)
{
	KeyCode code = XKeysymToKeycode(dpy, sym);

	if (!code || !XTestFakeKeyEvent(dpy, code, press, CurrentTime)) {
		fprintf(stderr, "cannot inject keysym %lu\n", (unsigned long)sym);
		exit(1);
	}
}

static void
tap(Display *dpy, KeySym sym)
{
	key(dpy, sym, 1);
	key(dpy, sym, 0);
}

static void
modified(Display *dpy, KeySym mod1, KeySym mod2, KeySym sym)
{
	if (mod1 != NoSymbol)
		key(dpy, mod1, 1);
	if (mod2 != NoSymbol)
		key(dpy, mod2, 1);
	tap(dpy, sym);
	if (mod2 != NoSymbol)
		key(dpy, mod2, 0);
	if (mod1 != NoSymbol)
		key(dpy, mod1, 0);
}

static int
checkreport(const char *path)
{
	static const char expected[] =
		"KEYS 1b5b323775686a6b6c1b5b3130343b3375"
		"1b5b3130383b3375"
		"1b5b323775681b5b3237756a1b5b3237756b1b5b3237756c"
		"1b5b3130343b35751b5b3130363b3575"
		"1b5b3130373b35751b5b3130383b3575"
		"1b5b3130383b37751b5b3130383b3475"
		"1b5b3130383b36750d097f\n";
	char buf[4096];
	FILE *fp;
	size_t len;

	if (!(fp = fopen(path, "r")))
		return 1;
	len = fread(buf, 1, sizeof buf - 1, fp);
	fclose(fp);
	buf[len] = '\0';
	if (!strstr(buf, "STATE ok\n") || !strstr(buf, "DECRC ok\n") ||
	    !strstr(buf, expected) || !strstr(buf, "RESET ok\n")) {
		fprintf(stderr, "unexpected CSI u report:\n%s", buf);
		return 1;
	}
	puts("csi-u-input: ok");
	puts(expected);
	return 0;
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
	Window focus, window = None;
	pid_t child, exited;
	int i, revert, status;

	if (argc != 3) {
		fprintf(stderr, "usage: %s csi-u-input report-file\n", argv[0]);
		return 2;
	}
	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "csi-u-input: no X display; skipped\n");
		return 77;
	}
	XSetErrorHandler(ignorexerror);
	child = fork();
	if (child < 0) {
		perror("fork");
		return 1;
	}
	if (child == 0) {
		execl("./st", "st", "-T", "st-csi-u-test", "-g", "40x8",
		      "-e", argv[1], "--capture", argv[2], (char *)NULL);
		perror("exec ./st");
		_exit(127);
	}

	for (i = 0; i < 500 && (window == None || !reportready(argv[2])); i++) {
		XSync(dpy, False);
		window = findwindow(dpy, DefaultRootWindow(dpy), child);
		exited = waitpid(child, &status, WNOHANG);
		if (exited == child) {
			fprintf(stderr, "st exited early with status %d\n", status);
			XCloseDisplay(dpy);
			return 1;
		}
		msleep(10);
	}
	if (window == None || !reportready(argv[2])) {
		fprintf(stderr, "CSI u test window was not ready\n");
		kill(child, SIGTERM);
		waitpid(child, &status, 0);
		XCloseDisplay(dpy);
		return 1;
	}

	XRaiseWindow(dpy, window);
	for (i = 0; i < 100; i++) {
		XSetInputFocus(dpy, window, RevertToParent, CurrentTime);
		XSync(dpy, False);
		XGetInputFocus(dpy, &focus, &revert);
		if (focus == window)
			break;
		msleep(10);
	}
	if (focus != window) {
		fprintf(stderr, "could not focus CSI u test window\n");
		kill(child, SIGTERM);
		waitpid(child, &status, 0);
		XCloseDisplay(dpy);
		return 1;
	}
	msleep(300);
	tap(dpy, XK_Escape);
	tap(dpy, XK_h);
	tap(dpy, XK_j);
	tap(dpy, XK_k);
	tap(dpy, XK_l);
	modified(dpy, XK_Alt_L, NoSymbol, XK_h);
	/* Alt-j and Alt-k are deliberately retained local split shortcuts. */
	modified(dpy, XK_Alt_L, NoSymbol, XK_j);
	modified(dpy, XK_Alt_L, NoSymbol, XK_k);
	modified(dpy, XK_Alt_L, NoSymbol, XK_l);
	/* No delay: these must remain two unambiguous key events. */
	tap(dpy, XK_Escape);
	tap(dpy, XK_h);
	tap(dpy, XK_Escape);
	tap(dpy, XK_j);
	tap(dpy, XK_Escape);
	tap(dpy, XK_k);
	tap(dpy, XK_Escape);
	tap(dpy, XK_l);
	modified(dpy, XK_Control_L, NoSymbol, XK_h);
	modified(dpy, XK_Control_L, NoSymbol, XK_j);
	modified(dpy, XK_Control_L, NoSymbol, XK_k);
	modified(dpy, XK_Control_L, NoSymbol, XK_l);
	modified(dpy, XK_Alt_L, XK_Control_L, XK_l);
	modified(dpy, XK_Alt_L, XK_Shift_L, XK_l);
	modified(dpy, XK_Control_L, XK_Shift_L, XK_l);
	tap(dpy, XK_Return);
	tap(dpy, XK_Tab);
	tap(dpy, XK_BackSpace);
	XFlush(dpy);

	waitpid(child, &status, 0);
	XCloseDisplay(dpy);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		fprintf(stderr, "st test child failed with status %d\n", status);
		return 1;
	}
	return checkreport(argv[2]);
}
