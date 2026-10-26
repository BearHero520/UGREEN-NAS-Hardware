#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "led/led.h"
#include "led/led_protocol.h"
#include "led/led_test.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ftw.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); \
} } while (0)

struct fixture {
    struct ugreenctl_led_identity machine;
    int identity_error;
    struct ugreenctl_led_value state[10];
    uint16_t chip_id;
    int chip_error, owner_error, lock_error;
    int locked, locks, unlocks, reads, writes, chip_reads;
    bool corrupt, ignore_writes, mode_resets_rgb;
    int ack_failures;
    uint8_t last_frame[12];
    volatile sig_atomic_t *cancel;
};

static int owners(void *opaque) { return ((struct fixture *)opaque)->owner_error; }
static int lock(void *opaque)
{
    struct fixture *fixture = opaque;
    ++fixture->locks;
    if (fixture->lock_error) return fixture->lock_error;
    CHECK(!fixture->locked);
    fixture->locked = 1;
    return 0;
}
static void unlock(void *opaque)
{
    struct fixture *fixture = opaque;
    CHECK(fixture->locked);
    fixture->locked = 0; ++fixture->unlocks;
}
static int chip(void *opaque, uint16_t *id)
{
    struct fixture *fixture = opaque;
    CHECK(fixture->locked);
    ++fixture->chip_reads;
    if (fixture->chip_error) return fixture->chip_error;
    *id = fixture->chip_id;
    return 0;
}
static void encode_status(const struct ugreenctl_led_value *value, uint8_t frame[11])
{
    unsigned int sum = 0, cycle = value->on_ms + value->off_ms;
    size_t i;
    frame[0] = (uint8_t)value->mode;
    frame[1] = (uint8_t)value->brightness;
    frame[2] = (uint8_t)value->red; frame[3] = (uint8_t)value->green;
    frame[4] = (uint8_t)value->blue;
    frame[5] = (uint8_t)(cycle >> 8); frame[6] = (uint8_t)cycle;
    frame[7] = (uint8_t)(value->on_ms >> 8); frame[8] = (uint8_t)value->on_ms;
    for (i = 0; i < 9; ++i) sum += frame[i];
    frame[9] = (uint8_t)(sum >> 8); frame[10] = (uint8_t)sum;
}
static int read_status(void *opaque, uint8_t id, uint8_t frame[11])
{
    struct fixture *fixture = opaque;
    CHECK(fixture->locked); CHECK(id < 10);
    ++fixture->reads;
    encode_status(&fixture->state[id], frame);
    if (fixture->corrupt) frame[10] ^= 1;
    return 0;
}
static int write_command(void *opaque, uint8_t id, const uint8_t frame[12])
{
    struct fixture *fixture = opaque;
    struct ugreenctl_led_value *state = &fixture->state[id];
    uint8_t normalized[12];
    unsigned int cycle;
    CHECK(fixture->locked); CHECK(id < 10);
    ++fixture->writes;
    memcpy(fixture->last_frame, frame, 12);
    if (frame[0] == 0xa0) {
        normalized[0] = id; memcpy(normalized+1,frame,11); frame = normalized;
    }
    CHECK(id == frame[0]);
    if (fixture->ignore_writes) return 0;
    switch (frame[5]) {
    case 1: state->brightness = frame[6]; break;
    case 2: state->red = frame[6]; state->green = frame[7]; state->blue = frame[8]; break;
    case 3:
        state->mode = (enum ugreenctl_led_mode)frame[6];
        if (fixture->mode_resets_rgb) state->red = state->green = state->blue = 0;
        break;
    case 4:
    case 5:
        state->mode = frame[5] == 4 ? UGREENCTL_LED_MODE_BLINK : UGREENCTL_LED_MODE_BREATH;
        cycle = ((unsigned int)frame[6] << 8) | frame[7];
        state->on_ms = ((unsigned int)frame[8] << 8) | frame[9];
        state->off_ms = cycle - state->on_ms;
        break;
    default: CHECK(0);
    }
    return 0;
}
static int ack(void *opaque)
{
    struct fixture *fixture = opaque;
    CHECK(fixture->locked);
    if (fixture->ack_failures > 0) { --fixture->ack_failures; return -EIO; }
    return 0;
}
static void pause_ms(void *opaque, unsigned int milliseconds)
{
    struct fixture *fixture = opaque;
    CHECK(fixture->locked);
    if (milliseconds == 100 && fixture->cancel != NULL) *fixture->cancel = 1;
}
static const struct ugreenctl_led_test_io io = {
    owners, lock, unlock, chip, read_status, write_command, ack, pause_ms, NULL
};

static int read_machine(void *opaque,struct ugreenctl_led_identity *identity,char *error,size_t size)
{
    struct fixture *fixture=opaque;(void)error;(void)size;
    if (fixture->identity_error) return fixture->identity_error;
    *identity=fixture->machine;return 0;
}

static void initialize(struct fixture *fixture)
{
    unsigned int i;
    memset(fixture, 0, sizeof(*fixture));
    fixture->chip_id = 0xc5b2;
    for (i = 0; i < 10; ++i) {
        fixture->state[i].mode = UGREENCTL_LED_MODE_ON;
        fixture->state[i].brightness = 180;
        fixture->state[i].red = fixture->state[i].green = fixture->state[i].blue = 255;
    }
}

static int open_fixture(struct fixture *fixture, bool force, bool apply,
                        struct ugreenctl_led_session **session)
{
    struct ugreenctl_led_identity identity = {0};
    const struct ugreenctl_led_access access = {force, apply};
    char error[256];
    (void)snprintf(identity.product_name, sizeof(identity.product_name), "DXP4800 Plus");
    return ugreenctl_led_test_open(&identity, &access, &io, fixture, session, error, sizeof(error));
}

static struct ugreenctl_led_patch rgb_patch(void)
{
    struct ugreenctl_led_patch patch = {0};
    patch.fields = UGREENCTL_LED_PATCH_RGB;
    patch.value.red = 10; patch.value.green = 20; patch.value.blue = 30;
    return patch;
}

