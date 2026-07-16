#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loader.h"
#include "ugreenctl.h"

#define UGREENCTL_VERSION "0.1.0"

struct cli_options {
    const char *model_directory;
    const char *model_id;
    bool force;
    bool apply;
};

static void usage(FILE *stream)
{
    (void)fprintf(stream,
                  "ugreenctl %s - UGREEN NAS Hardware Management Utility\n\n"
                  "Usage:\n"
                  "  ugreenctl [options] models\n"
                  "  ugreenctl [options] info\n"
                  "  ugreenctl [options] fan status\n"
                  "  ugreenctl [options] fan set <cpu|sys> <0-255>\n"
                  "  ugreenctl [options] power startup get\n"
                  "  ugreenctl [options] power startup set <on|off|restore>\n"
                  "  ugreenctl [options] led list\n\n"
                  "Options (must appear before the command):\n"
                  "  --model <id>        use a specific model plugin\n"
                  "  --plugin-dir <dir>  directory containing model .so files\n"
                  "  --apply             perform a requested write (writes are dry runs otherwise)\n"
                  "  --force             bypass model/driver/chip safeguards\n"
                  "  --version           print the version\n"
                  "  --help              print this help\n\n"
                  "The supported DXP4800 Plus model uses direct Super I/O access and requires\n"
                  "root or CAP_SYS_RAWIO. The vendor /proc/it86 driver must be unloaded.\n",
                  UGREENCTL_VERSION);
}

static const char *startup_policy_name(enum ugreenctl_startup_policy policy)
{
    switch (policy) {
    case UGREENCTL_STARTUP_ON:
        return "on";
    case UGREENCTL_STARTUP_OFF:
        return "off";
    case UGREENCTL_STARTUP_RESTORE:
        return "restore";
    default:
        return "unknown";
    }
}

static void print_capabilities(unsigned int capabilities)
{
    bool first = true;
    if (capabilities & UGREENCTL_CAP_FAN) {
        (void)fputs("fan", stdout);
        first = false;
    }
    if (capabilities & UGREENCTL_CAP_LED) {
        (void)fprintf(stdout, "%sled", first ? "" : ", ");
        first = false;
    }
    if (capabilities & UGREENCTL_CAP_POWER) {
        (void)fprintf(stdout, "%spower", first ? "" : ", ");
        first = false;
    }
    if (first) {
        (void)fputs("none (profile only)", stdout);
    }
}

static int parse_pwm(const char *text, uint8_t *pwm)
{
    char *end;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || *text == '\0' || *end != '\0' || value < 0 || value > 255) {
        return -EINVAL;
    }
    *pwm = (uint8_t)value;
    return 0;
}

static enum ugreenctl_startup_policy parse_startup_policy(const char *text)
{
    if (strcmp(text, "on") == 0) {
        return UGREENCTL_STARTUP_ON;
    }
    if (strcmp(text, "off") == 0) {
        return UGREENCTL_STARTUP_OFF;
    }
    if (strcmp(text, "restore") == 0 || strcmp(text, "last") == 0) {
        return UGREENCTL_STARTUP_RESTORE;
    }
    return UGREENCTL_STARTUP_UNKNOWN;
}

static int choose_plugin(const struct ugreenctl_plugin_set *set,
                         const struct cli_options *options,
                         const struct ugreenctl_plugin **chosen)
{
    char product_name[256];
    int dmi_result = ugreenctl_read_dmi_product(product_name, sizeof(product_name));
    const struct ugreenctl_plugin *plugin;

    if (options->model_id != NULL) {
        plugin = ugreenctl_find_plugin(set, options->model_id);
        if (plugin == NULL) {
            (void)fprintf(stderr, "error: model plugin '%s' was not found\n", options->model_id);
            return -ENOENT;
        }
        if (!options->force && dmi_result == 0) {
            const char * const *name;
            bool matches = false;
            for (name = plugin->dmi_product_names; name != NULL && *name != NULL; ++name) {
                if (strcmp(*name, product_name) == 0) {
                    matches = true;
                    break;
                }
            }
            if (!matches) {
                (void)fprintf(stderr,
                              "error: selected model '%s' does not match DMI product '%s'; "
                              "use --force only after checking the hardware\n",
                              plugin->id, product_name);
                return -ENODEV;
            }
        }
        *chosen = plugin;
        return 0;
    }
    if (dmi_result != 0) {
        (void)fprintf(stderr,
                      "error: cannot determine DMI product name; pass --model <id> after "
                      "verifying your hardware\n");
        return dmi_result;
    }
    plugin = ugreenctl_match_product(set, product_name);
    if (plugin == NULL) {
        (void)fprintf(stderr,
                      "error: no plugin matches DMI product '%s'; run 'ugreenctl models'\n",
                      product_name);
        return -ENODEV;
    }
    *chosen = plugin;
    return 0;
}

