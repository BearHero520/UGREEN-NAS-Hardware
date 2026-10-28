#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
static FILE *test_fopen(const char *,const char *);
static int test_access(const char *,int);
static int test_socket(int,int,int);
static int test_ioctl(int,unsigned long,...);
static int test_close(int);
static unsigned test_ifindex(const char *);
#define fopen test_fopen
#define access test_access
#define socket test_socket
#define ioctl test_ioctl
#define close test_close
#define if_nametoindex test_ifindex
#undef _DEFAULT_SOURCE
#include "network/wol_interface.c"
#undef fopen
#undef access
#undef socket
#undef ioctl
#undef close
#undef if_nametoindex
static char root[256];
static unsigned writes,queries,not_supported,bad_readback,write_error,changed;
static unsigned current;
static const char *map(const char *p,char *out,size_t size)
{
    assert(!strncmp(p,"/sys",4));assert(snprintf(out,size,"%s%s",root,p+4)<(int)size);return out;
}
static FILE *test_fopen(const char *p,const char *mode) { char b[1024];return fopen(map(p,b,sizeof(b)),mode); }
static int test_access(const char *p,int mode) { char b[1024];return access(map(p,b,sizeof(b)),mode); }
static int test_socket(int a,int b,int c) { (void)a;(void)b;(void)c;return 42; }
static int test_close(int fd) { assert(fd==42);return 0; }
static unsigned test_ifindex(const char *name) { assert(!strcmp(name,"enp8s0"));return changed && queries ? 2 : 1; }
static int test_ioctl(int fd,unsigned long request,...)
{
    va_list args;struct ifreq *r;struct ethtool_wolinfo *w;
    assert(fd==42 && request==SIOCETHTOOL);
    va_start(args,request);r=va_arg(args,struct ifreq *);va_end(args);
    assert(!strcmp(r->ifr_name,"enp8s0"));w=(struct ethtool_wolinfo *)r->ifr_data;
    if (w->cmd==ETHTOOL_GWOL) { ++queries;w->supported=not_supported ? 0 : WAKE_MAGIC;w->wolopts=bad_readback ? 0 : current; }
    else { assert(w->cmd==ETHTOOL_SWOL);++writes;if (write_error) { errno=EIO;return -1; }current=w->wolopts; }
    return 0;
}
static void put(const char *suffix,const char *value)
{
    char p[1024],*c;FILE *f;
    snprintf(p,sizeof(p),"%s/%s",root,suffix);
    for (c=p+1;*c;++c) if (*c=='/') { *c=0;(void)mkdir(p,0700);*c='/'; }
    f=fopen(p,"w");assert(f);fputs(value,f);fclose(f);
}
int main(void)
{
    char tmp[]="/tmp/wol-explicit-XXXXXX",error[256]={0};
    struct ugreenctl_wol_profile p={"test","DXP2800 GT",NULL,NULL,NULL,NULL,0};
    enum ugreenctl_wol_policy policy=UGREENCTL_WOL_ON;
    assert(mkdtemp(tmp));strcpy(root,tmp);
    put("class/net/enp8s0/type","1\n");
    put("class/net/enp8s0/device/uevent","PCI_CLASS=020000\nPCI_SLOT_NAME=0000:08:00.0\n");
    assert(!ugreenctl_wol_interface(&p,"enp8s0",true,&policy,error,sizeof(error)) && writes==1 && queries==2);
    assert(policy==UGREENCTL_WOL_ON);
    writes=queries=0;not_supported=1;
    assert(ugreenctl_wol_interface(&p,"enp8s0",true,&policy,error,sizeof(error)) && !writes);
    not_supported=0;queries=0;changed=1;
    assert(ugreenctl_wol_interface(&p,"enp8s0",true,&policy,error,sizeof(error)) && !writes);
    changed=0;queries=0;write_error=1;
    assert(ugreenctl_wol_interface(&p,"enp8s0",true,&policy,error,sizeof(error)) && writes==1 && queries==1);
    write_error=0;writes=queries=0;bad_readback=1;
    assert(ugreenctl_wol_interface(&p,"enp8s0",true,&policy,error,sizeof(error)) && writes==1 && queries==2);
    puts("Explicit WOL: driver capability, interface replacement, write errors and readback checked");
    return 0;
}
