#define _POSIX_C_SOURCE 200809L
#include "led/led_service_test.h"
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const struct ugreenctl_led_descriptor descriptor={
    .name="disk1",.id=2,.bay=1,.mode_mask=3,.max_brightness=1,
    .color_mask=UGREENCTL_LED_COLOR_BIT(UGREENCTL_LED_COLOR_WHITE)|UGREENCTL_LED_COLOR_BIT(UGREENCTL_LED_COLOR_ORANGE)
};
static const struct ugreenctl_led_profile profile={
    .id="service-fixture",.product_name="fixture",.protocol="sysfs-gpio",.experimental=true,
    .leds=&descriptor,.led_count=1
};
static const struct ugreenctl_led_identity identity={.profile=&profile};
struct fixture { struct ugreenctl_led_value value,saved_value; unsigned int writes,reads; bool confirmed,interrupt_apply,saved,delay_read; pid_t client; };
static bool interrupt_next_server;
static bool identify_next_server;
static bool delay_next_server;
static volatile sig_atomic_t interrupted;
static void signal_received(int signum) { (void)signum; interrupted=1; }

static void check(bool condition,const char *message)
{
    if (!condition) { fprintf(stderr,"FAIL: %s\n",message); exit(1); }
}
static bool confirmed(void *context) { return ((struct fixture *)context)->confirmed; }
static void client(void *context,pid_t pid) { ((struct fixture *)context)->client=pid; }
static int read_value(void *context,uint8_t id,struct ugreenctl_led_value *value,char *error,size_t size)
{
    struct fixture *fixture=context;
    (void)error;(void)size;
    check(fixture->client>0,"only a kernel-authenticated client can read");
    if (id!=2) return -ENOTSUP;
    if (fixture->delay_read) {
        struct timespec delay={3,0};
        fixture->delay_read=false;
        while (nanosleep(&delay,&delay)<0 && errno==EINTR) {}
    }
    ++fixture->reads; *value=fixture->value; return 0;
}
static int apply(void *context,uint8_t id,const struct ugreenctl_led_patch *patch,
                  struct ugreenctl_led_value *value,char *error,size_t size)
{
    struct fixture *fixture=context;
    int result=ugreenctl_led_validate_target_patch(id==2 ? &descriptor : NULL,patch,error,size);
    check(fixture->client>0,"only authenticated client can write");
    if (result) return result;
    ++fixture->writes;
    if (patch->fields & UGREENCTL_LED_PATCH_MODE) fixture->value.mode=patch->value.mode;
    if (patch->fields & UGREENCTL_LED_PATCH_BRIGHTNESS) fixture->value.brightness=patch->value.brightness;
    if (patch->fields & UGREENCTL_LED_PATCH_COLOR) fixture->value.color=patch->value.color;
    if (fixture->interrupt_apply) {
        struct timespec delay={0,100000000};
        fixture->interrupt_apply=false;
        check(kill(getppid(),SIGUSR1)==0,"interrupt waiting client after hardware apply");
        (void)nanosleep(&delay,NULL);
    }
    *value=fixture->value; return 0;
}
static int begin_identify(void *context,uint8_t id,char *error,size_t size)
{
    struct fixture *fixture=context;
    (void)error;(void)size;
    check(fixture->client>0,"identify begins only for authenticated peer");
    if (id!=2) return -ENOTSUP;
    if (fixture->saved) return -EBUSY;
    fixture->saved_value=fixture->value; fixture->saved=true;
    fixture->value.mode=UGREENCTL_LED_MODE_BLINK; ++fixture->writes;
    return 0;
}
static int end_identify(void *context,uint8_t id,char *error,size_t size)
{
    struct fixture *fixture=context;
    (void)error;(void)size;
    check(fixture->client>0,"restore retains authenticated peer exclusion");
    if (id!=2 || !fixture->saved) return -EINVAL;
    fixture->value=fixture->saved_value; fixture->saved=false; ++fixture->writes;
    return 0;
}