static int require_apply(const struct cli_options *options, const char *description)
{
    if (options->apply) {
        return 0;
    }
    (void)printf("dry-run: would %s\nrerun with --apply to write hardware state\n", description);
    return 1;
}

static void print_status(const struct ugreenctl_status *status)
{
    size_t index;
    (void)printf("controller: %s\n", status->controller);
    (void)printf("startup: %s\n", startup_policy_name(status->startup_policy));
    for (index = 0; index < status->fan_count; ++index) {
        const struct ugreenctl_fan_status *fan = &status->fans[index];
        (void)printf("fan %s: pwm=%u mode=%s tach=%u rpm=%lu\n",
                     fan->id, fan->pwm, fan->manual ? "manual" : "auto",
                     fan->tachometer, fan->rpm);
    }
}

static int report_plugin_error(const char *action, int result, const char *error)
{
    if (error != NULL && error[0] != '\0') {
        (void)fprintf(stderr, "error: %s: %s\n", action, error);
    } else if (result < 0) {
        (void)fprintf(stderr, "error: %s: %s\n", action, strerror(-result));
    } else {
        (void)fprintf(stderr, "error: %s failed\n", action);
    }
    return EXIT_FAILURE;
}

int main(int argc, char **argv)
{
    struct cli_options options = {
        .model_directory = ugreenctl_default_model_dir(),
        .model_id = NULL,
        .force = false,
        .apply = false
    };
    struct ugreenctl_plugin_set plugins;
    const struct ugreenctl_plugin *plugin = NULL;
    struct ugreenctl_request request;
    char error[256] = {0};
    int arg = 1;
    int result;

    while (arg < argc && strncmp(argv[arg], "--", 2) == 0) {
        if (strcmp(argv[arg], "--help") == 0) {
            usage(stdout);
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[arg], "--version") == 0) {
            (void)puts(UGREENCTL_VERSION);
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[arg], "--force") == 0) {
            options.force = true;
            ++arg;
            continue;
        }
        if (strcmp(argv[arg], "--apply") == 0) {
            options.apply = true;
            ++arg;
            continue;
        }
        if ((strcmp(argv[arg], "--model") == 0 || strcmp(argv[arg], "--plugin-dir") == 0) &&
            arg + 1 < argc) {
            if (strcmp(argv[arg], "--model") == 0) {
                options.model_id = argv[arg + 1];
            } else {
                options.model_directory = argv[arg + 1];
            }
            arg += 2;
            continue;
        }
        (void)fprintf(stderr, "error: unknown or incomplete option '%s'\n", argv[arg]);
        usage(stderr);
        return EXIT_FAILURE;
    }
    if (arg >= argc) {
        usage(stderr);
        return EXIT_FAILURE;
    }

    result = ugreenctl_plugins_open(options.model_directory, &plugins, error, sizeof(error));
    if (result != 0) {
        return report_plugin_error("load model plugins", result, error);
    }
    if (strcmp(argv[arg], "models") == 0 && arg + 1 == argc) {
        size_t index;
        for (index = 0; index < plugins.count; ++index) {
            const struct ugreenctl_plugin *item = plugins.items[index].plugin;
            (void)printf("%-14s %-36s ", item->id, item->display_name);
            print_capabilities(item->capabilities);
            (void)putchar('\n');
        }
        ugreenctl_plugins_close(&plugins);
        return EXIT_SUCCESS;
    }

    result = choose_plugin(&plugins, &options, &plugin);
    if (result != 0) {
        ugreenctl_plugins_close(&plugins);
        return EXIT_FAILURE;
    }
    request.force = options.force;

    if (strcmp(argv[arg], "info") == 0 && arg + 1 == argc) {
        struct ugreenctl_status status;
        if (plugin->read_status == NULL) {
            (void)fprintf(stderr, "error: %s is a profile-only plugin; hardware commands are not verified\n",
                          plugin->id);
            result = EXIT_FAILURE;
        } else {
            result = plugin->read_status(&request, &status, error, sizeof(error));
            if (result == 0) {
                (void)printf("model: %s (%s)\n", plugin->id, plugin->display_name);
                print_status(&status);
                result = EXIT_SUCCESS;
            } else {
                result = report_plugin_error("read hardware status", result, error);
            }
        }
    } else if (strcmp(argv[arg], "fan") == 0 && arg + 1 < argc &&
               strcmp(argv[arg + 1], "status") == 0 && arg + 2 == argc) {
        struct ugreenctl_status status;
        if (plugin->read_status == NULL) {
            (void)fprintf(stderr, "error: fan status is not verified for %s\n", plugin->id);
            result = EXIT_FAILURE;
        } else {
            result = plugin->read_status(&request, &status, error, sizeof(error));
            if (result == 0) {
                size_t index;
                for (index = 0; index < status.fan_count; ++index) {
                    const struct ugreenctl_fan_status *fan = &status.fans[index];
                    (void)printf("%s: pwm=%u mode=%s tach=%u rpm=%lu\n",
                                 fan->id, fan->pwm, fan->manual ? "manual" : "auto",
                                 fan->tachometer, fan->rpm);
                }
                result = EXIT_SUCCESS;
            } else {
                result = report_plugin_error("read fan status", result, error);
            }
        }
    } else if (strcmp(argv[arg], "fan") == 0 && arg + 4 == argc &&
               strcmp(argv[arg + 1], "set") == 0) {
        uint8_t pwm;
        if (parse_pwm(argv[arg + 3], &pwm) != 0 || plugin->set_fan_pwm == NULL) {
            (void)fprintf(stderr, "error: fan control is not available for %s\n", plugin->id);
            result = EXIT_FAILURE;
        } else if (require_apply(&options, "set the fan PWM") != 0) {
            result = EXIT_SUCCESS;
        } else {
            result = plugin->set_fan_pwm(&request, argv[arg + 2], pwm, error, sizeof(error));
            if (result == 0) {
                (void)printf("%s fan PWM set to %u\n", argv[arg + 2], pwm);
                result = EXIT_SUCCESS;
            } else {
                result = report_plugin_error("set fan PWM", result, error);
            }
        }
    } else if (strcmp(argv[arg], "power") == 0 && arg + 2 < argc &&
               strcmp(argv[arg + 1], "startup") == 0 &&
               strcmp(argv[arg + 2], "get") == 0 && arg + 3 == argc) {
        struct ugreenctl_status status;
        if (plugin->read_status == NULL) {
            (void)fprintf(stderr, "error: startup policy is not verified for %s\n", plugin->id);
            result = EXIT_FAILURE;
        } else {
            result = plugin->read_status(&request, &status, error, sizeof(error));
            if (result == 0) {
                (void)puts(startup_policy_name(status.startup_policy));
                result = EXIT_SUCCESS;
            } else {
                result = report_plugin_error("read startup policy", result, error);
            }
        }
    } else if (strcmp(argv[arg], "power") == 0 && arg + 3 < argc &&
               strcmp(argv[arg + 1], "startup") == 0 &&
               strcmp(argv[arg + 2], "set") == 0 && arg + 4 == argc) {
        enum ugreenctl_startup_policy policy = parse_startup_policy(argv[arg + 3]);
        if (policy == UGREENCTL_STARTUP_UNKNOWN || plugin->set_startup_policy == NULL) {
            (void)fprintf(stderr, "error: use startup policy on, off, or restore\n");
            result = EXIT_FAILURE;
        } else if (require_apply(&options, "change the AC recovery startup policy") != 0) {
            result = EXIT_SUCCESS;
        } else {
            result = plugin->set_startup_policy(&request, policy, error, sizeof(error));
            if (result == 0) {
                (void)printf("startup policy set to %s\n", startup_policy_name(policy));
                result = EXIT_SUCCESS;
            } else {
                result = report_plugin_error("set startup policy", result, error);
            }
        }
    } else if (strcmp(argv[arg], "led") == 0 && arg + 1 < argc &&
               strcmp(argv[arg + 1], "list") == 0 && arg + 2 == argc) {
        (void)fprintf(stderr, "error: LED control is not verified for %s\n", plugin->id);
        result = EXIT_FAILURE;
    } else {
        (void)fprintf(stderr, "error: invalid command\n");
        usage(stderr);
        result = EXIT_FAILURE;
    }

    ugreenctl_plugins_close(&plugins);
    return result;
}