static void test_process_identity(void)
{
    const char *sudo_args[] = {"sudo", "ugreen_leds_cli", "all", "-on"};
    const char *env_args[] = {"env", "ugreen_leds_cli", "all", "-status"};
    const char *unrelated_args[] = {"some-program", "ugreen_leds_cli"};
    const char *cli_args[] = {"/usr/local/bin/ugreen_leds_cli", "all", "-status"};
    const char *script_args[] = {"/bin/sh", "/usr/bin/ugreen-diskiomon"};
    const char *shell_flags[] = {"/bin/bash", "-o", "pipefail", "-x", "--", "/usr/bin/ugreen-netdevmon"};
    const char *shell_text[] = {"/bin/bash", "-lc", "printf '%s'", "ugreen_leds_cli"};
    const char *python_script[] = {"python3", "-u", "/usr/bin/ugreen-diskiomon"};
    const char *python_module[] = {"python3", "-m", "some_program", "ugreen_leds_cli"};
    CHECK(!ugreenctl_led_test_process_is_owner("sudo", "/usr/bin/sudo", sudo_args, 4));
    CHECK(!ugreenctl_led_test_process_is_owner("env", "/usr/bin/env", env_args, 4));
    CHECK(!ugreenctl_led_test_process_is_owner("some-program", "/usr/bin/some-program", unrelated_args, 2));
    CHECK(ugreenctl_led_test_process_is_owner("ugreen_leds_cli", "/usr/local/bin/ugreen_leds_cli", cli_args, 3));
    CHECK(ugreenctl_led_test_process_is_owner("sh", "/bin/dash", script_args, 2));
    CHECK(ugreenctl_led_test_process_is_owner("bash", "/bin/bash", shell_flags, 6));
    CHECK(!ugreenctl_led_test_process_is_owner("bash", "/bin/bash", shell_text, 4));
    CHECK(ugreenctl_led_test_process_is_owner("python3", "/usr/bin/python3", python_script, 3));
    CHECK(!ugreenctl_led_test_process_is_owner("python3", "/usr/bin/python3", python_module, 4));
    CHECK(ugreenctl_led_test_process_is_owner("ugreen-netdevmo", NULL, NULL, 0));
}

static void test_wire_protocol(void)
{
    /* Published example: purple, brightness 180, blink 400/600 ms. */
    uint8_t status[11] = {2, 0xb4, 0xff, 0, 0xff, 3, 0xe8, 1, 0x90, 4, 0x30};
    const uint8_t expected[12] = {3, 0xa0, 1, 0, 0, 2, 10, 20, 30, 0, 0, 0xdf};
    const uint8_t parameters[4] = {10, 20, 30, 0};
    uint8_t frame[12], invalid[11] = {0};
    struct ugreenctl_led_value value;
    CHECK(ugreenctl_led_decode(status, sizeof(status), &value) == 0);
    CHECK(value.mode == UGREENCTL_LED_MODE_BLINK && value.on_ms == 400 && value.off_ms == 600);
    CHECK(value.red == 255 && value.blue == 255 && value.brightness == 180);
    CHECK(ugreenctl_led_encode(3, 2, parameters, frame) == 0);
    CHECK(memcmp(expected, frame, sizeof(expected)) == 0);
    status[10] ^= 1;
    CHECK(ugreenctl_led_decode(status, sizeof(status), &value) == -EBADMSG);
    CHECK(ugreenctl_led_decode(invalid, sizeof(invalid), &value) == 0);
    CHECK(value.mode == UGREENCTL_LED_MODE_OFF && value.brightness == 0 && value.red == 0);
    CHECK(ugreenctl_led_decode(invalid, 10, &value) == -EINVAL);
    /* Checksum is valid, but on time exceeds the full cycle. */
    memset(&value, 0, sizeof(value));
    value.mode = UGREENCTL_LED_MODE_BLINK; value.brightness = 1; value.on_ms = 2;
    encode_status(&value, invalid);
    invalid[6] = 1; invalid[10] -= 1;
    CHECK(ugreenctl_led_decode(invalid, sizeof(invalid), &value) == -EBADMSG);
    invalid[0] = 4; invalid[10] += 2;
    CHECK(ugreenctl_led_decode(invalid, sizeof(invalid), &value) == -EBADMSG);
}

static void test_profile_selection(void)
{
    struct ugreenctl_led_descriptor descriptors[10];
    const struct ugreenctl_led_profile *profile;
    CHECK(ugreenctl_led_profile_find("DXP4800S", "") != NULL);
    CHECK(ugreenctl_led_profile_find("DXP480T Plus", "") != NULL);
    CHECK(ugreenctl_led_profile_find("DXP4800 GT", "") != NULL);
    profile=ugreenctl_led_profile_find("DXP2800 GT", "");
    CHECK(profile && !strcmp(profile->protocol,"cs201x-sio") && profile->expected_chip_id==0x2011);
    CHECK(ugreenctl_led_profile_find("DXP2800", "DXP2800 V1.2 251215") != NULL);
    CHECK(ugreenctl_led_profile_find("DXP2800", "") == NULL);
    CHECK(ugreenctl_led_profile_find("DXP2800", "EM_DXP2800_V1.0.25") == NULL);
    CHECK(ugreenctl_led_profile_find("DXP2800", "EM_DXP2800_V1.0.24") != NULL);
    CHECK(ugreenctl_led_profile_find("DXP4800 Pro", "") != NULL);
    profile = ugreenctl_led_profile_find("DXP8800 Plus", "");
    CHECK(profile != NULL && profile->experimental);
    CHECK(ugreenctl_led_descriptors(profile, descriptors, 10) == 10);
    CHECK(descriptors[9].id == 9 && descriptors[9].bay == 8 && strcmp(descriptors[9].name, "disk8") == 0);
    profile=ugreenctl_led_profile_find("iDX6011 Pro","");
    CHECK(ugreenctl_led_descriptors(profile,descriptors,10)==9);
    CHECK(descriptors[2].id==2 && strcmp(descriptors[2].name,"netdev2")==0 && descriptors[2].bay==0);
    CHECK(descriptors[3].id==3 && descriptors[3].bay==1);
    profile=ugreenctl_led_profile_find("DXP480T Plus","");
    CHECK(ugreenctl_led_descriptors(profile,descriptors,10)==1);
    CHECK(!descriptors[0].rgb && descriptors[0].color_mask!=0);
}

