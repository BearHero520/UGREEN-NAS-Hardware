#define _DEFAULT_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int test_open(const char *,int,...);
static FILE *test_fopen(const char *,const char *);
static DIR *test_opendir(const char *);
static int test_lstat(const char *,struct stat *);
static ssize_t test_write(int,const void *,size_t);
#define open test_open
#define fopen test_fopen
#define opendir test_opendir
#define lstat test_lstat
#define write test_write
#undef _DEFAULT_SOURCE
#include "power/rtc_wake.c"
#undef open
#undef fopen
#undef opendir
#undef lstat
#undef write
static char root[256];
static unsigned writes,fail_write,mismatch;
static const char *map(const char *path,char *buffer,size_t size)
{
    if (!strncmp(path,"/sys",4)) { assert(snprintf(buffer,size,"%s%s",root,path+4)<(int)size);return buffer; }
    return path;
}
static int test_open(const char *p,int flags,...) { char b[1024];return open(map(p,b,sizeof(b)),flags); }
static FILE *test_fopen(const char *p,const char *mode) { char b[1024];return fopen(map(p,b,sizeof(b)),mode); }
static DIR *test_opendir(const char *p) { char b[1024];return opendir(map(p,b,sizeof(b))); }
static int test_lstat(const char *p,struct stat *s) { char b[1024];return lstat(map(p,b,sizeof(b)),s); }
static ssize_t test_write(int fd,const void *data,size_t size)
{
    ++writes;
    if (writes==fail_write) { errno=EIO;return -1; }
    assert(!ftruncate(fd,0));
    if (writes==mismatch) { assert(write(fd,"123",3)==3);return (ssize_t)size; }
    /* The real wakealarm exposes an empty file when cleared. */
    if (size==1 && *(const char *)data=='0') return 1;
    return write(fd,data,size);
}
static void put(const char *suffix,const char *text)
{
    char p[1024];char *c;FILE *f;
    snprintf(p,sizeof(p),"%s/%s",root,suffix);
    for (c=p+1;*c;++c) if (*c=='/') { *c=0;(void)mkdir(p,0700);*c='/'; }
    f=fopen(p,"w");assert(f);fputs(text,f);fclose(f);
}
int main(void)
{
    char tmp[]="/tmp/rtc-runtime-XXXXXX",error[256];
    struct ugreenctl_rtc_profile p={"test","DXP4800",NULL,NULL,"rtc_cmos",NULL,1};
    time_t target=time(NULL)+3600,observed=-1;
    assert(mkdtemp(tmp));strcpy(root,tmp);
    put("class/rtc/rtc3/name","rtc_cmos 00:03\n");put("class/rtc/rtc3/wakealarm","");
    { char clock[64];
      assert(ugreenctl_rtc_wake_set(&p,target,error,sizeof(error)) && writes==0);
      snprintf(clock,sizeof(clock),"%lld",(long long)time(NULL)+28800);
      put("class/rtc/rtc3/since_epoch",clock);
      assert(ugreenctl_rtc_wake_set(&p,target,error,sizeof(error)) && writes==0);
      snprintf(clock,sizeof(clock),"%lld",(long long)time(NULL)-28800);
      put("class/rtc/rtc3/since_epoch",clock);
      assert(ugreenctl_rtc_wake_set(&p,target,error,sizeof(error)) && writes==0);
      put("class/rtc/rtc3/since_epoch","invalid");
      assert(ugreenctl_rtc_wake_set(&p,target,error,sizeof(error)) && writes==0);
      snprintf(clock,sizeof(clock),"%lld",(long long)time(NULL));
      put("class/rtc/rtc3/since_epoch",clock);
    }
    assert(!ugreenctl_rtc_wake_set(&p,target,error,sizeof(error)) && writes==2);
    assert(!ugreenctl_rtc_wake_read(&p,&observed,error,sizeof(error)) && observed==target);
    assert(!ugreenctl_rtc_wake_clear(&p,error,sizeof(error)) && writes==3);
    assert(!ugreenctl_rtc_wake_read(&p,&observed,error,sizeof(error)) && observed==0);
    writes=0;fail_write=1;
    assert(ugreenctl_rtc_wake_set(&p,target,error,sizeof(error)) && writes==1);
    writes=0;fail_write=0;mismatch=1;
    assert(ugreenctl_rtc_wake_set(&p,target,error,sizeof(error)) && writes==1);
    writes=0;mismatch=2;
    assert(ugreenctl_rtc_wake_set(&p,target,error,sizeof(error)) && writes==2);
    writes=0;mismatch=0;
    assert(ugreenctl_rtc_wake_set(&p,time(NULL),error,sizeof(error)) && writes==0);
    put("class/rtc/rtc8/name","rtc_cmos 00:08\n");put("class/rtc/rtc8/wakealarm","");
    assert(ugreenctl_rtc_wake_set(&p,target,error,sizeof(error)) && writes==0);
    puts("RTC transactions: clear-before-set, pinned path, readback and fail-closed errors passed");
    return 0;
}