static pid_t server(int *fd,bool identity_ok,int expected_writes)
{
    int sockets[2];
    pid_t child;
    check(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0,"create actual local IPC socket pair");
    child=fork(); check(child>=0,"fork holder fixture");
    if (!child) {
        struct fixture fixture={.confirmed=identity_ok,.interrupt_apply=interrupt_next_server,.delay_read=delay_next_server};
        const struct ugreenctl_led_service_test_ops ops={.context=&fixture,.identity=&identity,
            .confirmed=confirmed,.read=read_value,.apply=apply,.client=client,
            .begin_identify=identify_next_server ? begin_identify : NULL,
            .end_identify=identify_next_server ? end_identify : NULL};
        int result;
        close(sockets[0]);
        fixture.value.mode=UGREENCTL_LED_MODE_OFF; fixture.value.color=UGREENCTL_LED_COLOR_WHITE;
        fixture.value.unknown_fields=UGREENCTL_LED_PATCH_RGB|UGREENCTL_LED_PATCH_TIMING;
        result=ugreenctl_led_service_test_serve(sockets[1],&ops);
        close(sockets[1]);
        check(result==0 || result==-EPROTO || result==-EPIPE,"bounded server completion");
        check(fixture.writes==(unsigned int)expected_writes,"invalid requests cannot reach hardware writes");
        check(fixture.client==0,"authenticated-client exclusion cleared on all exits");
        check(!fixture.saved,"connection close restores pending raw identify snapshot");
        _exit(0);
    }
    close(sockets[1]); *fd=sockets[0]; return child;
}
static void join(pid_t child)
{
    int status;
    check(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0,"holder fixture completed");
}
static void raw_request(int fd,uint32_t *request,int expected)
{
    unsigned char response[384]; size_t done=0;
    check(send(fd,request,64,0)==64,"send fixed wire request");
    while (done<sizeof(response)) {
        ssize_t n=recv(fd,response+done,sizeof(response)-done,0);
        check(n>0,"read complete response"); done+=(size_t)n;
    }
    uint32_t status;
    memcpy(&status,response+8,sizeof(status));
    check((int)(int32_t)ntohl(status)==expected,"wire request enforced by service");
}

static void nonroot_peer(void)
{
    char directory[]="/tmp/ugreenctl-led-peer-XXXXXX";
    struct sockaddr_un address={.sun_family=AF_UNIX};
    struct fixture fixture={.confirmed=true};
    const struct ugreenctl_led_service_test_ops ops={.context=&fixture,.identity=&identity,
        .confirmed=confirmed,.read=read_value,.apply=apply,.client=client};
    int listener,accepted;
    pid_t child;
    check(mkdtemp(directory)!=NULL && chmod(directory,0777)==0,"create peer-auth fixture directory");
    check(snprintf(address.sun_path,sizeof(address.sun_path),"%s/socket",directory)<(int)sizeof(address.sun_path),"fixture socket path fits");
    listener=socket(AF_UNIX,SOCK_STREAM,0);
    check(listener>=0 && bind(listener,(struct sockaddr *)&address,sizeof(address))==0 &&
          chmod(address.sun_path,0666)==0 && listen(listener,1)==0,"bind peer-auth fixture listener");
    child=fork(); check(child>=0,"fork non-root client");
    if (!child) {
        int fd; char byte; ssize_t count;
        close(listener);
        check(setgid(1000)==0 && setuid(1000)==0,"drop client credentials before creating socket");
        fd=socket(AF_UNIX,SOCK_STREAM,0);
        check(fd>=0 && connect(fd,(struct sockaddr *)&address,sizeof(address))==0,"connect genuinely non-root peer");
        count=recv(fd,&byte,1,0);
        check(count==0 || (count<0 && errno==ECONNRESET),"unauthorized peer receives no hardware response");
        close(fd); _exit(0);
    }
    accepted=accept(listener,NULL,NULL); check(accepted>=0,"accept non-root peer");
    check(ugreenctl_led_service_test_serve(accepted,&ops)==-EPERM && !fixture.writes && !fixture.reads && !fixture.client,
          "kernel peer credentials reject non-root before callbacks or owner exclusions");
    close(accepted); close(listener); join(child);
    check(unlink(address.sun_path)==0 && rmdir(directory)==0,"remove owned peer-auth fixture");
}

