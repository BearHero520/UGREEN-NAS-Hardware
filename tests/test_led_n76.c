#include "led/led_test.h"
#include "led/led_n76_protocol.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"N76 %d: %s\n",__LINE__,#x); exit(1); } } while (0)
struct n76_fixture {
    struct ugreenctl_led_identity identity;
    uint8_t regs[256];
    struct ugreenctl_n76_command writes[64];
    unsigned int count, owner_after, fail_write;
    int read_error, owner_error, lock_error;
    bool locked, ignore_mode, changed_bios;
};
static int owners(void *p) {
    struct n76_fixture *f=p;
    return f->owner_error ? f->owner_error : f->owner_after && f->count>=f->owner_after ? -EBUSY : 0;
}
static int lock(void *p) { struct n76_fixture *f=p; if (f->lock_error) return f->lock_error; CHECK(!f->locked); f->locked=true; return 0; }
static void unlock(void *p) { struct n76_fixture *f=p; CHECK(f->locked); f->locked=false; }
static int read_byte(void *p,uint8_t reg,uint8_t *v) {
    struct n76_fixture *f=p; CHECK(f->locked);
    /* No undocumented output/PWM/timing reads are permitted by this fixture. */
    CHECK(reg==0x5a || reg==0x5b || reg==0x5d || (reg>=0x50 && reg<=0x55));
    if (f->read_error) return f->read_error;
    *v=f->regs[reg]; return 0;
}
static int chip(void *p,uint16_t *id) { uint8_t hi,lo; int r=read_byte(p,0x5a,&hi); if (!r) r=read_byte(p,0x5b,&lo); if (!r) *id=(uint16_t)((unsigned)hi<<8|lo); return r; }
static int write_byte(void *p,uint8_t reg,uint8_t v) {
    struct n76_fixture *f=p; CHECK(f->locked && f->count<64);
    if (f->fail_write && f->count+1==f->fail_write) return -EREMOTEIO;
    f->writes[f->count].reg=reg; f->writes[f->count++].value=v;
    if (!(f->ignore_mode && reg>=0x50 && reg<=0x55)) f->regs[reg]=v;
    return 0;
}
static int legacy_read(void *p,uint8_t id,uint8_t *frame) { (void)p;(void)id;(void)frame; CHECK(false); return -EIO; }
static int legacy_write(void *p,uint8_t id,const uint8_t *frame) { (void)p;(void)id;(void)frame; CHECK(false); return -EIO; }
static int legacy_ack(void *p) { (void)p; CHECK(false); return -EIO; }
static void pause_ms(void *p,unsigned int ms) { (void)p; CHECK(ms==2 || ms==10); }
static int identity(void *p,struct ugreenctl_led_identity *out,char *e,size_t n) {
    struct n76_fixture *f=p; (void)e;(void)n; *out=f->identity;
    if (f->changed_bios) strcpy(out->bios_version,"updated"); return 0;
}
static const struct ugreenctl_led_test_io io={
    .owners=owners,.lock=lock,.unlock=unlock,.read_chip_id=chip,.read_status=legacy_read,
    .write_command=legacy_write,.read_ack=legacy_ack,.pause_ms=pause_ms,.read_identity=identity,
    .read_byte=read_byte,.write_byte=write_byte
};
static void init(struct n76_fixture *f,const char *model) {
    memset(f,0,sizeof(*f)); strcpy(f->identity.product_name,model);
    f->identity.profile=ugreenctl_led_profile_find(model,"");
    strcpy(f->identity.bios_version,"initial");
    f->regs[0x5a]=0xa5; f->regs[0x5b]=0xb5; f->regs[0x5d]=7;
}
static struct ugreenctl_led_access access={.force=true,.apply=true,.n76_direct=true,.n76_bus=1,.n76_address=0x31};
static int open_session(struct n76_fixture *f,struct ugreenctl_led_session **s) {
    char error[256];return ugreenctl_led_test_open(&f->identity,&access,&io,f,s,error,sizeof(error));
}
void ugreenctl_led_n76_fixture_tests(void)
{
    struct n76_fixture f;
    struct ugreenctl_led_session *s=NULL;
    struct ugreenctl_led_value value;
    struct ugreenctl_n76_plan plan;
    struct ugreenctl_led_patch patch={.fields=UGREENCTL_LED_PATCH_MODE|UGREENCTL_LED_PATCH_COLOR,
        .value={.mode=UGREENCTL_LED_MODE_ON,.color=UGREENCTL_LED_COLOR_ORANGE}};
    char error[256];
    unsigned int i, before;
    const unsigned int bits[]={0,4,2};
    init(&f,"DH2600"); CHECK(!open_session(&f,&s));
    CHECK(ugreenctl_led_identity_confirmed(s));
    CHECK(!ugreenctl_led_read(s,0,&value,error,sizeof(error)));
    CHECK(value.n76_mode_valid && value.unknown_fields==31 && !value.cached_fields && !value.unverified_write);
    CHECK(ugreenctl_led_identify(s,0,1,NULL,error,sizeof(error))==-ENOTSUP && !f.count);
    for (i=0;i<3;++i) {
        f.count=0;
        CHECK(!ugreenctl_led_apply(s,(uint8_t)i,&patch,&value,error,sizeof(error)));
        CHECK(f.count==4 && f.writes[0].reg==0x50+bits[i] && f.writes[1].reg==0x51+bits[i]);
        CHECK(f.writes[2].reg==0xb1 && f.writes[2].value==(3U<<bits[i]));
        CHECK(f.writes[3].reg==0xa0 && f.writes[3].value==(2U<<bits[i]));
        CHECK(value.unverified_write && value.unknown_fields==31 && !value.cached_fields);
    }
    f.count=0; patch.value.mode=UGREENCTL_LED_MODE_BLINK;
    patch.fields|=UGREENCTL_LED_PATCH_TIMING; patch.value.on_ms=0x1234;patch.value.off_ms=0x4321;
    CHECK(!ugreenctl_led_apply(s,1,&patch,&value,error,sizeof(error)));
    CHECK(f.count==9 && f.writes[3].reg==0xca && f.writes[3].value==0x34 && f.writes[4].value==0x12);
    CHECK(f.writes[5].reg==0xda && f.writes[5].value==0x21 && f.writes[6].value==0x43);
    CHECK(f.writes[8].reg==0x55 && f.writes[8].value==1);
    CHECK(value.n76_modes[0]==0 && value.n76_modes[1]==1 && value.unknown_fields==31);
    before=f.count;patch.value.color=UGREENCTL_LED_COLOR_WHITE_ORANGE;
    CHECK(ugreenctl_led_apply(s,1,&patch,&value,error,sizeof(error))==-ENOTSUP && f.count==before);
    patch.fields=UGREENCTL_LED_PATCH_MODE;patch.value.mode=UGREENCTL_LED_MODE_OFF;
    CHECK(!ugreenctl_led_apply(s,1,&patch,&value,error,sizeof(error)) && f.count==before+3);
    CHECK(value.unverified_write && (value.unknown_fields&UGREENCTL_LED_PATCH_MODE));
    before=f.count; f.changed_bios=true;
    CHECK(ugreenctl_led_apply(s,0,&patch,&value,error,sizeof(error))==-ESTALE && f.count==before);
    f.changed_bios=false; f.regs[0x5d]++;
    CHECK(ugreenctl_led_apply(s,0,&patch,&value,error,sizeof(error))==-ESTALE && f.count==before);
    f.regs[0x5d]--; f.owner_error=-EBUSY;
    CHECK(ugreenctl_led_apply(s,0,&patch,&value,error,sizeof(error))==-EBUSY && f.count==before);
    f.owner_error=0; f.regs[0x5b]=0;
    CHECK(ugreenctl_led_apply(s,0,&patch,&value,error,sizeof(error))==-ENOTSUP && f.count==before);
    ugreenctl_led_close(s); CHECK(!f.locked);
    /* Every flag/identity/lock denial leaves zero command writes. */
    for (i=0;i<7;++i) {
        init(&f,"DXP480T Plus");
        if (i==0) f.regs[0x5a]=0;
        if (i==1) f.read_error=-EIO;
        if (i==2) f.owner_error=-EBUSY;
        if (i==3) f.lock_error=-EBUSY;
        if (i==4) access.n76_address=0x3a;
        if (i==5) access.apply=false;
        if (i==6) access.force=false;
        if (i<5) CHECK(open_session(&f,&s)<0);
        else {
            CHECK(!open_session(&f,&s));
            CHECK(ugreenctl_led_apply(s,0,&patch,&value,error,sizeof(error))==-EPERM);
            ugreenctl_led_close(s);
        }
        CHECK(!f.count && !f.locked);
        access.apply=true;access.force=true;access.n76_address=0x31;
    }
    init(&f,"DXP4800"); CHECK(open_session(&f,&s)==-ENOTSUP && !f.count && !f.locked);
    init(&f,"DXP480T Plus"); CHECK(!open_session(&f,&s));
    patch.fields=UGREENCTL_LED_PATCH_MODE|UGREENCTL_LED_PATCH_COLOR|UGREENCTL_LED_PATCH_BRIGHTNESS;
    patch.value.mode=UGREENCTL_LED_MODE_ON;patch.value.color=UGREENCTL_LED_COLOR_WHITE_ORANGE;patch.value.brightness=128;
    CHECK(!ugreenctl_led_apply(s,0,&patch,&value,error,sizeof(error)) && f.count==6);
    CHECK(f.writes[3].reg==0xe0 && f.writes[3].value==128 && f.writes[4].reg==0xe1 && f.writes[5].value==3);
    CHECK(value.unknown_fields==31 && value.unverified_write);
    CHECK(ugreenctl_n76_plan(f.identity.profile,1,&patch,&plan,error,sizeof(error))<0 && !plan.count);
    before=f.count;patch.value.brightness=1;
    CHECK(ugreenctl_led_apply(s,0,&patch,&value,error,sizeof(error))<0 && f.count==before);
    patch.value.brightness=128;patch.fields=UGREENCTL_LED_PATCH_COLOR;
    CHECK(ugreenctl_led_apply(s,0,&patch,&value,error,sizeof(error))<0 && f.count==before);
    ugreenctl_led_close(s);
    /* Late owners, failing I/O and mode mismatch stop the transaction; retries
     * cannot silently continue or attempt rollback of unknown outputs. */
    patch.fields=UGREENCTL_LED_PATCH_MODE;patch.value.mode=UGREENCTL_LED_MODE_OFF;
    for (i=0;i<3;++i) {
        init(&f,"DXP480T Plus"); f.regs[0x50]=1; CHECK(!open_session(&f,&s));
        if (i==0) f.owner_after=1;
        if (i==1) f.fail_write=2;
        if (i==2) f.ignore_mode=true;
        CHECK(ugreenctl_led_apply(s,0,&patch,&value,error,sizeof(error))<0 && f.count==1);
        f.owner_after=0;f.fail_write=0;f.ignore_mode=false;
        CHECK(ugreenctl_led_apply(s,0,&patch,&value,error,sizeof(error))==-EIO && f.count==1);
        CHECK(!ugreenctl_led_identity_confirmed(s));ugreenctl_led_close(s);
    }
    puts("N76 direct: exact maps, unverified outputs, readback errors and fail-closed writes passed");
}
