#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define CSI "\033["

static struct termios saved_termios;
static int have_termios;
static volatile sig_atomic_t stopped;

static void
on_signal(int signal)
{
	(void)signal;
	stopped = 1;
}

static int
install_signals(void)
{
	struct sigaction action;

	memset(&action, 0, sizeof action);
	action.sa_handler = on_signal;
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGHUP, &action, NULL) < 0 ||
	    sigaction(SIGINT, &action, NULL) < 0 ||
	    sigaction(SIGQUIT, &action, NULL) < 0 ||
	    sigaction(SIGTERM, &action, NULL) < 0)
		return -1;
	return 0;
}

static int
writeall(int fd, const void *data, size_t size)
{
	const char *p = data;
	ssize_t written;

	while (size > 0) {
		written = write(fd, p, size);
		if (written < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		p += written;
		size -= written;
	}
	return 0;
}

static void
cleanup(void)
{
	/* Safe even when the stack is already empty. */
	writeall(STDOUT_FILENO, CSI "<u", sizeof(CSI "<u") - 1);
	if (have_termios)
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
}

static int
makeraw(void)
{
	struct termios raw;

	if (tcgetattr(STDIN_FILENO, &saved_termios) < 0)
		return -1;
	raw = saved_termios;
	cfmakeraw(&raw);
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0)
		return -1;
	have_termios = 1;
	return 0;
}

