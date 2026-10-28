#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "core/identity.h"
#include "power/rtc_wake.h"
#include "network/wol_interface.h"
static int fake_identity(struct ugreenctl_machine_identity *,char *,size_t);
static int fake_rtc_read(const struct ugreenctl_rtc_profile *,time_t *,char *,size_t);
#define ugreenctl_identity_read fake_identity
#define ugreenctl_rtc_wake_read fake_rtc_read
#undef _DEFAULT_SOURCE
#include "core/wake_cli.c"
#undef ugreenctl_identity_read
#undef ugreenctl_rtc_wake_read
static const char *product="DXP2800 GT";
static unsigned reads;
static int fake_identity(struct ugreenctl_machine_identity *i,char *e,size_t n)
{
    (void)e;(void)n; memset(i,0,sizeof(*i)); strcpy(i->source,"dmi"); strcpy(i->product_name,product); return 0;
}
static int fake_rtc_read(const struct ugreenctl_rtc_profile *p,time_t *epoch,char *e,size_t n)
{
    (void)e;(void)n;assert(!strcmp(p->product,product));++reads;*epoch=0;return 0;
}
static int run(int n,const char **args) { return ugreenctl_wake_cli(n,(char **)args); }
int main(void)
{
    const char *read[]={"ugreenctl","power","rtc-wake","get"};
    const char *dry[]={"ugreenctl","power","rtc-wake","clear"};
    const char *override[]={"ugreenctl","--model","dx4600","--force","--apply","power","rtc-wake","clear"};
    const char *force[]={"ugreenctl","--apply","power","rtc-wake","clear"};
    const char *wol[]={"ugreenctl","network","wol","set","on"};
    const char *explicit[]={"ugreenctl","network","wol","set","on","--interface","enp8s0"};
    const char *fan[]={"ugreenctl","fan","status"};
    const char *rtc_nic[]={"ugreenctl","power","rtc-wake","get","--interface","eth0"};
    assert(!run(4,read) && reads==1); /* GT has no loadable hardware plugin or WOL auto map. */
    assert(!run(4,dry) && reads==1); /* dry-run does no device access */
    assert(run(8,override)==1); assert(run(5,force)==1);
    assert(run(5,wol)==1); /* never guess automatic NIC count */
    assert(!run(7,explicit));
    assert(run(3,fan)==-1); assert(run(6,rtc_nic)==1);
    product="Unknown NAS";
    assert(run(4,read)==1); assert(run(4,dry)==1);
    puts("Independent RTC routing without WOL/plugin; exact identity, dry-run and argument gates passed");
    return 0;
}
