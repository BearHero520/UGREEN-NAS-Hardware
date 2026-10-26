#define _XOPEN_SOURCE 700
#include "led/led_gpio.h"
#include <errno.h>
#include <ftw.h>
#include <linux/gpio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c);abort(); } } while (0)
struct gpio_fixture {
    int owner_error,lock_error,locks,unlocks,closes,opens,requests,writes;
    bool wrong_chip,busy,wrong_readback;
    uint64_t values[2];
    unsigned int offsets[2][12],counts[2];
};
static void directories(const char *path)
{
    char copy[4096],*at;(void)snprintf(copy,sizeof(copy),"%s",path);
    for (at=copy+1;*at;++at) if (*at=='/') { *at=0;CHECK(!mkdir(copy,0700)||errno==EEXIST);*at='/'; }
    CHECK(!mkdir(copy,0700)||errno==EEXIST);
}
static void binary_file(const char *path,const void *data,size_t length)
{
    FILE *file=fopen(path,"wb");CHECK(file);CHECK(fwrite(data,1,length,file)==length);CHECK(!fclose(file));
}
static void string_file(const char *path,const char *text) { binary_file(path,text,strlen(text)+1); }
static void u32_file(const char *path,unsigned int value)
{
    unsigned char data[4]={(unsigned char)(value>>24),(unsigned char)(value>>16),(unsigned char)(value>>8),(unsigned char)value};
    binary_file(path,data,4);
}
static void create_tree(const char *root,bool four)
{
    const char *nodes[2]={four ? "/pinctrl/gpio@fd8a0000" : "/pinctrl/gpio@2ae20000","/pinctrl/gpio@fec50000"};
    const unsigned int pins2[12]={30,31,28,29,15,13,14,12,11,12,7,8};
    const unsigned int pins4[12]={22,4,8,20,13,14,9,10,11,12,7,8};
    unsigned char routing[144];unsigned int i,banks=four ? 2 : 1;char path[4096],directory[4096],node[4096],link[4096];
    (void)snprintf(directory,sizeof(directory),"%s/firmware/devicetree/base/ug-led",root);directories(directory);
    (void)snprintf(path,sizeof(path),"%s/firmware/devicetree/base/model",root);
    string_file(path,four ? "UGREEN RK3588C DH4300Plus Board" : "UGREEN RK3576 DH2300 Board");
    (void)snprintf(path,sizeof(path),"%s/firmware/devicetree/base/compatible",root);
    string_file(path,four ? "rockchip,rk3588" : "rockchip,rk3576");
    (void)snprintf(path,sizeof(path),"%s/compatible",directory);string_file(path,"ug-rockchip-led");
    (void)snprintf(path,sizeof(path),"%s/status",directory);string_file(path,"okay");
    (void)snprintf(path,sizeof(path),"%s/led_num",directory);u32_file(path,four ? 6 : 4);
    for (i=0;i<12;++i) {
        unsigned int phandle=four && i>=4 ? 2 : 1,pin=four ? pins4[i] : pins2[i];
        memset(routing+i*12,0,12);routing[i*12+3]=(unsigned char)phandle;routing[i*12+7]=(unsigned char)pin;
    }
    (void)snprintf(path,sizeof(path),"%s/leds-gpios",directory);binary_file(path,routing,sizeof(routing));
    (void)snprintf(path,sizeof(path),"%s/bus/gpio/devices",root);directories(path);
    (void)snprintf(path,sizeof(path),"%s/dev",root);directories(path);
    for (i=0;i<banks;++i) {
        (void)snprintf(node,sizeof(node),"%s/firmware/devicetree/base%s",root,nodes[i]);directories(node);
        (void)snprintf(path,sizeof(path),"%s/compatible",node);string_file(path,"rockchip,gpio-bank");
        (void)snprintf(path,sizeof(path),"%s/phandle",node);u32_file(path,i+1);
        (void)snprintf(directory,sizeof(directory),"%s/devices/platform/bank%u/gpiochip%u",root,i,i+12);directories(directory);
        (void)snprintf(path,sizeof(path),"%s/of_node",directory);CHECK(!symlink(node,path));
        (void)snprintf(link,sizeof(link),"%s/bus/gpio/devices/gpiochip%u",root,i+12);CHECK(!symlink(directory,link));
    }
}
static int gpio_open(void *opaque,const char *path)
{
    struct gpio_fixture *fixture=opaque;const char *name=strrchr(path,'/');unsigned int number;
    CHECK(name && sscanf(name+1,"gpiochip%u",&number)==1 && number>=12 && number<=13);++fixture->opens;return (int)number;
}
static int gpio_control(void *opaque,int fd,unsigned long command,void *argument)
{
    struct gpio_fixture *fixture=opaque;unsigned int bank=(unsigned int)(fd>=100 ? fd-100 : fd)-12;
    CHECK(bank<2);
    if (command==GPIO_GET_CHIPINFO_IOCTL) {
        struct gpiochip_info *info=argument;(void)snprintf(info->name,sizeof(info->name),"gpiochip%u",bank+12);
        info->lines=fixture->wrong_chip ? 64 : 32;return 0;
    }
    if (command==GPIO_V2_GET_LINEINFO_IOCTL) {
        struct gpio_v2_line_info *info=argument;info->flags=fixture->busy ? GPIO_V2_LINE_FLAG_USED : GPIO_V2_LINE_FLAG_OUTPUT;return 0;
    }
    if (command==GPIO_V2_GET_LINE_IOCTL) {
        struct gpio_v2_line_request *request=argument;
        CHECK(request->config.flags==0 && request->config.num_attrs==0);CHECK(request->num_lines<=12);
        memcpy(fixture->offsets[bank],request->offsets,request->num_lines*sizeof(request->offsets[0]));
        fixture->counts[bank]=request->num_lines;request->fd=fd+100;++fixture->requests;return 0;
    }
    if (command==GPIO_V2_LINE_GET_VALUES_IOCTL) {
        struct gpio_v2_line_values *values=argument;values->bits=fixture->values[bank]&values->mask;return 0;
    }
    if (command==GPIO_V2_LINE_SET_CONFIG_IOCTL) {
        struct gpio_v2_line_config *config=argument;uint64_t mask=config->attrs[1].mask;
        CHECK(config->flags==0 && config->num_attrs==2);
        CHECK(config->attrs[0].attr.id==GPIO_V2_LINE_ATTR_ID_FLAGS && config->attrs[0].attr.flags==GPIO_V2_LINE_FLAG_OUTPUT);
        CHECK(config->attrs[1].attr.id==GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES && config->attrs[0].mask==mask);
        if (!fixture->wrong_readback) fixture->values[bank]=(fixture->values[bank]&~mask)|(config->attrs[1].attr.values&mask);
        ++fixture->writes;return 0;
    }
    CHECK(false);return -EINVAL;
}
static void gpio_close(void *opaque,int fd) { struct gpio_fixture *fixture=opaque;(void)fd;++fixture->closes; }
static int gpio_owners(void *opaque) { return ((struct gpio_fixture *)opaque)->owner_error; }
static int gpio_lock(void *opaque) { struct gpio_fixture *fixture=opaque;++fixture->locks;return fixture->lock_error; }
static void gpio_unlock(void *opaque) { ++((struct gpio_fixture *)opaque)->unlocks; }
static const struct ugreenctl_led_gpio_test_io gpio_io={gpio_open,gpio_control,gpio_close,gpio_owners,gpio_lock,gpio_unlock};
static int remove_tree(const char *path,const struct stat *metadata,int type,struct FTW *walk)
{
    (void)metadata;(void)type;(void)walk;return remove(path);
}