static void test_smbus_and_capabilities(void)
{
    struct fixture fixture;
    struct ugreenctl_led_identity identity={0};
    const struct ugreenctl_led_access access={true,true};
    struct ugreenctl_led_session *session;
    struct ugreenctl_led_patch patch=rgb_patch();
    struct ugreenctl_led_value readback;
    struct ugreenctl_led_descriptor descriptor;
    uint8_t frame[12];
    const uint8_t parameters[4]={10,20,30,0};
    char error[256];
    CHECK(ugreenctl_led_encode_smbus(2,parameters,false,frame)==0);
    CHECK(frame[0]==0xa0 && frame[4]==2 && frame[9]==0 && frame[10]==0xdf);
    CHECK(ugreenctl_led_encode_smbus(2,parameters,true,frame)==0);
    CHECK(frame[9]==0xdf && frame[10]==0);
    initialize(&fixture);
    (void)snprintf(identity.product_name,sizeof(identity.product_name),"DXP6800");
    CHECK(ugreenctl_led_test_open(&identity,&access,&io,&fixture,&session,error,sizeof(error))==0);
    CHECK(ugreenctl_led_identity_confirmed(session));
    CHECK(ugreenctl_led_apply(session,2,&patch,&readback,error,sizeof(error))==0);
    CHECK(fixture.last_frame[0]==0xa0 && fixture.last_frame[9]==0 && fixture.last_frame[10]==0xdf);
    ugreenctl_led_close(session);
    initialize(&fixture);
    (void)snprintf(identity.product_name,sizeof(identity.product_name),"DXP2800");
    (void)snprintf(identity.product_version,sizeof(identity.product_version),"DXP2800 V1.2 251215");
    CHECK(ugreenctl_led_test_open(&identity,&access,&io,&fixture,&session,error,sizeof(error))==0);
    CHECK(ugreenctl_led_apply(session,2,&patch,&readback,error,sizeof(error))==0);
    CHECK(fixture.last_frame[9]==0xdf && fixture.last_frame[10]==0);
    ugreenctl_led_close(session);
    CHECK(ugreenctl_led_descriptors(ugreenctl_led_profile_find("DXP480T Plus",""),&descriptor,1)==1);
    CHECK(ugreenctl_led_validate_target_patch(&descriptor,&patch,error,sizeof(error))==-ENOTSUP);
    patch.fields=UGREENCTL_LED_PATCH_COLOR;patch.value.color=UGREENCTL_LED_COLOR_WHITE;
    CHECK(ugreenctl_led_validate_target_patch(&descriptor,&patch,error,sizeof(error))==0);
    patch.value.color=UGREENCTL_LED_COLOR_GREEN;
    CHECK(ugreenctl_led_validate_target_patch(&descriptor,&patch,error,sizeof(error))==-ENOTSUP);
}

static void test_no_unsafe_access(void)
{
    struct fixture fixture;
    struct ugreenctl_led_session *session;
    struct ugreenctl_led_patch patch = rgb_patch();
    struct ugreenctl_led_value value;
    char error[256];
    initialize(&fixture); fixture.owner_error = -EBUSY;
    CHECK(open_fixture(&fixture, true, true, &session) == -EBUSY);
    CHECK(fixture.locks == 0 && fixture.chip_reads == 0 && fixture.writes == 0);
    initialize(&fixture); fixture.lock_error = -EBUSY;
    CHECK(open_fixture(&fixture, true, true, &session) == -EBUSY);
    CHECK(fixture.reads == 0 && fixture.chip_reads == 0 && fixture.writes == 0);
    initialize(&fixture); fixture.corrupt = true;
    CHECK(open_fixture(&fixture, true, true, &session) == -EBADMSG);
    CHECK(fixture.writes == 0 && fixture.unlocks == 1);
    initialize(&fixture);
    CHECK(open_fixture(&fixture, false, true, &session) == 0);
    CHECK(ugreenctl_led_apply(session, 0, &patch, &value, error, sizeof(error)) == -EPERM);
    CHECK(fixture.writes == 0); ugreenctl_led_close(session);
    initialize(&fixture);
    CHECK(open_fixture(&fixture, true, false, &session) == 0);
    CHECK(ugreenctl_led_apply(session, 0, &patch, &value, error, sizeof(error)) == -EPERM);
    CHECK(fixture.writes == 0); ugreenctl_led_close(session);
    initialize(&fixture); fixture.chip_error = -EIO;
    CHECK(open_fixture(&fixture, true, true, &session) == 0);
    CHECK(ugreenctl_led_chip_id(session) == 0);
    CHECK(ugreenctl_led_read(session, 0, &value, error, sizeof(error)) == 0);
    CHECK(ugreenctl_led_apply(session, 0, &patch, &value, error, sizeof(error)) == -ENOTSUP);
    CHECK(fixture.writes == 0); ugreenctl_led_close(session);
    initialize(&fixture); fixture.chip_id = 0xffff;
    CHECK(open_fixture(&fixture, true, true, &session) == 0);
    CHECK(ugreenctl_led_apply(session, 0, &patch, &value, error, sizeof(error)) == -ENOTSUP);
    CHECK(fixture.writes == 0); ugreenctl_led_close(session);
    initialize(&fixture);
    CHECK(open_fixture(&fixture, true, true, &session) == 0);
    fixture.owner_error = -EBUSY;
    CHECK(ugreenctl_led_apply(session, 0, &patch, &value, error, sizeof(error)) == -EBUSY);
    CHECK(fixture.writes == 0); fixture.owner_error = 0;
    fixture.chip_id = 0;
    CHECK(ugreenctl_led_apply(session, 0, &patch, &value, error, sizeof(error)) == -ENOTSUP);
    CHECK(fixture.writes == 0); fixture.chip_id = 0xc5b2;
    CHECK(ugreenctl_led_apply(session, 6, &patch, &value, error, sizeof(error)) == -EINVAL);
    CHECK(fixture.writes == 0); ugreenctl_led_close(session);
}

