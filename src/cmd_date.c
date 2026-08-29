// cmd_date.c — print the host's current local date/time
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
// usage: date

#include <stdio.h>
#include <time.h>

static int local_tm(time_t now, struct tm *out)
{
#if defined(_WIN32)
    return localtime_s(out, &now);
#else
    return localtime_r(&now, out) ? 0 : -1;
#endif
}

int cmd_date(int argc, char **argv)
{
    time_t now;
    struct tm tmv;
    char text[32];

    (void)argv;

    if (argc != 1) {
        fprintf(stderr, "usage: date\n");
        return 1;
    }

    now = time(NULL);
    if (now == (time_t)-1 || local_tm(now, &tmv) != 0) {
        fprintf(stderr, "date: cannot read host clock\n");
        return 1;
    }

    if (strftime(text, sizeof text, "%Y-%m-%d %H:%M:%S", &tmv) == 0) {
        fprintf(stderr, "date: cannot format host clock\n");
        return 1;
    }

    puts(text);
    return 0;
}
