/* dwlb-leftstatus — emits void-linux logo + HH:MM + date once per wall-clock
 * minute, perfectly aligned to the minute boundary.
 *
 * Single timerfd anchored to CLOCK_REALTIME with TFD_TIMER_ABSTIME at the next
 * minute. Subsequent expirations occur on minute boundaries forever; the
 * kernel handles suspend/resume (we just see N coalesced expirations on wake
 * and emit the current time once).
 *
 * No forks, no busy loops, no clock drift. Idle CPU is unmeasurable.
 *
 * Build:  cc -O2 -Wall -o dwlb-leftstatus dwlb-leftstatus.c
 */

#include "../config.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#define LOGO    WS_LEFTST_LOGO
#define FG      WS_LEFTST_FG
#define DATE_FG WS_LEFTST_DATE_FG

static void emit(void) {
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	char hm[8], date[16];
	strftime(hm,   sizeof hm,   "%H:%M", &tm);
	strftime(date, sizeof date, "%b %d", &tm);
	printf("^fg(" FG ")  " LOGO "    ^fg(" FG ")%s    ^fg(" DATE_FG ")%s    \n",
	       hm, date);
	fflush(stdout);
}

int main(void) {
	signal(SIGPIPE, SIG_IGN);

	int tfd = timerfd_create(CLOCK_REALTIME, TFD_CLOEXEC);
	if (tfd < 0) return 1;

	time_t now = time(NULL);
	struct itimerspec spec = {
		.it_value    = { .tv_sec = now - (now % 60) + 60, .tv_nsec = 0 },
		.it_interval = { .tv_sec = 60, .tv_nsec = 0 },
	};
	if (timerfd_settime(tfd, TFD_TIMER_ABSTIME, &spec, NULL) < 0) return 1;

	sigset_t mask; sigemptyset(&mask);
	sigaddset(&mask, SIGTERM); sigaddset(&mask, SIGINT); sigaddset(&mask, SIGHUP);
	sigaddset(&mask, SIGUSR1); /* external "repaint now" trigger (e.g. on hotplug) */
	sigprocmask(SIG_BLOCK, &mask, NULL);
	int sfd = signalfd(-1, &mask, SFD_CLOEXEC);
	if (sfd < 0) return 1;

	emit();   /* initial paint */

	struct pollfd pfd[2] = { { tfd, POLLIN, 0 }, { sfd, POLLIN, 0 } };
	for (;;) {
		if (poll(pfd, 2, -1) < 0) {
			if (errno == EINTR) continue;
			return 1;
		}
		if (pfd[1].revents & POLLIN) {
			struct signalfd_siginfo si;
			if (read(sfd, &si, sizeof si) == (ssize_t)sizeof si
			    && si.ssi_signo == SIGUSR1) {
				emit();
				continue;
			}
			return 0; /* TERM/INT/HUP */
		}
		if (pfd[0].revents & POLLIN) {
			uint64_t exp;
			(void)!read(tfd, &exp, sizeof exp);
			emit();
		}
	}
}