static void test_validation_and_readback(void)
{
    struct fixture fixture;
    struct ugreenctl_led_session *session;
    struct ugreenctl_led_patch patch = rgb_patch();
    struct ugreenctl_led_value value;
    char error[256];
    initialize(&fixture);
    CHECK(open_fixture(&fixture, true, true, &session) == 0);
    patch.value.red = 256;
    CHECK(ugreenctl_led_apply(session, 0, &patch, &value, error, sizeof(error)) == -EINVAL);
    CHECK(fixture.writes == 0);
    patch = rgb_patch();
    CHECK(ugreenctl_led_apply(session, 2, &patch, &value, error, sizeof(error)) == 0);
    CHECK(fixture.writes == 1 && value.red == 10 && value.green == 20 && value.blue == 30);
    fixture.ignore_writes = true;
    CHECK(ugreenctl_led_apply(session, 3, &patch, &value, error, sizeof(error)) == -EIO);
    fixture.ignore_writes = false;
    fixture.ack_failures = 1;
    CHECK(ugreenctl_led_apply(session, 3, &patch, &value, error, sizeof(error)) == -EIO);
    patch.fields = UGREENCTL_LED_PATCH_MODE | UGREENCTL_LED_PATCH_TIMING;
    patch.value.mode = UGREENCTL_LED_MODE_BREATH;
    patch.value.on_ms = 65535; patch.value.off_ms = 1;
    CHECK(ugreenctl_led_apply(session, 0, &patch, &value, error, sizeof(error)) == -EINVAL);
    patch.value.off_ms = 0;
    CHECK(ugreenctl_led_apply(session, 0, &patch, &value, error, sizeof(error)) == 0);
    CHECK(value.on_ms == 65535 && value.off_ms == 0 && value.mode == UGREENCTL_LED_MODE_BREATH);
    patch.value.on_ms = 0; patch.value.off_ms = 65536;
    CHECK(ugreenctl_led_validate_patch(&patch, error, sizeof(error)) == -EINVAL);
    patch.fields = UGREENCTL_LED_PATCH_MODE;
    CHECK(ugreenctl_led_validate_patch(&patch, error, sizeof(error)) == -EINVAL);
    patch.value.mode = UGREENCTL_LED_MODE_OFF;
    CHECK(ugreenctl_led_apply(session, 0, &patch, &value, error, sizeof(error)) == 0);
    CHECK(value.mode == UGREENCTL_LED_MODE_OFF);
    patch = rgb_patch();
    patch.fields |= UGREENCTL_LED_PATCH_MODE;
    patch.value.mode = UGREENCTL_LED_MODE_ON;
    fixture.mode_resets_rgb = true;
    CHECK(ugreenctl_led_apply(session, 0, &patch, &value, error, sizeof(error)) == -EIO);
    ugreenctl_led_close(session);
    CHECK(fixture.unlocks == 1 && fixture.locked == 0);
}

static void test_identify_restores(void)
{
    struct fixture fixture;
    struct ugreenctl_led_session *session;
    struct ugreenctl_led_value original;
    volatile sig_atomic_t cancelled = 0;
    char error[256];
    initialize(&fixture);
    fixture.state[2].mode = UGREENCTL_LED_MODE_BREATH;
    fixture.state[2].brightness = 23; fixture.state[2].red = 2;
    fixture.state[2].green = 3; fixture.state[2].blue = 4;
    fixture.state[2].on_ms = 800; fixture.state[2].off_ms = 1200;
    original = fixture.state[2];
    fixture.cancel = &cancelled;
    CHECK(open_fixture(&fixture, true, true, &session) == 0);
    CHECK(ugreenctl_led_identify(session, 2, 3, &cancelled, error, sizeof(error)) == 0);
    CHECK(cancelled == 1 && fixture.writes == 6);
    CHECK(memcmp(&original, &fixture.state[2], sizeof(original)) == 0);
    fixture.ack_failures = 1;
    CHECK(ugreenctl_led_identify(session, 2, 1, &cancelled, error, sizeof(error)) == -EIO);
    CHECK(memcmp(&original, &fixture.state[2], sizeof(original)) == 0);
    CHECK(ugreenctl_led_identify(session, 2, 0, &cancelled, error, sizeof(error)) == -EINVAL);
    fixture.state[2].mode = UGREENCTL_LED_MODE_ON;
    fixture.state[2].on_ms = 123; fixture.state[2].off_ms = 456;
    original = fixture.state[2];
    CHECK(ugreenctl_led_identify(session, 2, 1, &cancelled, error, sizeof(error)) == 0);
    CHECK(memcmp(&original, &fixture.state[2], sizeof(original)) == 0);
    memset(&fixture.state[2], 0, sizeof(fixture.state[2]));
    original = fixture.state[2];
    CHECK(ugreenctl_led_identify(session, 2, 1, &cancelled, error, sizeof(error)) == 0);
    CHECK(memcmp(&original, &fixture.state[2], sizeof(original)) == 0);
    ugreenctl_led_close(session);
    CHECK(fixture.unlocks == 1);
}

static void make_directories(const char *path)
{
    char copy[4096],*position;
    (void)snprintf(copy,sizeof(copy),"%s",path);
    for (position=copy+1;*position;++position) if (*position=='/') {
        *position='\0';CHECK(mkdir(copy,0700)==0 || errno==EEXIST);*position='/';
    }
    CHECK(mkdir(copy,0700)==0 || errno==EEXIST);
}

static void fixture_file(const char *path,const char *text)
{
    FILE *file=fopen(path,"w");CHECK(file!=NULL);
    CHECK(fputs(text,file)>=0);CHECK(fclose(file)==0);
}

static int remove_fixture(const char *path,const struct stat *metadata,int type,struct FTW *walk)
{
    (void)metadata;(void)type;(void)walk;return remove(path);
}

static void n76_driver_observer(void *opaque,const char *path,const char *text)
{
    char file[4096],*slash;unsigned long value;
    (void)opaque;
    if (strlen(path)>=8 && !strcmp(path+strlen(path)-8,"/trigger")) {
        char triggers[256];
        (void)snprintf(triggers,sizeof(triggers),"[%s] %s %s %s",text,strcmp(text,"none") ? "none" : "",strcmp(text,"normal") ? "normal" : "",strcmp(text,"timer") ? "timer" : "");
        fixture_file(path,triggers);return;
    }
    if (strlen(path)<11 || strcmp(path+strlen(path)-11,"/brightness")) return;
    value=strtoul(text,NULL,10);if (value<2) return;
    (void)snprintf(file,sizeof(file),"%s",path);slash=strrchr(file,'/');CHECK(slash);strcpy(slash+1,"rbrightness");
    fixture_file(file,text);
}

struct sysfs_guard_observer {
    struct fixture *fixture;
    void (*driver)(void *,const char *,const char *);
    unsigned int writes,none_writes;
    bool claim_owner,owner_during_restore,unknown_restoration;
};

