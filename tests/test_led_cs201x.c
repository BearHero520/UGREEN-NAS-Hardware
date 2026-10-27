#include "led/led_cs201x.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c);abort(); } } while (0)
static const struct ugreenctl_led_profile profile={.id="fixture-cs2011",.product_name="DXP2800 GT",.protocol="cs201x-sio"};
struct fixture {
    struct ugreenctl_led_identity identity;
    unsigned char memory[256],index,ldn,active;
    unsigned int chip,base,address,revision,permission_runtime;
    int owner_error,lock_error,read_error,permission_error;
    unsigned int port_reads,port_writes,target_writes,flow_writes,locks,unlocks,grants,revokes;
    unsigned int target_reads,unstable_read,mismatch_after_write;
    bool fail_all_readbacks,owner_after_write,allow_flow,tamper_on_takeover,flow_mismatch_once;
    volatile sig_atomic_t *cancel;
};
/* The production callbacks are linked too, but never used in this fixture. */
int ugreenctl_led_detect(struct ugreenctl_led_identity *identity,char *error,size_t size)
{ (void)identity;(void)error;(void)size;return -ENOTSUP; }
int ugreenctl_led_process_owners(void) { return 0; }
int ugreenctl_led_process_owners_except(pid_t pid) { (void)pid;return 0; }
static unsigned int offset(unsigned int id,unsigned int field)
{
    static const unsigned int registers[]={0,4,8,12,0x45,0x46,0x44};
    return registers[field]+(field>=4 ? id*4 : id);
}
static void reset(struct fixture *f)
{
    unsigned int id;
    memset(f,0,sizeof(*f));f->chip=0x2011;f->revision=0x42;f->base=0xe00;f->active=1;f->ldn=3;
    strcpy(f->identity.product_name,"DXP2800 GT");strcpy(f->identity.product_version,"1.0");
    strcpy(f->identity.source,"dmi");f->identity.profile=&profile;f->memory[0x40]=1;
    for (id=0;id<4;++id) {
        f->memory[offset(id,0)]=1;f->memory[offset(id,1)]=(unsigned char)(120+id);
        f->memory[offset(id,2)]=32;f->memory[offset(id,3)]=22;
        f->memory[offset(id,4)]=(unsigned char)(1+id);f->memory[offset(id,5)]=(unsigned char)(11+id);
        f->memory[offset(id,6)]=(unsigned char)(21+id);
    }
}
static int detect(void *opaque,struct ugreenctl_led_identity *identity,char *error,size_t size)
{ struct fixture *f=opaque;(void)error;(void)size;*identity=f->identity;return 0; }
static int owners(void *opaque,uint16_t base)
{
    struct fixture *f=opaque;(void)base;
    return f->owner_error ? f->owner_error : f->owner_after_write && f->target_writes ? -EBUSY : 0;
}
static int lock(void *opaque) { struct fixture *f=opaque;++f->locks;return f->lock_error; }
static void unlock(void *opaque) { struct fixture *f=opaque;++f->unlocks; }
static int permission(void *opaque,uint16_t port,unsigned int count,bool enable)
{
    struct fixture *f=opaque;CHECK((port==0x2e && count==2) || ((port==f->base || port==f->permission_runtime) && count==13));
    if (enable) { if (count==13) f->permission_runtime=port;++f->grants;return f->permission_error; }
    ++f->revokes;return 0;
}
static int read_port(void *opaque,uint16_t port,uint8_t *value)
{
    struct fixture *f=opaque;++f->port_reads;
    if (port==0x2f) {
        switch(f->index) {
        case 0x20:*value=(unsigned char)(f->chip>>8);break;
        case 0x21:*value=(unsigned char)f->chip;break;
        case 0x22:*value=(unsigned char)(f->revision>>8);break;
        case 0x23:*value=(unsigned char)f->revision;break;
        case 7:*value=f->ldn;break;
        case 0x30:CHECK(f->ldn==10);*value=f->active;break;
        case 0x60:CHECK(f->ldn==10);*value=(unsigned char)(f->base>>8);break;
        case 0x61:CHECK(f->ldn==10);*value=(unsigned char)f->base;break;
        default:CHECK(false);return -EINVAL;
        }
        return 0;
    }
    CHECK(port==f->base+8 && f->address>=0x20005400 && f->address<0x20005500);
    if (f->address!=0x20005440) {
        ++f->target_reads;
        if (f->read_error) return f->read_error;
        if (f->target_reads==f->unstable_read) f->memory[f->address-0x20005400]^=1;
    }
    *value=f->memory[f->address-0x20005400];
    if (f->address==0x20005440 && f->flow_writes && f->flow_mismatch_once) {
        *value^=1;f->flow_mismatch_once=false;
    }
    if (f->address!=0x20005440 && f->target_writes &&
        (f->fail_all_readbacks || f->target_writes==f->mismatch_after_write)) {
        *value^=1;f->mismatch_after_write=0;
    }
    return 0;
}
static int write_port(void *opaque,uint16_t port,uint8_t value)
{
    struct fixture *f=opaque;unsigned int shift,id,field;bool known=false;++f->port_writes;
    if (port==0x2e) { f->index=value;return 0; }
    if (port==0x2f) { CHECK(f->index==7);f->ldn=value;return 0; }
    if (port==f->base+1) { CHECK(value==0);return 0; }
    if (port>=f->base+4 && port<=f->base+7) {
        shift=(port-f->base-4)*8;f->address=(f->address&~(0xffU<<shift))|((unsigned int)value<<shift);return 0;
    }
    CHECK(port==f->base+8);
    if (f->address==0x20005440) {
        CHECK(f->allow_flow && value<=1);++f->flow_writes;f->memory[0x40]=value;
        if (value==1 && f->tamper_on_takeover) f->memory[7]^=1;
        return 0;
    }
    for (id=0;id<4;++id) for (field=0;field<7;++field)
        if (f->address==0x20005400+offset(id,field)) known=true;
    CHECK(known && f->address!=0x20005440);++f->target_writes;f->memory[f->address-0x20005400]=value;return 0;
}
static void pause_ms(void *opaque,unsigned int duration)
{ struct fixture *f=opaque;if (duration==50 && f->cancel) *f->cancel=1; }
static const struct ugreenctl_led_cs201x_test_io io={detect,owners,lock,unlock,permission,read_port,write_port,pause_ms};
static struct ugreenctl_led_cs201x *open_fixture(struct fixture *f)
{
    struct ugreenctl_led_cs201x *c=NULL;char error[512];const struct ugreenctl_led_access access={.force=true,.apply=true};
    int r=ugreenctl_led_cs201x_test_open(&access,&io,f,&c,error,sizeof(error));
    if (r) fprintf(stderr,"fixture open: %d %s\n",r,error);CHECK(!r && c);CHECK(f->ldn==3);return c;
}
static void reject_open(struct fixture *f,int expected)
{
    struct ugreenctl_led_cs201x *c=NULL;char error[512];const struct ugreenctl_led_access access={.force=true,.apply=true};
    CHECK(ugreenctl_led_cs201x_test_open(&access,&io,f,&c,error,sizeof(error))==expected);
    CHECK(!c && !f->target_writes && f->ldn==3);
    CHECK(f->unlocks==(f->locks && !f->lock_error ? 1U : 0U));CHECK(f->revokes==f->grants-(f->permission_error ? 1U : 0U));
}
static void test_identity_and_ownership(void)
{
    struct fixture f;
    reset(&f);strcpy(f.identity.product_name,"DXP4800 GT");reject_open(&f,-ENOTSUP);CHECK(!f.port_writes && !f.grants);
    reset(&f);strcpy(f.identity.source,"devicetree");reject_open(&f,-ENOTSUP);
    reset(&f);f.identity.profile=NULL;reject_open(&f,-ENOTSUP);
    reset(&f);f.owner_error=-EBUSY;reject_open(&f,-EBUSY);CHECK(!f.port_writes && !f.grants);
    reset(&f);f.lock_error=-EBUSY;reject_open(&f,-EBUSY);
    reset(&f);f.permission_error=-EPERM;reject_open(&f,-EPERM);
    reset(&f);f.chip=0x8613;reject_open(&f,-ENODEV);
    reset(&f);f.active=0;reject_open(&f,-ENODEV);
    reset(&f);f.base=0;reject_open(&f,-ENODEV);
    reset(&f);f.base=0x82;reject_open(&f,-ENODEV);
    reset(&f);f.base=0xe01;reject_open(&f,-ENODEV);
    reset(&f);f.base=0xfff8;reject_open(&f,-ENODEV);
    reset(&f);f.memory[0x40]=0;reject_open(&f,-EBUSY);
}
static void test_reads(void)
{
    struct fixture f;struct ugreenctl_led_cs201x *c;struct ugreenctl_led_value value;char error[512];unsigned int id;
    reset(&f);c=open_fixture(&f);
    CHECK(ugreenctl_led_cs201x_identity_confirmed(c) && ugreenctl_led_cs201x_chip_id(c)==0x2011);
    CHECK(strstr(ugreenctl_led_cs201x_bus(c),"0x0e00"));CHECK(ugreenctl_led_cs201x_identity(c)->profile==&profile);
    for (id=0;id<4;++id) {
        CHECK(!ugreenctl_led_cs201x_read(c,(uint8_t)id,&value,error,sizeof(error)));
        CHECK(value.mode==UGREENCTL_LED_MODE_ON && value.brightness==120+id && value.red==1+id && value.green==11+id && value.blue==21+id);
        CHECK(value.on_ms==1100 && value.off_ms==500 && !value.unknown_fields && !value.cached_fields);
    }
    CHECK(ugreenctl_led_cs201x_read(c,4,&value,error,sizeof(error))==-EINVAL);
    f.memory[0]=0;f.memory[4]=1;f.memory[8]=2;f.memory[12]=4;
    CHECK(!ugreenctl_led_cs201x_read(c,0,&value,error,sizeof(error)) && value.brightness==1 && value.unknown_fields==UGREENCTL_LED_PATCH_TIMING);
    CHECK(!f.target_writes);ugreenctl_led_cs201x_close(c);CHECK(f.grants==f.revokes && f.unlocks==1);
}
static void test_patch_and_guards(void)
{
    struct fixture f;struct ugreenctl_led_cs201x *c;struct ugreenctl_led_value value;char error[512];unsigned char before[256];unsigned int id,field,writes;
    struct ugreenctl_led_patch patch={UGREENCTL_LED_PATCH_MODE|UGREENCTL_LED_PATCH_RGB|UGREENCTL_LED_PATCH_BRIGHTNESS|UGREENCTL_LED_PATCH_TIMING,
        {.mode=UGREENCTL_LED_MODE_BLINK,.brightness=240,.red=33,.green=55,.blue=77,.on_ms=200,.off_ms=300}};
    reset(&f);c=open_fixture(&f);memcpy(before,f.memory,sizeof(before));
    CHECK(!ugreenctl_led_cs201x_apply(c,2,&patch,&value,error,sizeof(error)));
    CHECK(value.mode==UGREENCTL_LED_MODE_BLINK && value.brightness==240 && value.red==33 && value.green==55 && value.blue==77);
    CHECK(value.on_ms==200 && value.off_ms==300 && f.memory[10]==10 && f.memory[14]==4);
    for (id=0;id<4;++id) if (id!=2) for (field=0;field<7;++field) CHECK(f.memory[offset(id,field)]==before[offset(id,field)]);
    CHECK(f.memory[0x40]==1 && f.ldn==3);writes=f.target_writes;
    patch.value.on_ms=201;CHECK(ugreenctl_led_cs201x_apply(c,2,&patch,NULL,error,sizeof(error))==-EINVAL);CHECK(f.target_writes==writes);
    patch.value.on_ms=12500;patch.value.off_ms=300;CHECK(ugreenctl_led_cs201x_apply(c,2,&patch,NULL,error,sizeof(error))==-EINVAL);
    patch.value.on_ms=200;patch.value.mode=UGREENCTL_LED_MODE_BREATH;CHECK(ugreenctl_led_cs201x_apply(c,2,&patch,NULL,error,sizeof(error))==-EINVAL);
    patch.value.mode=UGREENCTL_LED_MODE_BLINK;patch.value.red=256;CHECK(ugreenctl_led_cs201x_apply(c,2,&patch,NULL,error,sizeof(error))==-EINVAL);
    patch.value.red=0;patch.fields=UGREENCTL_LED_PATCH_COLOR;CHECK(ugreenctl_led_cs201x_apply(c,2,&patch,NULL,error,sizeof(error))==-EINVAL);
    patch.fields=UGREENCTL_LED_PATCH_BRIGHTNESS;f.owner_error=-EBUSY;CHECK(ugreenctl_led_cs201x_apply(c,2,&patch,NULL,error,sizeof(error))==-EBUSY);
    f.owner_error=0;f.chip=0x2012;CHECK(ugreenctl_led_cs201x_apply(c,2,&patch,NULL,error,sizeof(error))==-ENODEV);
    f.chip=0x2011;f.memory[0x40]=2;CHECK(ugreenctl_led_cs201x_apply(c,2,&patch,NULL,error,sizeof(error))==-EBADMSG);
    f.memory[0x40]=1;strcpy(f.identity.product_version,"2.0");CHECK(ugreenctl_led_cs201x_apply(c,2,&patch,NULL,error,sizeof(error))==-ENOTSUP);
    strcpy(f.identity.product_version,"1.0");
    {
        char *fields[]={f.identity.bios_vendor,f.identity.bios_version,f.identity.bios_date,f.identity.board_version};
        size_t i;
        for (i=0;i<sizeof(fields)/sizeof(fields[0]);++i) {
            unsigned int port_writes=f.port_writes;
            strcpy(fields[i],"changed firmware identity");
            CHECK(ugreenctl_led_cs201x_apply(c,2,&patch,NULL,error,sizeof(error))==-ENOTSUP);
            CHECK(f.port_writes==port_writes && f.target_writes==writes);
            fields[i][0]=0;
        }
    }
    f.base=0xf00;
    CHECK(ugreenctl_led_cs201x_apply(c,2,&patch,NULL,error,sizeof(error))==-ESTALE);
    CHECK(f.target_writes==writes);ugreenctl_led_cs201x_close(c);
    reset(&f);{
        const struct ugreenctl_led_access access={.force=false,.apply=true};
        CHECK(!ugreenctl_led_cs201x_test_open(&access,&io,&f,&c,error,sizeof(error)));
        CHECK(ugreenctl_led_cs201x_apply(c,0,&patch,NULL,error,sizeof(error))==-EPERM && !f.target_writes);
        ugreenctl_led_cs201x_close(c);
    }
    reset(&f);{
        const struct ugreenctl_led_access access={.force=true,.apply=false};
        CHECK(!ugreenctl_led_cs201x_test_open(&access,&io,&f,&c,error,sizeof(error)));
        CHECK(ugreenctl_led_cs201x_apply(c,0,&patch,NULL,error,sizeof(error))==-EPERM && !f.target_writes);
        ugreenctl_led_cs201x_close(c);
    }
}
static void test_unreadable_snapshots(void)
{
    struct fixture f;struct ugreenctl_led_cs201x *c;char error[512];unsigned int kind;
    struct ugreenctl_led_patch patch={UGREENCTL_LED_PATCH_BRIGHTNESS,{.brightness=222}};
    for (kind=0;kind<4;++kind) {
        reset(&f);c=open_fixture(&f);
        if (kind==0) f.memory[0]=255;
        if (kind==1) { f.memory[0]=2;f.memory[8]=1;f.memory[12]=1; }
        if (kind==2) f.read_error=-EIO;
        if (kind==3) f.unstable_read=8;
        CHECK(ugreenctl_led_cs201x_apply(c,0,&patch,NULL,error,sizeof(error))==(kind==2 ? -EIO : -EBADMSG));
        CHECK(!f.target_writes);ugreenctl_led_cs201x_close(c);
    }
}
static void test_rollback(void)
{
    struct fixture f;struct ugreenctl_led_cs201x *c;char error[512];unsigned char before[256];
    struct ugreenctl_led_patch patch={UGREENCTL_LED_PATCH_RGB,{.red=222,.green=223,.blue=224}};
    reset(&f);c=open_fixture(&f);memcpy(before,f.memory,sizeof(before));f.mismatch_after_write=1;
    CHECK(ugreenctl_led_cs201x_apply(c,0,&patch,NULL,error,sizeof(error))==-EIO);
    CHECK(strstr(error,"previous LED state restored") && !memcmp(before,f.memory,sizeof(before)));ugreenctl_led_cs201x_close(c);
    reset(&f);c=open_fixture(&f);f.fail_all_readbacks=true;
    CHECK(ugreenctl_led_cs201x_apply(c,0,&patch,NULL,error,sizeof(error))==-EIO);CHECK(strstr(error,"restoration failed"));ugreenctl_led_cs201x_close(c);
    reset(&f);c=open_fixture(&f);f.owner_after_write=true;
    CHECK(ugreenctl_led_cs201x_apply(c,0,&patch,NULL,error,sizeof(error))==-EBUSY);CHECK(strstr(error,"state may have changed") && f.target_writes==1);
    ugreenctl_led_cs201x_close(c);
}
static void test_identify(void)
{
    struct fixture f;struct ugreenctl_led_cs201x *c;char error[512];unsigned char before[256];volatile sig_atomic_t cancelled;unsigned int kind;
    for (kind=0;kind<4;++kind) {
        reset(&f);f.memory[0]=(unsigned char)(kind==0 ? 1 : 0);
        /* Also restore inactive timing that cannot be encoded as an on/off patch. */
        if (kind>=2) { f.memory[8]=2;f.memory[12]=4; }
        if (kind==3) memset(f.memory,0,16);
        c=open_fixture(&f);memcpy(before,f.memory,sizeof(before));cancelled=0;
        if (kind==1 || kind==2) f.cancel=&cancelled;
        CHECK(ugreenctl_led_cs201x_identify(c,0,1,&cancelled,error,sizeof(error))==((kind==1 || kind==2) ? -EINTR : 0));
        CHECK(!memcmp(before,f.memory,sizeof(before)) && f.target_writes>0);ugreenctl_led_cs201x_close(c);
    }
    reset(&f);c=open_fixture(&f);cancelled=1;
    CHECK(ugreenctl_led_cs201x_identify(c,0,1,&cancelled,error,sizeof(error))==-EINTR && !f.target_writes);
    CHECK(ugreenctl_led_cs201x_identify(c,0,0,NULL,error,sizeof(error))==-EINVAL);
    CHECK(ugreenctl_led_cs201x_identify(c,0,61,NULL,error,sizeof(error))==-EINVAL);ugreenctl_led_cs201x_close(c);
}
static struct ugreenctl_led_cs201x *open_holder(struct fixture *f,bool takeover)
{
    const struct ugreenctl_led_access access={.force=true,.apply=true};struct ugreenctl_led_cs201x *c=NULL;char error[512];
    int r=ugreenctl_led_cs201x_test_open_holder(&access,takeover,&io,f,&c,error,sizeof(error));
    if (r) fprintf(stderr,"holder open: %d %s\n",r,error);CHECK(!r && c);return c;
}
static void test_holder_lifecycle(void)
{
    struct fixture f;struct ugreenctl_led_cs201x *c=NULL;char error[512];unsigned char before[256];unsigned int flow;
    const struct ugreenctl_led_access access={.force=true,.apply=true};
    struct ugreenctl_led_patch patch={UGREENCTL_LED_PATCH_RGB|UGREENCTL_LED_PATCH_BRIGHTNESS,{.red=231,.green=177,.blue=99,.brightness=199}};
    reset(&f);f.memory[0x40]=0;
    CHECK(ugreenctl_led_cs201x_test_open_holder(&access,false,&io,&f,&c,error,sizeof(error))==-EBUSY && !c && !f.target_writes && !f.flow_writes);
    reset(&f);f.memory[0x40]=0;f.allow_flow=true;f.active=0;
    CHECK(ugreenctl_led_cs201x_test_open_holder(&access,true,&io,&f,&c,error,sizeof(error))==-ENODEV && !c && !f.flow_writes && !f.target_writes);
    for (flow=0;flow<2;++flow) {
        reset(&f);f.memory[0x40]=(unsigned char)flow;f.allow_flow=true;
        f.memory[2]=0;f.memory[10]=1;f.memory[14]=9; /* Raw inactive timing must survive holder shutdown. */
        memcpy(before,f.memory,sizeof(before));c=open_holder(&f,!flow);
        CHECK(f.flow_writes==(flow ? 0U : 1U) && f.memory[0x40]==1 && !f.target_writes);
        ugreenctl_led_cs201x_set_client(c,1234);
        CHECK(!ugreenctl_led_cs201x_apply(c,3,&patch,NULL,error,sizeof(error)));
        CHECK(!ugreenctl_led_cs201x_restore_close(c,error,sizeof(error)));
        CHECK(!memcmp(f.memory,before,sizeof(before)) && f.flow_writes==(flow ? 0U : 2U));
        CHECK(f.grants==f.revokes && f.unlocks==1);
    }
    reset(&f);f.memory[0x40]=0;f.allow_flow=true;
    { const struct ugreenctl_led_access read_only={.force=false,.apply=true};
      CHECK(ugreenctl_led_cs201x_test_open_holder(&read_only,true,&io,&f,&c,error,sizeof(error))==-EPERM && !c && !f.port_writes); }
}
static void test_holder_startup_failures(void)
{
    struct fixture f;struct ugreenctl_led_cs201x *c=NULL;char error[512];unsigned char before[256];unsigned int kind;
    const struct ugreenctl_led_access access={.force=true,.apply=true};
    for (kind=0;kind<4;++kind) {
        reset(&f);f.memory[0x40]=0;f.allow_flow=true;
        if (kind==0) f.memory[3]=255;
        if (kind==1) f.unstable_read=29;
        if (kind==2) f.memory[0x40]=2;
        if (kind==3) f.read_error=-EIO;
        CHECK(ugreenctl_led_cs201x_test_open_holder(&access,true,&io,&f,&c,error,sizeof(error))==(kind==3 ? -EIO : -EBADMSG));
        CHECK(!c && !f.flow_writes && !f.target_writes);
    }
    reset(&f);f.memory[0x40]=0;f.allow_flow=true;memcpy(before,f.memory,sizeof(before));f.flow_mismatch_once=true;
    CHECK(ugreenctl_led_cs201x_test_open_holder(&access,true,&io,&f,&c,error,sizeof(error))==-EIO && !c);
    CHECK(strstr(error,"previous holder state restored") && !memcmp(before,f.memory,sizeof(before)) && f.flow_writes==2);
    reset(&f);f.memory[0x40]=0;f.allow_flow=true;memcpy(before,f.memory,sizeof(before));f.tamper_on_takeover=true;
    CHECK(ugreenctl_led_cs201x_test_open_holder(&access,true,&io,&f,&c,error,sizeof(error))==-EIO && !c);
    CHECK(strstr(error,"takeover changed LED registers") && !memcmp(before,f.memory,sizeof(before)) && f.flow_writes==2);
}
static void test_raw_identify_sessions(void)
{
    struct fixture f;struct ugreenctl_led_cs201x *c;struct ugreenctl_led_value value;char error[512];unsigned char before[256];
    struct ugreenctl_led_patch patch={UGREENCTL_LED_PATCH_BRIGHTNESS,{.brightness=202}};
    reset(&f);f.allow_flow=true;f.memory[0]=0;f.memory[8]=2;f.memory[12]=9;memcpy(before,f.memory,sizeof(before));c=open_holder(&f,false);
    CHECK(!ugreenctl_led_cs201x_begin_identify(c,0,error,sizeof(error)));
    CHECK(!ugreenctl_led_cs201x_read(c,0,&value,error,sizeof(error)) && value.mode==UGREENCTL_LED_MODE_BLINK && value.blue==255);
    CHECK(ugreenctl_led_cs201x_begin_identify(c,1,error,sizeof(error))==-EBUSY);
    CHECK(ugreenctl_led_cs201x_apply(c,1,&patch,NULL,error,sizeof(error))==-EBUSY);
    CHECK(ugreenctl_led_cs201x_end_identify(c,1,error,sizeof(error))==-EINVAL);
    CHECK(!ugreenctl_led_cs201x_end_identify(c,0,error,sizeof(error)) && !memcmp(f.memory,before,sizeof(before)));
    CHECK(ugreenctl_led_cs201x_end_identify(c,0,error,sizeof(error))==-ENOENT);
    CHECK(!ugreenctl_led_cs201x_apply(c,1,&patch,NULL,error,sizeof(error)));
    CHECK(!ugreenctl_led_cs201x_begin_identify(c,2,error,sizeof(error)));
    CHECK(!ugreenctl_led_cs201x_restore_close(c,error,sizeof(error)) && !memcmp(f.memory,before,sizeof(before)));
    reset(&f);c=open_fixture(&f);memcpy(before,f.memory,sizeof(before));f.mismatch_after_write=1;
    CHECK(ugreenctl_led_cs201x_begin_identify(c,0,error,sizeof(error))==-EIO);
    CHECK(!memcmp(before,f.memory,sizeof(before)) && strstr(error,"previous LED state restored"));
    CHECK(ugreenctl_led_cs201x_apply(c,1,&patch,NULL,error,sizeof(error))==-EBUSY);
    CHECK(!ugreenctl_led_cs201x_end_identify(c,0,error,sizeof(error)));ugreenctl_led_cs201x_close(c);
    reset(&f);c=open_fixture(&f);f.read_error=-EIO;
    CHECK(ugreenctl_led_cs201x_begin_identify(c,0,error,sizeof(error))==-EIO && !f.target_writes);
    CHECK(ugreenctl_led_cs201x_end_identify(c,0,error,sizeof(error))==-ENOENT);ugreenctl_led_cs201x_close(c);
}
static void test_holder_restore_failures(void)
{
    struct fixture f;struct ugreenctl_led_cs201x *c;char error[512];unsigned int kind;
    struct ugreenctl_led_patch patch={UGREENCTL_LED_PATCH_BRIGHTNESS,{.brightness=202}};
    for (kind=0;kind<3;++kind) {
        reset(&f);f.memory[0x40]=0;f.allow_flow=true;c=open_holder(&f,true);
        CHECK(!ugreenctl_led_cs201x_apply(c,1,&patch,NULL,error,sizeof(error)));
        if (kind==0) f.owner_error=-EBUSY;
        if (kind==1) f.memory[0x40]=0;
        if (kind==2) f.fail_all_readbacks=true;
        CHECK(ugreenctl_led_cs201x_restore_close(c,error,sizeof(error))==(kind==2 ? -EIO : -EBUSY));
        CHECK(strstr(error,"state may have changed") && f.flow_writes==1 && f.grants==f.revokes && f.unlocks==1);
        CHECK(f.memory[0x40]==(kind==1 ? 0 : 1));
    }
}
int main(void)
{
    test_identity_and_ownership();test_reads();test_patch_and_guards();test_unreadable_snapshots();test_rollback();test_identify();
    test_holder_lifecycle();test_holder_startup_failures();test_raw_identify_sessions();test_holder_restore_failures();
#if !defined(__x86_64__) && !defined(__i386__)
    { struct ugreenctl_led_cs201x *c=NULL;char error[128];const struct ugreenctl_led_access access={.force=true,.apply=true};
      CHECK(ugreenctl_led_cs201x_open(&access,&c,error,sizeof(error))==-ENOTSUP && !c);
      CHECK(ugreenctl_led_cs201x_open_holder(&access,true,&c,error,sizeof(error))==-ENOTSUP && !c); }
#endif
    puts("CS2011 LED transport fixtures passed");return 0;
}