static ssize_t
readreply(unsigned char *buf, size_t size, int timeout, unsigned char final)
{
	struct pollfd pfd = { STDIN_FILENO, POLLIN, 0 };
	ssize_t nread, total = 0;

	while (!stopped && (size_t)total < size) {
		if (poll(&pfd, 1, timeout) <= 0)
			break;
		nread = read(STDIN_FILENO, buf + total, size - total);
		if (nread < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (nread == 0)
			break;
		total += nread;
		if (buf[total - 1] == final)
			break;
	}
	return total;
}

static int
expectreply(const char *request, const char *expected, unsigned char final)
{
	unsigned char buf[64];
	ssize_t len;

	if (writeall(STDOUT_FILENO, request, strlen(request)) < 0)
		return -1;
	len = readreply(buf, sizeof buf, 1000, final);
	return len == (ssize_t)strlen(expected) &&
	       memcmp(buf, expected, len) == 0 ? 0 : -1;
}

static void
printhex(FILE *fp, const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		fprintf(fp, "%02x", buf[i]);
}

static int
statechecks(FILE *report)
{
	int i;

	if (expectreply(CSI "?u", CSI "?0u", 'u') < 0 ||
	    writeall(STDOUT_FILENO, CSI "=1;1u", sizeof(CSI "=1;1u") - 1) < 0 ||
	    expectreply(CSI "?u", CSI "?1u", 'u') < 0 ||
	    writeall(STDOUT_FILENO, CSI "=1;3u", sizeof(CSI "=1;3u") - 1) < 0 ||
	    expectreply(CSI "?u", CSI "?0u", 'u') < 0 ||
	    writeall(STDOUT_FILENO, CSI ">1u", sizeof(CSI ">1u") - 1) < 0 ||
	    expectreply(CSI "?u", CSI "?1u", 'u') < 0 ||
	    writeall(STDOUT_FILENO, CSI "?1049h", sizeof(CSI "?1049h") - 1) < 0 ||
	    expectreply(CSI "?u", CSI "?0u", 'u') < 0 ||
	    writeall(STDOUT_FILENO, CSI ">1u", sizeof(CSI ">1u") - 1) < 0 ||
	    expectreply(CSI "?u", CSI "?1u", 'u') < 0 ||
	    writeall(STDOUT_FILENO, CSI "<u", sizeof(CSI "<u") - 1) < 0 ||
	    expectreply(CSI "?u", CSI "?0u", 'u') < 0 ||
	    writeall(STDOUT_FILENO, CSI "?1049l", sizeof(CSI "?1049l") - 1) < 0 ||
	    expectreply(CSI "?u", CSI "?1u", 'u') < 0 ||
	    writeall(STDOUT_FILENO, CSI "<u", sizeof(CSI "<u") - 1) < 0 ||
	    expectreply(CSI "?u", CSI "?0u", 'u') < 0 ||
	    writeall(STDOUT_FILENO, CSI "=31;1u", sizeof(CSI "=31;1u") - 1) < 0 ||
	    expectreply(CSI "?u", CSI "?1u", 'u') < 0 ||
	    writeall(STDOUT_FILENO, CSI "=1;3u", sizeof(CSI "=1;3u") - 1) < 0 ||
	    expectreply(CSI "?u", CSI "?0u", 'u') < 0)
		return -1;

	/* Mode 2 sets bits, omitted mode replaces, and empty-stack pop resets. */
	if (writeall(STDOUT_FILENO, CSI "=1;2u", sizeof(CSI "=1;2u") - 1) < 0 ||
	    expectreply(CSI "?u", CSI "?1u", 'u') < 0 ||
	    writeall(STDOUT_FILENO, CSI "=0u", sizeof(CSI "=0u") - 1) < 0 ||
	    expectreply(CSI "?u", CSI "?0u", 'u') < 0 ||
	    writeall(STDOUT_FILENO, CSI "=1u", sizeof(CSI "=1u") - 1) < 0 ||
	    writeall(STDOUT_FILENO, CSI "<u", sizeof(CSI "<u") - 1) < 0 ||
	    expectreply(CSI "?u", CSI "?0u", 'u') < 0)
		return -1;

	/* Nine pushes evict the oldest entry from the eight-entry stack. */
	if (writeall(STDOUT_FILENO, CSI "=1u", sizeof(CSI "=1u") - 1) < 0)
		return -1;
	for (i = 0; i < 9; i++)
		if (writeall(STDOUT_FILENO, CSI ">0u", sizeof(CSI ">0u") - 1) < 0)
			return -1;
	if (writeall(STDOUT_FILENO, CSI "<8u", sizeof(CSI "<8u") - 1) < 0 ||
	    expectreply(CSI "?u", CSI "?0u", 'u') < 0 ||
	    writeall(STDOUT_FILENO, "\033c", sizeof("\033c") - 1) < 0 ||
	    expectreply(CSI "?u", CSI "?0u", 'u') < 0)
		return -1;
	fputs("STATE ok\n", report);

	if (writeall(STDOUT_FILENO,
	             CSI "2;3H" CSI "s" CSI "4;5H" CSI "u",
	             sizeof(CSI "2;3H" CSI "s" CSI "4;5H" CSI "u") - 1) < 0 ||
	    expectreply(CSI "6n", CSI "2;3R", 'R') < 0)
		return -1;
	fputs("DECRC ok\n", report);
	return 0;
}

static ssize_t
readevents(unsigned char *buf, size_t size)
{
	struct pollfd pfd = { STDIN_FILENO, POLLIN, 0 };
	ssize_t nread, total = 0;
	int timeout = 5000;

	while (!stopped && (size_t)total < size && poll(&pfd, 1, timeout) > 0) {
		nread = read(STDIN_FILENO, buf + total, size - total);
		if (nread < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (nread == 0)
			break;
		total += nread;
		timeout = 300;
	}
	return total;
}

static int
capture(const char *path)
{
	unsigned char buf[512];
	ssize_t len;
	FILE *report;

	if (!(report = fopen(path, "w")))
		return 1;
	if (statechecks(report) < 0 ||
	    writeall(STDOUT_FILENO, CSI ">1u", sizeof(CSI ">1u") - 1) < 0) {
		fputs("STATE failed\n", report);
		fclose(report);
		return 1;
	}
	fputs("READY\n", report);
	fflush(report);

	len = readevents(buf, sizeof buf);
	if (len < 0) {
		fclose(report);
		return 1;
	}
	fputs("KEYS ", report);
	printhex(report, buf, len);
	fputc('\n', report);

	if (writeall(STDOUT_FILENO, CSI "<u", sizeof(CSI "<u") - 1) < 0 ||
	    expectreply(CSI "?u", CSI "?0u", 'u') < 0) {
		fputs("RESET failed\n", report);
		fclose(report);
		return 1;
	}
	fputs("RESET ok\n", report);
	fclose(report);
	return 0;
}

static int
interactive(int count)
{
	unsigned char buf[256];
	ssize_t len;
	int i;

	if (writeall(STDOUT_FILENO, CSI "?u", sizeof(CSI "?u") - 1) < 0)
		return 1;
	len = readreply(buf, sizeof buf, 1000, 'u');
	if (len <= 0)
		return 1;
	fputs("query: ", stderr);
	printhex(stderr, buf, len);
	fputc('\n', stderr);

	if (writeall(STDOUT_FILENO, CSI ">1u", sizeof(CSI ">1u") - 1) < 0)
		return 1;
	fprintf(stderr,
	        "CSI u enabled; enter %d key events (include one Chinese IME commit)\n",
	        count);
	for (i = 0; i < count; i++) {
		len = readevents(buf, sizeof buf);
		if (len <= 0)
			return 1;
		fprintf(stderr, "event %d: ", i + 1);
		printhex(stderr, buf, len);
		fputc('\n', stderr);
	}
	return 0;
}

int
main(int argc, char **argv)
{
	int count = 12, result;

	if (makeraw() < 0) {
		perror("termios");
		return 1;
	}
	if (atexit(cleanup) != 0)
		return 1;
	if (install_signals() < 0)
		return 1;
	if (argc == 3 && strcmp(argv[1], "--capture") == 0)
		result = capture(argv[2]);
	else {
		if (argc == 2)
			count = atoi(argv[1]);
		if (count < 1)
			count = 1;
		result = interactive(count);
	}
	return result;
}