static void observe_sysfs_guard(void *opaque,const char *path,const char *text)
{
    struct sysfs_guard_observer *guard=opaque;
    ++guard->writes;
    guard->driver(NULL,path,text);
    if (guard->claim_owner && guard->writes==1) guard->fixture->owner_error=-EBUSY;
    if (strlen(path)>=8 && !strcmp(path+strlen(path)-8,"/trigger") && !strcmp(text,"none")) {
        ++guard->none_writes;
        if (guard->owner_during_restore && guard->none_writes==2) guard->fixture->owner_error=-EBUSY;
        if (guard->unknown_restoration && guard->none_writes==3)
            fixture_file(path,"[heartbeat] none normal timer\n");
    }
}

static void reset_sysfs_guard_state(const char *node,bool n76)
{
    char path[4096];
    (void)snprintf(path,sizeof(path),"%s/brightness",node);fixture_file(path,"0\n");
    if (n76) { (void)snprintf(path,sizeof(path),"%s/rbrightness",node);fixture_file(path,"180\n"); }
    (void)snprintf(path,sizeof(path),"%s/color",node);fixture_file(path,n76 ? "orange\n" : "2\n");
    if (!n76) {
        (void)snprintf(path,sizeof(path),"%s/state",node);
        fixture_file(path,"op_mode:on,color:2,t-hight:0, t-low:0,mode:0x1,tri_t:0\n");
    }
    (void)snprintf(path,sizeof(path),"%s/trigger",node);fixture_file(path,"[none] normal timer breath\n");
}

static void test_sysfs_late_guards(struct ugreenctl_led_session *session,struct fixture *fixture,
    const char *node,uint8_t id,void (*driver)(void *,const char *,const char *),bool n76)
{
    struct ugreenctl_led_patch patch={.fields=UGREENCTL_LED_PATCH_COLOR|UGREENCTL_LED_PATCH_MODE,
        .value={.mode=UGREENCTL_LED_MODE_ON,.color=UGREENCTL_LED_COLOR_WHITE}};
    struct ugreenctl_led_value value;
    struct sysfs_guard_observer guard={.fixture=fixture,.driver=driver,.claim_owner=true};
    volatile sig_atomic_t cancelled=1;
    char error[256],path[4096];
    ugreenctl_led_test_sysfs_observer(session,observe_sysfs_guard,&guard);
    if (n76) {
        guard.claim_owner=false;
        (void)snprintf(path,sizeof(path),"%s/trigger",node);
        fixture_file(path,"[heartbeat] none normal timer\n");
        patch.fields=UGREENCTL_LED_PATCH_COLOR;
        CHECK(ugreenctl_led_apply(session,id,&patch,&value,error,sizeof(error))==-ENOTSUP);
        CHECK(guard.writes==0);
        patch.fields|=UGREENCTL_LED_PATCH_MODE;
        CHECK(!ugreenctl_led_apply(session,id,&patch,&value,error,sizeof(error)));
        CHECK(guard.writes>0 && value.mode==UGREENCTL_LED_MODE_ON);
        guard=(struct sysfs_guard_observer){.fixture=fixture,.driver=driver,.claim_owner=true};
    }
    CHECK(ugreenctl_led_apply(session,id,&patch,&value,error,sizeof(error))==-EBUSY);
    CHECK(guard.writes==1); /* A new owner after color blocks every remaining attribute. */
    fixture->owner_error=0;
    reset_sysfs_guard_state(node,n76);
    guard=(struct sysfs_guard_observer){.fixture=fixture,.driver=driver,.owner_during_restore=true};
    CHECK(ugreenctl_led_identify(session,id,1,&cancelled,error,sizeof(error))==-EBUSY);
    CHECK(guard.none_writes==2 && strstr(error,"identify restoration failed"));
    fixture->owner_error=0;
    CHECK(!ugreenctl_led_read(session,id,&value,error,sizeof(error)) && value.color==UGREENCTL_LED_COLOR_WHITE);
    reset_sysfs_guard_state(node,n76);
    guard=(struct sysfs_guard_observer){.fixture=fixture,.driver=driver,.unknown_restoration=true};
    CHECK(ugreenctl_led_identify(session,id,1,&cancelled,error,sizeof(error))==-EIO);
    CHECK(guard.none_writes==3 && strstr(error,"could not verify the saved fields"));
    CHECK(!ugreenctl_led_read(session,id,&value,error,sizeof(error)));
    CHECK(value.mode==UGREENCTL_LED_MODE_OFF && (value.unknown_fields&UGREENCTL_LED_PATCH_MODE));
    ugreenctl_led_test_sysfs_observer(session,driver,NULL);
    reset_sysfs_guard_state(node,n76);
}

static void n76_available_triggers(struct ugreenctl_led_session *session,const char *root,const char *list)
{
    const struct ugreenctl_led_profile *profile=ugreenctl_led_identity(session)->profile;
    char path[4096];
    /* Registered triggers are global for all of this stock driver's cdevs. */
    for (size_t i=0;i<profile->led_count;++i) {
        (void)snprintf(path,sizeof(path),"%s/class/leds/%s/trigger",root,profile->leds[i].sysfs_name);
        fixture_file(path,list);
    }
}

static void test_n76_missing_triggers(struct ugreenctl_led_session *session,const char *root)
{
    struct sysfs_guard_observer guard={.driver=n76_driver_observer};
    struct ugreenctl_led_patch patch={.fields=UGREENCTL_LED_PATCH_COLOR|UGREENCTL_LED_PATCH_MODE,
        .value={.mode=UGREENCTL_LED_MODE_ON,.color=UGREENCTL_LED_COLOR_WHITE}};
    struct ugreenctl_led_value value;
    char error[256];
    ugreenctl_led_test_sysfs_observer(session,observe_sysfs_guard,&guard);
    n76_available_triggers(session,root,"[none] timer\n");
    CHECK(ugreenctl_led_apply(session,0,&patch,&value,error,sizeof(error))==-ENOTSUP);
    CHECK(guard.writes==0 && strstr(error,"'normal'"));
    CHECK(ugreenctl_led_identify(session,0,1,NULL,error,sizeof(error))==-ENOTSUP);
    CHECK(guard.writes==0); /* No effect or recovery writes for a known missing prerequisite. */
    n76_available_triggers(session,root,"[none] normal\n");
    patch.fields|=UGREENCTL_LED_PATCH_TIMING;patch.value.mode=UGREENCTL_LED_MODE_BLINK;
    patch.value.on_ms=200;patch.value.off_ms=200;
    CHECK(ugreenctl_led_apply(session,0,&patch,&value,error,sizeof(error))==-ENOTSUP);
    CHECK(guard.writes==0 && strstr(error,"'timer'"));
    n76_available_triggers(session,root,"[none] abnormal timer2\n");
    CHECK(ugreenctl_led_apply(session,0,&patch,&value,error,sizeof(error))==-ENOTSUP);
    CHECK(guard.writes==0 && strstr(error,"'timer'"));
    patch.fields&=~UGREENCTL_LED_PATCH_TIMING;patch.value.mode=UGREENCTL_LED_MODE_ON;
    CHECK(ugreenctl_led_apply(session,0,&patch,&value,error,sizeof(error))==-ENOTSUP);
    CHECK(guard.writes==0 && strstr(error,"'normal'"));
    CHECK(ugreenctl_led_identify(session,0,1,NULL,error,sizeof(error))==-ENOTSUP);
    CHECK(guard.writes==0);
    n76_available_triggers(session,root,"[none] normal timer breath\n");
    ugreenctl_led_test_sysfs_observer(session,n76_driver_observer,NULL);
}

