#define main fan_daemon_unused_main
#define ugreenctl_identity_read fixture_identity_read
#define ugreenctl_read_thermal_snapshot fixture_read_thermal_snapshot
#include "core/fan_daemon.c"
#undef ugreenctl_read_thermal_snapshot
#undef ugreenctl_identity_read
#undef main

#include <assert.h>

static unsigned int opens, writes, sleeps, closes;
static unsigned int stop_interruptions;
static bool expect_stop;
static int open_fails, on_fails, off_fails, sleep_fails;

static int fake_open(void)
{
    ++opens;
    return open_fails ? -1 : 42;
}

static ssize_t fake_write(int fd, const void *data, size_t size)
{
    const struct input_event *event = data;
    int fails = expect_stop ? off_fails : on_fails;

    assert(fd == 42 && size == sizeof(*event));
    assert(event->type == EV_SND && event->code == SND_TONE);
    assert(event->value == (expect_stop ? 0 : 1000));
    ++writes;
    if (expect_stop && stop_interruptions > 0) {
        --stop_interruptions;
        errno = EINTR;
        return -1;
    }
    expect_stop = !expect_stop;
    if (fails) {
        errno = EIO;
        return -1;
    }
    return (ssize_t)size;
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
    assert(fd == 42 && !expect_stop);
    ++closes;
    return 0;
}

int fixture_identity_read(struct ugreenctl_machine_identity *identity, char *error, size_t size)
{
    (void)error;
    (void)size;
    *identity = (struct ugreenctl_machine_identity){.product_name = "DXP4800 Plus"};
    return 0;
}

int fixture_read_thermal_snapshot(struct ugreenctl_thermal_snapshot *snapshot,
                                  char *error, size_t size)
{
    (void)snapshot;
    (void)error;
    (void)size;
    return -ENODATA;
}

static void test_once_is_silent(const struct speaker_ops *ops)
{
    char directory[] = "/tmp/ugreenctl-fand-once-XXXXXX";
    char config_path[sizeof(directory) + sizeof("/fan.conf")];
    char state_path[sizeof(directory) + sizeof("/fan.state")];
    char lock_path[sizeof(directory) + sizeof("/fan.lock")];
    char state[1024] = {0};
    char *argv[] = {"ugreenctl-fand", "--config", config_path, "--state", state_path,
                    "--ugreenctl", "/bin/false", "--plugin-dir", directory, "--once", NULL};
    FILE *file;

    assert(mkdtemp(directory) != NULL);
    (void)snprintf(config_path, sizeof(config_path), "%s/fan.conf", directory);
    (void)snprintf(state_path, sizeof(state_path), "%s/fan.state", directory);
    (void)snprintf(lock_path, sizeof(lock_path), "%s/fan.lock", directory);
    assert(setenv("UGREENCTL_FAN_CURVE_LOCK", lock_path, 1) == 0);
    file = fopen(config_path, "w");
    assert(file != NULL);
    assert(fputs("beep_on_failure=true\nfailure_threshold=1\n", file) >= 0);
    assert(fclose(file) == 0);

    assert(run_fan_daemon((int)(sizeof(argv) / sizeof(argv[0]) - 1), argv, ops) == EXIT_FAILURE);
    assert(opens == 0);
    file = fopen(state_path, "r");
    assert(file != NULL);
    assert(fread(state, 1, sizeof(state) - 1, file) > 0);
    assert(fclose(file) == 0);
    assert(strstr(state, "status=error\n") != NULL);

    assert(unsetenv("UGREENCTL_FAN_CURVE_LOCK") == 0);
    assert(unlink(lock_path) == 0);
    assert(unlink(state_path) == 0);
    assert(unlink(config_path) == 0);
    assert(rmdir(directory) == 0);
}

int main(void)
{
    const struct speaker_ops ops = {fake_open, fake_write, fake_sleep, fake_close};
    unsigned int count = 0;
    struct timespec last = {0}, now = {10, 500};
    bool attempted = false;

    test_once_is_silent(&ops);

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
    now.tv_sec = 310; off_fails = 0; stop_interruptions = 2;
    assert(maybe_beep(count, 3, 60, &now, &last, &attempted, &ops) == 1);
    assert(stop_interruptions == 0 && writes == 12 && sleeps == 4 && closes == 5);
    return 0;
}
