#define main fan_daemon_unused_main
#include "core/fan_daemon.c"
#undef main

#include <assert.h>

static unsigned int opens, writes, sleeps, closes;
static int open_fails, on_fails, off_fails, sleep_fails;

static int fake_open(void)
{
    ++opens;
    return open_fails ? -1 : 42;
}

static ssize_t fake_write(int fd, const void *data, size_t size)
{
    const struct input_event *event = data;
    assert(fd == 42 && size == sizeof(*event));
    assert(event->type == EV_SND && event->code == SND_TONE);
    assert(event->value == (writes % 2 == 0 ? 1000 : 0));
    ++writes;
    return (event->value ? on_fails : off_fails) ? -1 : (ssize_t)size;
}

static int fake_sleep(const struct timespec *duration, struct timespec *remaining)
{
    assert(duration->tv_sec == 0 && duration->tv_nsec == 100000000L && remaining == NULL);
    ++sleeps;
    if (sleep_fails) { errno = EINTR; return -1; }
    return 0;
}

static int fake_close(int fd)
{
    assert(fd == 42);
    ++closes;
    return 0;
}

int main(void)
{
    const struct speaker_ops ops = {fake_open, fake_write, fake_sleep, fake_close};
    unsigned int count = 0;
    struct timespec last = {0}, now = {10, 500};
    bool attempted = false;

    count_cycle_failure(&count, true); assert(count == 1);
    count_cycle_failure(&count, true); assert(count == 2);
    assert(maybe_beep(count, 3, 60, &now, &last, &attempted, &ops) == 0 && opens == 0);
    count_cycle_failure(&count, true); assert(count == 3);
    assert(maybe_beep(count, 3, 60, &now, &last, &attempted, &ops) == 1);
    assert(opens == 1 && writes == 2 && sleeps == 1 && closes == 1);
    now = (struct timespec){70, 499};
    assert(maybe_beep(count, 3, 60, &now, &last, &attempted, &ops) == 0);
    now.tv_nsec = 500;
    open_fails = 1;
    assert(maybe_beep(count, 3, 60, &now, &last, &attempted, &ops) == -1);
    assert(opens == 2 && writes == 2 && closes == 1);
    now.tv_sec = 71;
    assert(maybe_beep(count, 3, 60, &now, &last, &attempted, &ops) == 0);
    count_cycle_failure(&count, false); assert(count == 0);
    count_cycle_failure(&count, true); assert(count == 1);
    count = UINT_MAX; count_cycle_failure(&count, true); assert(count == UINT_MAX);
    now = (struct timespec){130, 500};
    open_fails = 0; on_fails = 1;
    assert(maybe_beep(count, 3, 60, &now, &last, &attempted, &ops) == -1);
    assert(writes == 4 && sleeps == 1 && closes == 2);
    now.tv_sec = 190; on_fails = 0; sleep_fails = 1;
    assert(maybe_beep(count, 3, 60, &now, &last, &attempted, &ops) == -1);
    assert(writes == 6 && sleeps == 2 && closes == 3);
    now.tv_sec = 250; sleep_fails = 0; off_fails = 1;
    assert(maybe_beep(count, 3, 60, &now, &last, &attempted, &ops) == -1);
    assert(writes == 8 && sleeps == 3 && closes == 4);
    now.tv_sec = 200;
    assert(!beep_due(&now, &last, true, 60));
    return 0;
}