static void test_sysfs_n76(bool dh2600)
{
    char root[]="/tmp/ugreenctl-led-sysfs-XXXXXX";
    char path[4096],node[4096],driver[4096],module[4096],client[4096],link[4096],error[256];
    struct fixture fixture;
    struct ugreenctl_led_identity identity={0};
    struct ugreenctl_led_access access={true,true};
    struct ugreenctl_led_session *session;
    struct ugreenctl_led_value value;
    struct ugreenctl_led_patch patch=rgb_patch();
    CHECK(mkdtemp(root)!=NULL);
    (void)snprintf(client,sizeof(client),"%s/devices/i2c-7/7-0026",root);make_directories(client);
    (void)snprintf(node,sizeof(node),"%s/leds/power",client);make_directories(node);
    (void)snprintf(driver,sizeof(driver),"%s/bus/i2c/drivers/mcu_led",root);make_directories(driver);
    (void)snprintf(module,sizeof(module),"%s/module/%s",root,dh2600 ? "leds_mcu_n76e003_dh2600" : "leds_mcu_n76e003");make_directories(module);
    (void)snprintf(path,sizeof(path),"%s/class/leds",root);make_directories(path);
    (void)snprintf(link,sizeof(link),"%s/class/leds/power",root);CHECK(symlink(node,link)==0);
    (void)snprintf(path,sizeof(path),"%s/device",node);CHECK(symlink(client,path)==0);
    (void)snprintf(path,sizeof(path),"%s/driver",client);CHECK(symlink(driver,path)==0);
    (void)snprintf(path,sizeof(path),"%s/module",driver);CHECK(symlink(module,path)==0);
    (void)snprintf(path,sizeof(path),"%s/max_brightness",node);fixture_file(path,"254\n");
    (void)snprintf(path,sizeof(path),"%s/brightness",node);fixture_file(path,"180\n");
    (void)snprintf(path,sizeof(path),"%s/rbrightness",node);fixture_file(path,"180\n");
    (void)snprintf(path,sizeof(path),"%s/color",node);fixture_file(path,"white\n");
    (void)snprintf(path,sizeof(path),"%s/trigger",node);fixture_file(path,"[none] timer breath normal\n");
    (void)snprintf(path,sizeof(path),"%s/delay_on",node);fixture_file(path,"100\n");
    (void)snprintf(path,sizeof(path),"%s/delay_off",node);fixture_file(path,"200\n");
    if (dh2600) {
        unsigned int disk;
        for (disk=1;disk<=2;++disk) {
            char disk_node[4096];
            (void)snprintf(disk_node,sizeof(disk_node),"%s/leds/disk%u",client,disk);make_directories(disk_node);
            (void)snprintf(path,sizeof(path),"%s/class/leds/disk%u",root,disk);CHECK(!symlink(disk_node,path));
            (void)snprintf(path,sizeof(path),"%s/device",disk_node);CHECK(!symlink(client,path));
            (void)snprintf(path,sizeof(path),"%s/max_brightness",disk_node);fixture_file(path,"254\n");
            (void)snprintf(path,sizeof(path),"%s/brightness",disk_node);fixture_file(path,"0\n");
            (void)snprintf(path,sizeof(path),"%s/rbrightness",disk_node);fixture_file(path,"180\n");
            (void)snprintf(path,sizeof(path),"%s/color",disk_node);fixture_file(path,"white\n");
            (void)snprintf(path,sizeof(path),"%s/trigger",disk_node);fixture_file(path,"[none] timer normal\n");
        }
    }
    initialize(&fixture);(void)snprintf(identity.product_name,sizeof(identity.product_name),"%s",dh2600 ? "DH2600" : "DXP480T Plus");
    CHECK(ugreenctl_led_test_open_sysfs(&identity,&access,&io,&fixture,root,&session,error,sizeof(error))==0);
    ugreenctl_led_test_sysfs_observer(session,n76_driver_observer,NULL);
    CHECK(ugreenctl_led_identity_confirmed(session));CHECK(ugreenctl_led_chip_id(session)==0);
    CHECK(fixture.chip_reads==0 && fixture.reads==0 && fixture.writes==0);
    CHECK(ugreenctl_led_read(session,0,&value,error,sizeof(error))==0);
    CHECK(value.mode==UGREENCTL_LED_MODE_ON && value.color==UGREENCTL_LED_COLOR_WHITE);
    CHECK((value.unknown_fields&UGREENCTL_LED_PATCH_RGB)!=0);
    CHECK((value.cached_fields&UGREENCTL_LED_PATCH_COLOR)!=0);
    if (dh2600) {
        CHECK(!ugreenctl_led_read(session,1,&value,error,sizeof(error)) && value.mode==UGREENCTL_LED_MODE_OFF);
        CHECK(!ugreenctl_led_read(session,2,&value,error,sizeof(error)) && value.brightness==0);
        CHECK(!ugreenctl_led_identify(session,2,1,NULL,error,sizeof(error)));
        CHECK(!ugreenctl_led_read(session,2,&value,error,sizeof(error)) && value.mode==UGREENCTL_LED_MODE_OFF && value.brightness==0);
        patch.fields=UGREENCTL_LED_PATCH_BRIGHTNESS;patch.value.brightness=2;
        CHECK(ugreenctl_led_apply(session,2,&patch,&value,error,sizeof(error))==-ENOTSUP);
        patch=rgb_patch();
    }
    CHECK(ugreenctl_led_apply(session,0,&patch,&value,error,sizeof(error))==-ENOTSUP);
    patch.fields=UGREENCTL_LED_PATCH_COLOR;patch.value.color=UGREENCTL_LED_COLOR_ORANGE;
    CHECK(ugreenctl_led_apply(session,0,&patch,&value,error,sizeof(error))==0);
    CHECK(value.color==UGREENCTL_LED_COLOR_ORANGE && (value.cached_fields&UGREENCTL_LED_PATCH_COLOR));
    patch.fields=UGREENCTL_LED_PATCH_MODE;patch.value.mode=UGREENCTL_LED_MODE_OFF;
    CHECK(ugreenctl_led_apply(session,0,&patch,&value,error,sizeof(error))==0);
    CHECK(value.mode==UGREENCTL_LED_MODE_OFF && value.brightness==(dh2600 ? 0U : 180U));
    patch.value.mode=UGREENCTL_LED_MODE_BLINK;patch.fields|=UGREENCTL_LED_PATCH_TIMING;
    patch.value.on_ms=400;patch.value.off_ms=600;
    CHECK(ugreenctl_led_apply(session,0,&patch,&value,error,sizeof(error))==0);
    CHECK(value.mode==UGREENCTL_LED_MODE_BLINK && value.on_ms==400 && value.off_ms==600);
    CHECK(ugreenctl_led_identify(session,0,1,NULL,error,sizeof(error))==0);
    CHECK(ugreenctl_led_read(session,0,&value,error,sizeof(error))==0);
    CHECK(value.mode==UGREENCTL_LED_MODE_BLINK && value.color==UGREENCTL_LED_COLOR_ORANGE && value.on_ms==400 && value.off_ms==600);
    patch.value.mode=UGREENCTL_LED_MODE_BREATH;
    CHECK(ugreenctl_led_apply(session,0,&patch,&value,error,sizeof(error))==-ENOTSUP);
    patch.value.mode=UGREENCTL_LED_MODE_BLINK;patch.value.on_ms=1;
    CHECK(ugreenctl_led_apply(session,0,&patch,&value,error,sizeof(error))==-ENOTSUP);patch.value.on_ms=400;
    patch.fields=UGREENCTL_LED_PATCH_BRIGHTNESS;patch.value.brightness=1;
    CHECK(ugreenctl_led_apply(session,0,&patch,&value,error,sizeof(error))==-ENOTSUP);
    test_n76_missing_triggers(session,root);
    test_sysfs_late_guards(session,&fixture,node,0,n76_driver_observer,true);
    patch.fields=UGREENCTL_LED_PATCH_MODE;patch.value.mode=UGREENCTL_LED_MODE_ON;
    /* An owner or changed binding still wins over --force. */
    fixture.owner_error=-EBUSY;
    CHECK(ugreenctl_led_apply(session,0,&patch,&value,error,sizeof(error))==-EBUSY);fixture.owner_error=0;
    (void)snprintf(path,sizeof(path),"%s/module",driver);CHECK(unlink(path)==0);
    CHECK(!ugreenctl_led_identity_confirmed(session));
    CHECK(ugreenctl_led_apply(session,0,&patch,&value,error,sizeof(error))==-ENOTSUP);
    ugreenctl_led_close(session);CHECK(fixture.unlocks==1);
    initialize(&fixture);
    CHECK(ugreenctl_led_test_open_sysfs(&identity,&access,&io,&fixture,root,&session,error,sizeof(error))==-ENOTSUP);
    CHECK(fixture.chip_reads==0 && fixture.writes==0 && fixture.unlocks==1);
    CHECK(strncmp(root,"/tmp/ugreenctl-led-sysfs-",24)==0);
    CHECK(nftw(root,remove_fixture,16,FTW_DEPTH|FTW_PHYS)==0);
}