static void run_model(bool four)
{
    char root[]="/tmp/ugreenctl-gpio-test-XXXXXX",device[4096],path[4096],error[256];
    struct gpio_fixture fixture={0};struct ugreenctl_led_gpio *context;
    struct ugreenctl_led_access access={true,true};struct ugreenctl_led_value value;
    struct ugreenctl_led_patch patch={0};unsigned int bank=four ? 1 : 0,id=four ? 5 : 3;
    CHECK(mkdtemp(root));create_tree(root,four);(void)snprintf(device,sizeof(device),"%s/dev",root);
    fixture.values[0]=1;
    CHECK(!ugreenctl_led_gpio_test_open(root,device,&access,&gpio_io,&fixture,&context,error,sizeof(error)));
    CHECK(fixture.requests==(four ? 2 : 1) && fixture.writes==0);
    CHECK(fixture.counts[0]==(four ? 4 : 8));CHECK(fixture.offsets[0][0]==(four ? 22 : 30));
    CHECK(ugreenctl_led_gpio_identity_confirmed(context));
    CHECK(!ugreenctl_led_gpio_read(context,0,&value,error,sizeof(error)) && value.color==UGREENCTL_LED_COLOR_WHITE);
    CHECK(value.cached_fields==0 && value.brightness==1);
    CHECK(!ugreenctl_led_gpio_read(context,(uint8_t)id,&value,error,sizeof(error)) && value.mode==UGREENCTL_LED_MODE_OFF);
    CHECK(value.unknown_fields&UGREENCTL_LED_PATCH_COLOR);
    patch.fields=UGREENCTL_LED_PATCH_COLOR;patch.value.color=UGREENCTL_LED_COLOR_CHANNEL2;
    CHECK(ugreenctl_led_gpio_apply(context,(uint8_t)id,&patch,&value,error,sizeof(error))==-ENOTSUP && fixture.writes==0);
    patch.fields|=UGREENCTL_LED_PATCH_MODE;patch.value.mode=UGREENCTL_LED_MODE_ON;
    CHECK(!ugreenctl_led_gpio_apply(context,(uint8_t)id,&patch,&value,error,sizeof(error)));
    CHECK(value.color==(four ? UGREENCTL_LED_COLOR_RED : UGREENCTL_LED_COLOR_ORANGE));
    CHECK(fixture.values[bank]&(1ULL<<(four ? 7 : 7)));
    CHECK(fixture.values[0]&1); /* unrelated power pin preserved */
    patch.fields=UGREENCTL_LED_PATCH_MODE;patch.value.mode=UGREENCTL_LED_MODE_OFF;
    CHECK(!ugreenctl_led_gpio_apply(context,(uint8_t)id,&patch,&value,error,sizeof(error)) && value.brightness==0);
    fixture.wrong_readback=true;patch.value.mode=UGREENCTL_LED_MODE_ON;
    CHECK(ugreenctl_led_gpio_apply(context,(uint8_t)id,&patch,&value,error,sizeof(error))==-EIO);fixture.wrong_readback=false;
    patch.fields=UGREENCTL_LED_PATCH_RGB;CHECK(ugreenctl_led_gpio_apply(context,0,&patch,&value,error,sizeof(error))==-ENOTSUP);
    fixture.owner_error=-EBUSY;CHECK(!ugreenctl_led_gpio_identity_confirmed(context));
    CHECK(ugreenctl_led_gpio_read(context,0,&value,error,sizeof(error))==-EBUSY);fixture.owner_error=0;
    (void)snprintf(path,sizeof(path),"%s/firmware/devicetree/base/ug-led/led_num",root);u32_file(path,9);
    CHECK(!ugreenctl_led_gpio_identity_confirmed(context));
    patch.fields=UGREENCTL_LED_PATCH_MODE;CHECK(ugreenctl_led_gpio_apply(context,0,&patch,&value,error,sizeof(error))==-ENOTSUP);
    u32_file(path,four ? 6 : 4);
    ugreenctl_led_gpio_close(context);CHECK(fixture.closes==(four ? 4 : 2) && fixture.unlocks==1);
    memset(&fixture,0,sizeof(fixture));fixture.busy=true;
    CHECK(ugreenctl_led_gpio_test_open(root,device,&access,&gpio_io,&fixture,&context,error,sizeof(error))==-EBUSY);
    CHECK(fixture.writes==0 && fixture.unlocks==1);
    memset(&fixture,0,sizeof(fixture));fixture.wrong_chip=true;
    CHECK(ugreenctl_led_gpio_test_open(root,device,&access,&gpio_io,&fixture,&context,error,sizeof(error))==-ENOTSUP);
    CHECK(fixture.requests==0 && fixture.writes==0);
    memset(&fixture,0,sizeof(fixture));access.apply=false;
    CHECK(!ugreenctl_led_gpio_test_open(root,device,&access,&gpio_io,&fixture,&context,error,sizeof(error)));
    CHECK(ugreenctl_led_gpio_apply(context,0,&patch,&value,error,sizeof(error))==-EPERM && fixture.writes==0);
    ugreenctl_led_gpio_close(context);
    /* A correct SoC/model does not authorize different LED pin routing. */
    {
        unsigned char routing[144];FILE *file;
        (void)snprintf(path,sizeof(path),"%s/firmware/devicetree/base/ug-led/leds-gpios",root);
        file=fopen(path,"rb");CHECK(file && fread(routing,1,sizeof(routing),file)==sizeof(routing));CHECK(!fclose(file));
        routing[7]^=1;binary_file(path,routing,sizeof(routing));memset(&fixture,0,sizeof(fixture));
        CHECK(ugreenctl_led_gpio_test_open(root,device,&access,&gpio_io,&fixture,&context,error,sizeof(error))==-ENOTSUP);
        CHECK(fixture.opens==0 && fixture.requests==0 && fixture.writes==0);
        routing[7]^=1;binary_file(path,routing,sizeof(routing));
    }
    /* Never accept two character-device aliases for the same expected bank. */
    {
        char alias[4096],target[4096];
        (void)snprintf(alias,sizeof(alias),"%s/bus/gpio/devices/gpiochip99",root);
        (void)snprintf(target,sizeof(target),"%s/devices/platform/bank0/gpiochip12",root);CHECK(!symlink(target,alias));
        memset(&fixture,0,sizeof(fixture));
        CHECK(ugreenctl_led_gpio_test_open(root,device,&access,&gpio_io,&fixture,&context,error,sizeof(error))==-ENOTSUP);
        CHECK(fixture.opens==0 && fixture.writes==0);CHECK(!unlink(alias));
    }
    memset(&fixture,0,sizeof(fixture));fixture.lock_error=-EBUSY;
    CHECK(ugreenctl_led_gpio_test_open(root,device,&access,&gpio_io,&fixture,&context,error,sizeof(error))==-EBUSY);
    CHECK(fixture.requests==0 && fixture.opens==0 && fixture.writes==0);
    CHECK(!strncmp(root,"/tmp/ugreenctl-gpio-test-",23));CHECK(!nftw(root,remove_tree,16,FTW_DEPTH|FTW_PHYS));
}
void ugreenctl_led_gpio_fixture_tests(void) { run_model(false);run_model(true); }
