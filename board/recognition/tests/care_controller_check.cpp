#include "care_demo.h"
#include "care_logic.h"
#include <string.h>
static uint32_t now;
extern "C" uint32_t sys_now(){return now;}
static void advance(uint32_t n){now=n;*(volatile unsigned*)0xf8100308=n*1000;}
static void result(unsigned n){*(volatile unsigned*)0xf8100344=n;for(;;){}}
#define CHECK(x,n) do{if(!(x))result(n);}while(0)
static unsigned light(unsigned i){return *(volatile unsigned*)(0xf8101000+i*4);}
static unsigned opens(){return *(volatile unsigned*)0xf8100600;}
int main(){
 care_init();CHECK(strstr(care_json(),"\"hardware\":true"),101);
 CHECK(!care_action("cmd=open"),102);
 CHECK(care_configure("p0=0&p1=0&p2=1&p3=0&p4=1&p5=1&p6=0&p7=1"),103);
 CHECK(care_action("cmd=arm")&&care_action("cmd=open"),104);
 CHECK(!care_configure("neutral_us=1600"),105);
 TelemetryTrack t={1,0,900,0,0,0,96,20,480,216,0};
 advance(1000);care_update(&t,1,900000);CHECK(strstr(care_json(),"\"room\":0"),106);
 t.x0=1400;t.x1=1700;advance(1100);care_update(&t,1,1000000);CHECK(strstr(care_json(),"\"room\":0"),107);
 advance(1800);care_update(&t,1,1700000);CHECK(strstr(care_json(),"\"room\":1"),108);
 advance(4000);care_poll();CHECK(strstr(care_json(),"\"trail_ttl_ms\":9000"),114);
 advance(5000);care_poll();CHECK(strstr(care_json(),"\"valid\":false"),109);
 t.state=1;advance(5500);care_update(&t,1,5400000);CHECK(!care_pending()&&opens()==1,115);
 CHECK(care_action("cmd=disarm"),116);
 t.state=2;advance(6000);care_update(&t,1,5900000);
 CHECK(strstr(care_json(),"\"countdown_ms\":0")&&strstr(care_json(),"\"alarm\":true")&&care_pending(),110);
 CHECK(strstr(care_json(),"\"armed\":true")&&opens()==2,117);
 care::Config c=care::defaults();
 for(unsigned i=256;i<256+4*c.leds_per_room;i++)CHECK(light(i)==care::wire_color(c,255,0,0,c.strip_rgb),118);
 advance(6500);care_poll();for(unsigned i=256;i<288;i++)CHECK(light(i)==0,119);
 advance(7000);care_update(&t,1,6900000);CHECK(opens()==2,120);
 for(unsigned i=256;i<256+4*c.leds_per_room;i++)CHECK(light(i)==care::wire_color(c,255,0,0,c.strip_rgb),121);
 advance(9500);care_poll();CHECK(strstr(care_json(),"\"valid\":false"),129);
 for(unsigned i=256;i<288;i++)CHECK(light(i)==0,130);
 advance(10000);care_poll();for(unsigned i=256;i<284;i++)CHECK(light(i)!=0,131);
 CHECK(care_action("cmd=cancel"),111);advance(17000);care_poll();CHECK(strstr(care_json(),"\"alarm\":false"),112);
 for(unsigned i=256;i<288;i++)CHECK(light(i)==0,122);
 t.state=0;advance(18000);care_update(&t,1,17900000);
 // A second person's confirmed fall must trigger even while the chosen position is normal.
 TelemetryTrack both[]={t,t};both[1].id=2;both[1].state=2;
 advance(19000);care_update(both,2,18900000);CHECK(strstr(care_json(),"\"alarm\":true")&&opens()==3,113);
 CHECK(care_action("cmd=reset")&&!care_pending(),123);
 advance(20000);care_update(&t,1,19900000);CHECK(!care_pending(),124);
 t.state=2;advance(21000);care_update(&t,1,18000000);CHECK(!care_pending()&&opens()==3,125);
 CHECK(care_action("cmd=reset"),126);
 CHECK(care_configure("brightness=4"),127);
 advance(22000);care_update(&t,1,21900000);CHECK(care_pending()&&opens()==4,128);
 CHECK(!care_action("cmd=led_test")&&!care_action("cmd=led_test&index=256")&&!care_action("cmd=led_test&index=-1"),132);
 advance(23000);CHECK(care_action("cmd=led_test&index=0"),133);care_poll();
 for(unsigned i=0;i<256;i++)CHECK(light(i)==(i==0?0x080808u:0),134);
 CHECK(strstr(care_json(),"\"led_test_index\":0")&&opens()==4,135);
 advance(23040);CHECK(care_action("cmd=led_test&index=16"),136);care_poll();
 for(unsigned i=0;i<256;i++)CHECK(light(i)==(i==16?0x080808u:0),137);
 advance(28041);care_poll();CHECK(strstr(care_json(),"\"led_test_index\":-1"),138);
 CHECK(care_action("cmd=led_test&index=255")&&care_action("cmd=led_test_stop"),139);
 CHECK(strstr(care_json(),"\"led_test_index\":-1")&&opens()==4,140);
 result(0x600d1234);
}