static void gpio_driver_observer(void *opaque,const char *path,const char *text)
{
    char file[4096],state[256],*slash;
    (void)opaque;
    if (strlen(path)<6 || strcmp(path+strlen(path)-6,"/color")) return;
    (void)snprintf(file,sizeof(file),"%s",path);slash=strrchr(file,'/');CHECK(slash);strcpy(slash+1,"state");
    (void)snprintf(state,sizeof(state),"op_mode:on,color:%u,t-hight:0, t-low:0,mode:0x1,tri_t:0\n",(unsigned int)strtoul(text,NULL,10));
    fixture_file(file,state);
}

static void test_sysfs_gpio(bool four)
{
    char root[]="/tmp/ugreenctl-led-sysfs-XXXXXX";
    char path[4096],node[4096],platform[4096],driver[4096],module[4096],error[256],link[4096];
    const char *const names[]={"power","network_stat","disk1","disk2","disk3","disk4"};
    struct fixture fixture;struct ugreenctl_led_session *session;
    struct ugreenctl_led_identity identity={0};struct ugreenctl_led_access access={true,true};
    struct ugreenctl_led_patch patch={0};struct ugreenctl_led_value value;unsigned int i;
    CHECK(mkdtemp(root));
    (void)snprintf(platform,sizeof(platform),"%s/devices/platform/ug-led",root);make_directories(platform);
    (void)snprintf(driver,sizeof(driver),"%s/bus/platform/drivers/ug-gpio-leds",root);make_directories(driver);
    (void)snprintf(module,sizeof(module),"%s/module/ug_gpio_leds",root);make_directories(module);
    (void)snprintf(path,sizeof(path),"%s/module",driver);CHECK(!symlink(module,path));
    (void)snprintf(path,sizeof(path),"%s/driver",platform);CHECK(!symlink(driver,path));
    (void)snprintf(path,sizeof(path),"%s/class/leds",root);make_directories(path);
    for (i=0;i<(four ? 6U : 4U);++i) {
        (void)snprintf(node,sizeof(node),"%s/leds/%s",platform,names[i]);make_directories(node);
        (void)snprintf(link,sizeof(link),"%s/class/leds/%s",root,names[i]);CHECK(!symlink(node,link));
        (void)snprintf(path,sizeof(path),"%s/device",node);CHECK(!symlink(platform,path));
        (void)snprintf(path,sizeof(path),"%s/max_brightness",node);fixture_file(path,"255\n");
        (void)snprintf(path,sizeof(path),"%s/brightness",node);fixture_file(path,"255\n");
        (void)snprintf(path,sizeof(path),"%s/color",node);fixture_file(path,"2\n");
        (void)snprintf(path,sizeof(path),"%s/state",node);fixture_file(path,"op_mode:on,color:2,t-hight:0, t-low:0,mode:0x1,tri_t:0\n");
        (void)snprintf(path,sizeof(path),"%s/trigger",node);fixture_file(path,"[none] normal timer2 breath\n");
    }
    initialize(&fixture);(void)snprintf(identity.product_name,sizeof(identity.product_name),"%s",four ? "DH4300 Plus" : "DH2300");
    CHECK(!ugreenctl_led_test_open_sysfs(&identity,&access,&io,&fixture,root,&session,error,sizeof(error)));
    ugreenctl_led_test_sysfs_observer(session,gpio_driver_observer,NULL);
    CHECK(ugreenctl_led_identity_confirmed(session));
    CHECK(!ugreenctl_led_read(session,2,&value,error,sizeof(error)) && value.brightness==1);
    CHECK(value.color==(four ? UGREENCTL_LED_COLOR_RED : UGREENCTL_LED_COLOR_ORANGE));
    patch.fields=UGREENCTL_LED_PATCH_BRIGHTNESS;patch.value.brightness=2;
    CHECK(ugreenctl_led_apply(session,2,&patch,&value,error,sizeof(error))==-ENOTSUP);
    patch.fields=UGREENCTL_LED_PATCH_COLOR;patch.value.color=UGREENCTL_LED_COLOR_BOTH;
    CHECK(ugreenctl_led_apply(session,2,&patch,&value,error,sizeof(error))==-ENOTSUP);
    patch.value.color=UGREENCTL_LED_COLOR_CHANNEL1;
    CHECK(!ugreenctl_led_apply(session,2,&patch,&value,error,sizeof(error)) && value.color==UGREENCTL_LED_COLOR_WHITE);
    patch.value.color=UGREENCTL_LED_COLOR_CHANNEL2;
    CHECK(!ugreenctl_led_apply(session,2,&patch,&value,error,sizeof(error)));
    CHECK(!ugreenctl_led_identify(session,2,1,NULL,error,sizeof(error)));
    CHECK(!ugreenctl_led_read(session,2,&value,error,sizeof(error)) && value.brightness==1);
    CHECK(value.color==(four ? UGREENCTL_LED_COLOR_RED : UGREENCTL_LED_COLOR_ORANGE));
    /* The physical API is binary while the original kernel cache is 255. */
    (void)snprintf(path,sizeof(path),"%s/leds/disk1/brightness",platform);
    { char original[32];FILE *file=fopen(path,"r");CHECK(file);CHECK(fgets(original,sizeof(original),file));CHECK(!fclose(file));CHECK(!strcmp(original,"255")); }
    (void)snprintf(node,sizeof(node),"%s/leds/disk1",platform);
    test_sysfs_late_guards(session,&fixture,node,2,gpio_driver_observer,false);
    ugreenctl_led_close(session);
    (void)snprintf(path,sizeof(path),"%s/module",driver);CHECK(!unlink(path));
    /* The documented built-in platform binding is also valid. */
    CHECK(!ugreenctl_led_test_open_sysfs(&identity,&access,&io,&fixture,root,&session,error,sizeof(error)));
    ugreenctl_led_close(session);
    (void)snprintf(path,sizeof(path),"%s/driver",platform);CHECK(!unlink(path));
    CHECK(ugreenctl_led_test_open_sysfs(&identity,&access,&io,&fixture,root,&session,error,sizeof(error))==-ENOTSUP);
    CHECK(!nftw(root,remove_fixture,16,FTW_DEPTH|FTW_PHYS));
}