int main(void)
{
    struct ugreenctl_led_service_client *remote=NULL;
    struct ugreenctl_led_access access={true,true};
    struct ugreenctl_led_patch patch={.fields=UGREENCTL_LED_PATCH_MODE|UGREENCTL_LED_PATCH_BRIGHTNESS|UGREENCTL_LED_PATCH_COLOR};
    struct ugreenctl_led_value value;
    char error[256];
    int fd; pid_t child;
    if (geteuid()!=0) { puts("LED holder IPC requires root; non-root build tests skip privileged round trip"); return 0; }
    nonroot_peer();
    delay_next_server=true;
    child=server(&fd,true,0);
    delay_next_server=false;
    check(ugreenctl_led_service_test_connect(fd,&identity,&access,&remote,error,sizeof(error))==0,"connect delayed-response fixture");
    check(ugreenctl_led_service_read(remote,2,&value,error,sizeof(error))==-ETIMEDOUT,
          "delayed response expires at absolute RPC deadline");
    check(!ugreenctl_led_service_identity_confirmed(remote),"timeout invalidates remote identity");
    check(ugreenctl_led_service_read(remote,2,&value,error,sizeof(error))==-ENOTCONN,
          "late frame cannot be consumed or reconfirm a disconnected session");
    ugreenctl_led_service_close(remote); join(child);
    identify_next_server=true;
    child=server(&fd,true,2);
    {
        uint32_t request[16]={htonl(0x55474c44U),htonl(1),htonl(4),htonl(2)};
        raw_request(fd,request,-EPERM);
        request[13]=htonl(3);
        raw_request(fd,request,0);
        raw_request(fd,request,-EBUSY);
        request[2]=htonl(3); request[4]=htonl(UGREENCTL_LED_PATCH_MODE);
        request[5]=htonl(UGREENCTL_LED_MODE_ON);
        raw_request(fd,request,-EBUSY);
        request[4]=request[5]=0;
        request[2]=htonl(5); request[3]=htonl(3);
        raw_request(fd,request,-EINVAL);
        /* Drop the connection with an active identify: the holder restores
         * before releasing the authenticated-client exclusion. */
    }
    close(fd); join(child);
    child=server(&fd,true,2);
    check(ugreenctl_led_service_test_connect(fd,&identity,&access,&remote,error,sizeof(error))==0,"connect raw identify session");
    {
        volatile sig_atomic_t cancel=1;
        check(ugreenctl_led_service_identify(remote,2,60,&cancel,error,sizeof(error))==0,
              "cancelled identify restores through explicit server boundary");
        check(ugreenctl_led_service_read(remote,2,&value,error,sizeof(error))==0 && value.mode==UGREENCTL_LED_MODE_OFF,
              "remote identify restores original state without decoding dormant timing");
    }
    ugreenctl_led_service_close(remote); join(child);
    identify_next_server=false;
    {
        struct sigaction handler={0},original;
        handler.sa_handler=signal_received;
        sigemptyset(&handler.sa_mask);
        check(sigaction(SIGUSR1,&handler,&original)==0,"install interrupt handler without SA_RESTART");
        interrupt_next_server=true;
        child=server(&fd,true,2);
        interrupt_next_server=false;
        check(ugreenctl_led_service_test_connect(fd,&identity,&access,&remote,error,sizeof(error))==0,"connect interrupt fixture");
        patch.fields=UGREENCTL_LED_PATCH_MODE;
        patch.value.mode=UGREENCTL_LED_MODE_ON;
        check(ugreenctl_led_service_apply(remote,2,&patch,&value,error,sizeof(error))==0 && interrupted,
              "interrupted apply completes response before cancellation");
        check(ugreenctl_led_service_identity_confirmed(remote),"signal preserves authenticated exclusive session");
        patch.value.mode=UGREENCTL_LED_MODE_OFF;
        check(ugreenctl_led_service_apply(remote,2,&patch,&value,error,sizeof(error))==0 && value.mode==UGREENCTL_LED_MODE_OFF,
              "same session restores original output after interrupted response");
        ugreenctl_led_service_close(remote); join(child);
        check(sigaction(SIGUSR1,&original,NULL)==0,"restore signal handler");
        patch.fields=UGREENCTL_LED_PATCH_MODE|UGREENCTL_LED_PATCH_BRIGHTNESS|UGREENCTL_LED_PATCH_COLOR;
    }
    child=server(&fd,true,1);
    check(ugreenctl_led_service_test_connect(fd,&identity,&access,&remote,error,sizeof(error))==0,"handshake with exact profile");
    check(ugreenctl_led_service_identity_confirmed(remote),"holder identity confirmation");
    check(ugreenctl_led_service_read(remote,2,&value,error,sizeof(error))==0 && value.mode==UGREENCTL_LED_MODE_OFF,
          "read original physical state through IPC");
    check(value.unknown_fields==(UGREENCTL_LED_PATCH_RGB|UGREENCTL_LED_PATCH_TIMING),"unknown fields survive wire encoding");
    patch.value.mode=UGREENCTL_LED_MODE_ON; patch.value.brightness=1; patch.value.color=UGREENCTL_LED_COLOR_ORANGE;
    check(ugreenctl_led_service_apply(remote,2,&patch,&value,error,sizeof(error))==0 &&
          value.mode==UGREENCTL_LED_MODE_ON && value.brightness==1 && value.color==UGREENCTL_LED_COLOR_ORANGE,
          "atomic intent and real readback survive wire encoding");
    patch.fields=UGREENCTL_LED_PATCH_RGB; patch.value.red=255;
    check(ugreenctl_led_service_apply(remote,2,&patch,&value,error,sizeof(error))==-ENOTSUP,"unsupported RGB refused by target");
    ugreenctl_led_service_close(remote); join(child);
    access.apply=false;
    child=server(&fd,true,0);
    check(ugreenctl_led_service_test_connect(fd,&identity,&access,&remote,error,sizeof(error))==0,"read-only session connects");
    patch.fields=UGREENCTL_LED_PATCH_MODE; patch.value.mode=UGREENCTL_LED_MODE_ON;
    check(ugreenctl_led_service_apply(remote,2,&patch,&value,error,sizeof(error))==-EPERM,"read-only client cannot write");
    check(ugreenctl_led_service_identify(remote,2,1,NULL,error,sizeof(error))==-EPERM,
          "read-only client cannot start raw identify");
    ugreenctl_led_service_close(remote); join(child);
    access.apply=true;
    child=server(&fd,false,0);
    check(ugreenctl_led_service_test_connect(fd,&identity,&access,&remote,error,sizeof(error))==-ENOTSUP && !remote,"unconfirmed hardware rejects handshake");
    join(child);
    child=server(&fd,true,0);
    {
        struct ugreenctl_led_profile other=profile;
        struct ugreenctl_led_identity wrong=identity;
        other.id="wrong-profile"; wrong.profile=&other;
        check(ugreenctl_led_service_test_connect(fd,&wrong,&access,&remote,error,sizeof(error))==-EPROTO && !remote,"holder cannot silently control another model");
    }
    join(child);
    child=server(&fd,true,0);
    {
        uint32_t request[16]={htonl(0x55474c44U),htonl(1),htonl(3),htonl(2),htonl(UGREENCTL_LED_PATCH_MODE),htonl(UGREENCTL_LED_MODE_ON)};
        raw_request(fd,request,-EPERM); /* Bypass client helper: server still guards flags. */
        request[13]=htonl(3); request[4]=htonl(0x80000000U);
        raw_request(fd,request,-EINVAL);
        request[1]=htonl(99);
        raw_request(fd,request,-EPROTO);
    }
    close(fd); join(child);
    puts("LED holder IPC, raw identify restoration, authorization and identity tests passed");
    return 0;
}
