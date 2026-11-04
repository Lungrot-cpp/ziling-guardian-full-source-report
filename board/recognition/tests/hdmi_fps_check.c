// Emulated clock and sensor inputs verify actual OSD bytes, not physical HDMI.
#define SENSOR_TEST
#include "../software/src/sensors/sensors.c"
#include <string.h>
static void check(int yes,unsigned code) {
    if(!yes){IO[0]=code;asm volatile("ebreak");for(;;){}}
}
static void check_text(const char* expected,unsigned code) {
    IO[5]=0;publish();
    char line[97];memcpy(line,text,96);line[96]=0;
    check(strstr(line,expected)!=0,code);
    for(unsigned i=0;i<192;i++)check(DISPLAY[1024+768+i]==(uint8_t)text[i],code+100);
}
int main(void) {
    IO[2]=100;check_text("CAM FPS:--",1);
    IO[6]=100;IO[7]=10;check_text("CAM FPS:--",2);
    IO[2]=IO[6]=1000100;IO[7]=40;check_text("CAM FPS:30.0",3);
    // Publishing the same camera frame must not count an extra image.
    for(unsigned i=0;i<10;i++)check_text("CAM FPS:30.0",4);
    IO[2]=IO[6]=2200100;IO[7]=70;check_text("CAM FPS:25.0",5);
    IO[2]+=1000000;check_text("CAM FPS:--",6);
    IO[6]=IO[2];IO[7]+=30;check_text("CAM FPS:--",7);
    IO[2]=IO[6]=IO[2]+1000000;IO[7]+=10;check_text("CAM FPS:10.0",8);
    // Camera time and frame IDs may each wrap without changing the rate.
    camera_fps_seen=camera_fps_valid=0;
    IO[2]=IO[6]=0xffff0000;IO[7]=0xfffffff0;check_text("CAM FPS:--",9);
    IO[2]=IO[6]=IO[2]+1000000;IO[7]+=30;check_text("CAM FPS:30.0",10);
    // Backward tags invalidate the old window rather than creating a huge FPS.
    IO[2]=IO[6]=IO[2]-100;IO[7]-=1;check_text("CAM FPS:--",11);
    IO[2]=IO[6]=IO[2]+1000000;IO[7]+=30;check_text("CAM FPS:30.0",12);
    // Largest valid temperature strings must leave the FPS label intact in 96 cells.
    env_count=1;env_history[0]=(env_sample){IO[2],-4000,10000};
    thermal_ok=1;thermal_stamp=thermal_end=IO[2];
    for(unsigned i=0;i<768;i++)heat[i]=-4000;
    heat[0]=heat[400]=30000;
    check_text("CAM FPS:30.0",13);
    IO[0]=0x600d;asm volatile("ebreak");return 0;
}
void telemetry_service(void) {}