static void test_machine_version_guard(void)
{
    struct fixture fixture;
    struct ugreenctl_led_test_io checked_io=io;
    struct ugreenctl_led_identity baseline={0};
    struct ugreenctl_led_session *session;
    const struct ugreenctl_led_access access={true,true};
    struct ugreenctl_led_patch patch={UGREENCTL_LED_PATCH_BRIGHTNESS,{.brightness=90}};
    struct ugreenctl_led_value value;
    char error[256];size_t i;
    initialize(&fixture);
    strcpy(baseline.product_name,"DXP4800 Plus");strcpy(baseline.source,"dmi");
    strcpy(baseline.bios_vendor,"AMI");strcpy(baseline.bios_version,"fixture-old");
    strcpy(baseline.bios_date,"01/02/2025");strcpy(baseline.board_version,"board-A");
    baseline.profile=ugreenctl_led_profile_find(baseline.product_name,baseline.product_version);
    fixture.machine=baseline;checked_io.read_identity=read_machine;
    CHECK(!ugreenctl_led_test_open(&baseline,&access,&checked_io,&fixture,&session,error,sizeof(error)));
    for (i=0;i<5;++i) {
        int reads=fixture.reads;
        fixture.machine=baseline;
        if (i==0) strcpy(fixture.machine.bios_version,"fixture-new");
        if (i==1) strcpy(fixture.machine.bios_vendor,"different vendor");
        if (i==2) strcpy(fixture.machine.bios_date,"02/03/2026");
        if (i==3) strcpy(fixture.machine.board_version,"board-B");
        if (i==4) fixture.machine.bios_version[0]=0;
        CHECK(ugreenctl_led_apply(session,0,&patch,&value,error,sizeof(error))==-ESTALE);
        CHECK(ugreenctl_led_read(session,0,&value,error,sizeof(error))==-ESTALE);
        CHECK(ugreenctl_led_identify(session,0,1,NULL,error,sizeof(error))==-ESTALE);
        CHECK(!fixture.writes && fixture.reads==reads);
    }
    fixture.machine=baseline;fixture.identity_error=-EIO;
    CHECK(ugreenctl_led_apply(session,0,&patch,&value,error,sizeof(error))==-EIO && !fixture.writes);
    fixture.identity_error=0;
    CHECK(!ugreenctl_led_apply(session,0,&patch,&value,error,sizeof(error)) && fixture.writes==1);
    ugreenctl_led_close(session);
}

void ugreenctl_led_gpio_fixture_tests(void);

int main(void)
{
    test_process_identity();
    test_wire_protocol();
    test_profile_selection();
    test_smbus_and_capabilities();
    test_no_unsafe_access();
    test_validation_and_readback();
    test_identify_restores();
    test_machine_version_guard();
    test_sysfs_n76(false);test_sysfs_n76(true);
    test_sysfs_gpio(false);test_sysfs_gpio(true);
    ugreenctl_led_gpio_fixture_tests();
    puts("LED protocol, identity, ownership, readback and restoration tests passed");
    return 0;
}
