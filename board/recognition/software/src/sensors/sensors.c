#include <stdint.h>
#include <math.h>
#include "vendor/MLX90640_API.h"
#include "vendor/MLX90640_I2C_Driver.h"
#include "sensors.h"
#include "thermal_schedule.h"
#include "osd_labels.h"

#ifdef SENSOR_TEST
#define IO ((volatile uint32_t *)0x10000000u)
#define DISPLAY ((volatile uint32_t *)0x20000000u)
#else
#include "soc.h"
#define IO ((volatile uint32_t *)(IO_APB_SLAVE_0_INPUT + 0x0300u))
#define DISPLAY ((volatile uint32_t *)(IO_APB_SLAVE_0_INPUT + 0xa000u))
#endif
#ifndef SENSOR_TEST
#define HW ((volatile uint32_t *)(IO_APB_SLAVE_0_INPUT + 0x0400u))
static unsigned hw_env_seq, hw_seq[2], filter_frames, hw_errors_seen;
static unsigned calculation_pending,calculation_pixel;
static uint32_t calculation_stamp;
static float calculation_tr;
static unsigned service_enabled;
static unsigned alert_monitoring;
static uint32_t background_completed;
#endif
#define EMISSIVITY 0.95f
#define INVALID_TEMP (-32768)
static uint32_t gpio[2];
static uint32_t now(void) { return IO[2]; }
static void delay_us(uint32_t n) { uint32_t t=now(); while ((uint32_t)(now()-t)<n) {} }
static void lines(unsigned b, unsigned scl, unsigned sda) {
    gpio[b]=scl | (sda<<1); IO[b]=gpio[b];
}
static int raise_scl(unsigned b) {
    IO[b]=gpio[b]&~1u; gpio[b]&=~1u;
    uint32_t t=now();
    while (!(IO[b]&0x100)) if ((uint32_t)(now()-t)>2000) return -1;
    delay_us(b?2:5); return 0;
}
static int stop(unsigned b) {
    lines(b,1,1); delay_us(b?2:5);
    int e=raise_scl(b); lines(b,0,0); delay_us(b?2:5); return e;
}
static int start(unsigned b) {
    lines(b,1,0); delay_us(b?2:5);
    if (raise_scl(b)) return -1;
    if (!(IO[b]&0x200)) return -1;
    lines(b,0,1); delay_us(b?2:5); lines(b,1,1); return 0;
}
static int byte_out(unsigned b,uint8_t x) {
    for (int i=7;i>=0;i--) {
        lines(b,1,!(x&(1u<<i))); delay_us(b?2:5);
        if (raise_scl(b)) return -1;
        lines(b,1,gpio[b]>>1);
    }
    lines(b,1,0); delay_us(b?2:5);
    if (raise_scl(b)) return -1;
    int e=(IO[b]&0x200)?-1:0;
    lines(b,1,0); return e;
}
static int byte_in(unsigned b,uint8_t *x,int last) {
    unsigned value=0;
    for (unsigned i=0;i<8;i++) {
        lines(b,1,0); delay_us(b?2:5);
        if (raise_scl(b)) return -1;
        value=(value<<1)|((IO[b]>>9)&1);
        lines(b,1,0);
    }
    lines(b,1,!last); delay_us(b?2:5);
    if (raise_scl(b)) return -1;
    lines(b,1,0); *x=value; return 0;
}
static void recover(unsigned b) {
    lines(b,0,0);
    for (unsigned i=0;i<9;i++) { lines(b,1,0); delay_us(b?2:5); if(raise_scl(b)) break; }
    stop(b); lines(b,0,0);
}
int MLX90640_I2CRead(uint8_t address,uint16_t reg,uint16_t count,uint16_t *data) {
    int e=start(1)||byte_out(1,address<<1)||byte_out(1,reg>>8)||byte_out(1,reg)
          ||start(1)||byte_out(1,(address<<1)|1);
    for(unsigned i=0;i<count&&!e;i++) {
        uint8_t hi,lo;
        e=byte_in(1,&hi,0)||byte_in(1,&lo,i==count-1);
        if(!e) data[i]=((uint16_t)hi<<8)|lo;
    }
    int end=stop(1);
    return e||end ? -1:0;
}
int MLX90640_I2CWrite(uint8_t address,uint16_t reg,uint16_t data) {
    int e=start(1)||byte_out(1,address<<1)||byte_out(1,reg>>8)||byte_out(1,reg)
          ||byte_out(1,data>>8)||byte_out(1,data);
    int end=stop(1);
    return e||end ? -1:0;
}
static uint8_t crc8(const uint8_t *p) {
    uint8_t c=0xff;
    for(unsigned i=0;i<2;i++) { c^=p[i]; for(unsigned j=0;j<8;j++) c=(c&0x80)?(c<<1)^0x31:c<<1; }
    return c;
}

// Four accepted samples, not an exponential average. Isolated jumps are rejected;
// three consecutive similar large steps are accepted as a real scene change.
typedef struct { int16_t h[4], candidate; uint8_t count,index,jumps; } filter_t;
static int16_t filtered(filter_t *f,int16_t value,int limit) {
    int sum=0;
    for(unsigned i=0;i<f->count;i++) sum+=f->h[i];
    int prev=f->count?sum/f->count:value;
    int d=value-prev; if(d<0)d=-d;
    if(f->count && d>limit) {
        int c=value-f->candidate; if(c<0)c=-c;
        f->jumps=(f->jumps&&c<limit/2)?f->jumps+1:1;
        f->candidate=value;
        if(f->jumps<3)return prev;
        f->count=0; f->index=0;
    }
    f->jumps=0; f->h[f->index]=value; f->index=(f->index+1)&3;
    if(f->count<4)f->count++;
    sum=0; for(unsigned i=0;i<f->count;i++)sum+=f->h[i];
    return sum/f->count;
}
static filter_t thermal_filter[768],temp_filter,rh_filter;
static int16_t heat[768];
static int16_t previous_heat[768];
static int16_t raw_heat[768],previous_raw_heat[768];
static uint32_t previous_thermal_stamp;
static unsigned previous_thermal_ok;
static int16_t env_t,env_rh;
static uint32_t thermal_stamp,thermal_end,previous_thermal_end;
static unsigned env_ok,thermal_ok,mlx_ready,submask;
typedef struct { uint32_t stamp; int16_t t,rh; } env_sample;
static env_sample env_history[8];
static unsigned env_count,env_index;
static uint32_t sub_stamp[2];
static uint32_t errors_sht,errors_mlx,thermal_frames,overwritten,torn,pair_drops;
static uint16_t ee[832],frame[834],check_ee[832];
static paramsMLX90640 calibration;
static float temperatures[768];

static void sample_sht(void) {
    uint8_t b[6];
    int e=0;uint32_t stamp;
#ifndef SENSOR_TEST
    if(mlx_ready) {
    unsigned seq=HW[10];if(!seq || seq==hw_env_seq)return;
    uint32_t hi=HW[13],lo=HW[12];stamp=HW[11];
    if(seq!=HW[10])return;
    hw_env_seq=seq;
    b[0]=hi>>8;b[1]=hi;b[2]=lo>>24;b[3]=lo>>16;b[4]=lo>>8;b[5]=lo;
    } else
#endif
    {
    e=start(0)||byte_out(0,0x88)||byte_out(0,0x24)||byte_out(0,0x00);
    int end=stop(0); e|=end;
    stamp=now();
    if(!e) {
        delay_us(16000);
        e=start(0)||byte_out(0,0x89);
        for(unsigned i=0;i<6&&!e;i++)e=byte_in(0,&b[i],i==5);
        end=stop(0); e|=end;
    }
    }
    if(e || crc8(b)!=b[2] || crc8(b+3)!=b[5]) {
        errors_sht++;
#ifdef SENSOR_TEST
        recover(0);
#else
        if(!mlx_ready)recover(0);
#endif
        return;
    }
    uint32_t t=((uint32_t)b[0]<<8)|b[1],rh=((uint32_t)b[3]<<8)|b[4];
    int tc=-4500+(int)((17500*t+32767)/65535);
    int hc=(10000*rh+32767)/65535;
    if(tc<-4000 || tc>12500 || hc>10000) {errors_sht++;return;}
    env_t=filtered(&temp_filter,tc,500);
    env_rh=filtered(&rh_filter,hc,1500);
    env_ok=1;
    env_history[env_index]=(env_sample){stamp,env_t,env_rh};
    env_index=(env_index+1)&7;if(env_count<8)env_count++;
}
static int initialise_mlx(void) {
    recover(1);
    if(MLX90640_DumpEE(0x33,ee) || MLX90640_DumpEE(0x33,check_ee))return -1;
    for(unsigned i=0;i<832;i++)if(ee[i]!=check_ee[i])return -1;
    // Reject blank EEPROM and zero divisors before parameter extraction.
    if(!ee[33] || ee[33]==0xffff || !ee[48] || !(ee[51]&0xff00) || !(ee[50]&0x03ff))return -1;
    if(MLX90640_ExtractParameters(ee,&calibration))return -1;
    if(!calibration.kVdd || !calibration.KtPTAT)return -1;
    for(unsigned i=0;i<768;i++)if(!calibration.alpha[i])return -1;
    MLX90640_PrepareTemperatureCache(&calibration);
    if(MLX90640_SetChessMode(0x33) || MLX90640_SetRefreshRate(0x33,V6_MLX_REFRESH_CODE))return -1;
    submask=0; return 0;
}
static void sample_mlx(void) {
    uint16_t after,control;uint32_t stamp;
#ifndef SENSOR_TEST
    if(calculation_pending)goto calculate;
#endif
#ifndef SENSOR_TEST
    int chosen=-1;uint32_t oldest=0;
    for(unsigned sub=0;sub<2;sub++) {
        uint32_t seq=HW[3+sub],ts=HW[5+sub];
        if(!seq || (seq&1) || seq==hw_seq[sub] || !ts)continue;
        uint32_t age=now()-ts;
        if(age>1500000)continue;
        if(chosen<0 || age>oldest){chosen=(int)sub;oldest=age;}
    }
    if(chosen<0)return;
    unsigned seq=HW[3+chosen];stamp=HW[5+chosen];control=HW[7+chosen];after=chosen;
    for(unsigned i=0;i<832;i++){HW[1]=chosen*832+i;frame[i]=HW[2];}
    asm volatile("fence iorw,iorw" ::: "memory");
    if(seq!=HW[3+chosen] || (seq&1)){torn++;return;}
    if(hw_seq[chosen] && (uint32_t)(seq-hw_seq[chosen])>2)overwritten+=(uint32_t)(seq-hw_seq[chosen])/2-1;
    hw_seq[chosen]=seq;
#else
    uint16_t status;
    if(MLX90640_I2CRead(0x33,0x8000,1,&status))goto error;
    if(!(status&8))return;

    for(unsigned attempt=0;attempt<2;attempt++) {
        stamp=now();
        if(MLX90640_I2CWrite(0x33,0x8000,0x0030) ||
           MLX90640_I2CRead(0x33,0x0400,832,frame) ||
           MLX90640_I2CRead(0x33,0x8000,1,&after))goto error;
        if(!(after&8))break;
        // Retry a torn burst once now, without waiting another inference cycle.
        errors_mlx++;
        // The rejected raw burst has not changed the calculated opposite half.
        if(attempt==1){submask=0;return;}
    }
    if(MLX90640_I2CRead(0x33,0x800d,1,&control))goto error;
#endif
    if(!(control&0x1000) || !frame[778] || frame[778]==0x7fff)goto error;
    // Use the status AFTER the burst, as in Melexis GetFrameData. A conversion
    // can complete between the initial ready poll and clearing the ready flag.
    unsigned sub=after&1;
    frame[832]=control; frame[833]=sub;
    float ta=MLX90640_GetTa(frame,&calibration),vdd=MLX90640_GetVdd(frame,&calibration);
    if(!isfinite(ta)||ta<-40||ta>85||!isfinite(vdd)||vdd<3||vdd>3.6f)goto error;
#ifdef SENSOR_TEST
    MLX90640_CalculateTo(frame,&calibration,EMISSIVITY,ta-8.0f,temperatures);
#else
    calculation_pending=1;calculation_pixel=0;calculation_stamp=stamp;calculation_tr=ta-8.0f;
calculate:
    MLX90640_CalculateToRange(frame,&calibration,EMISSIVITY,calculation_tr,temperatures,calculation_pixel,calculation_pixel+96);
    calculation_pixel+=96;
    if(calculation_pixel<768)return;
    calculation_pending=0;stamp=calculation_stamp;
    sub=frame[833];
#endif
    sub_stamp[sub]=stamp;
    submask|=1u<<sub;
    if(submask!=3)return;
    int32_t delta=(int32_t)(sub_stamp[0]-sub_stamp[1]);
    uint32_t dt=delta<0?-(uint32_t)delta:(uint32_t)delta;
    if(dt>V6_THERMAL_PAIR_US) {pair_drops++;submask=1u<<sub;return;}
    MLX90640_BadPixelsCorrection(calibration.brokenPixels,temperatures,1,&calibration);
    MLX90640_BadPixelsCorrection(calibration.outlierPixels,temperatures,1,&calibration);
    previous_thermal_stamp=thermal_stamp;previous_thermal_end=thermal_end;previous_thermal_ok=thermal_ok;
    for(unsigned i=0;i<768;i++){previous_heat[i]=heat[i];previous_raw_heat[i]=raw_heat[i];}
    unsigned good=0;
    for(unsigned i=0;i<768;i++) {
        float t=temperatures[i];
        if(isfinite(t)&&t>=-40&&t<=300) {
            raw_heat[i]=(int16_t)(t*100.0f);
            #ifdef SENSOR_TEST
            heat[i]=filtered(&thermal_filter[i],raw_heat[i],800);
#else
            HW[20]=i|((filter_frames<2?filter_frames:2)<<10);HW[21]=(uint16_t)raw_heat[i];
            heat[i]=(int16_t)HW[21];
#endif
            good++;
        } else {
            heat[i]=INVALID_TEMP;raw_heat[i]=INVALID_TEMP;
#ifndef SENSOR_TEST
            HW[20]=i|((filter_frames<2?filter_frames:2)<<10);HW[21]=(uint16_t)INVALID_TEMP;
#endif
        }
    }
#ifndef SENSOR_TEST
    if(filter_frames<2)filter_frames++;
#endif
    thermal_stamp=(int32_t)(sub_stamp[0]-sub_stamp[1])<0?sub_stamp[0]:sub_stamp[1];
    thermal_end=(int32_t)(sub_stamp[0]-sub_stamp[1])>0?sub_stamp[0]:sub_stamp[1];
    thermal_ok=(good>=760);if(thermal_ok)thermal_frames++;submask=0;
#ifndef SENSOR_TEST
    background_completed=now();
#endif
    return;
error:
    errors_mlx++; submask=0;
#ifdef SENSOR_TEST
    mlx_ready=0;recover(1);
#endif
}
static char text[216];
static unsigned pos, text_end;
static void put(const char *s) { while(*s&&pos<text_end)text[pos++]=*s++; }
static void number(uint32_t v) {
    char b[10]; unsigned n=0; do {b[n++]='0'+v%10;v/=10;}while(v&&n<10);
    while(n&&pos<text_end)text[pos++]=b[--n];
}
static void decimal(int v) {
    if(v<0){put("-");v=-v;} number(v/100);put(".");
    if(v%100<10)put("0");number(v%100);
}
static void seconds(uint32_t us) {
    uint32_t ms=us/1000,part=ms%1000;
    number(ms/1000);put(".");
    if(part<100)put("0");if(part<10)put("0");number(part);put("s");
}
static unsigned heat_color(int value,int low,int high) {
    int k=(value-low)*255/(high-low);
    if(k<0)k=0;if(k>255)k=255;
    unsigned r=0,g=0,b=0;
    if(k<64){g=k*4;b=255;}
    else if(k<128){g=255;b=255-(k-64)*4;}
    else if(k<192){r=(k-128)*4;g=255;}
    else {r=255;g=255-(k-192)*4;}
    return ((r>>3)<<11)|((g>>2)<<5)|(b>>3);
}
static void heat_range(const int16_t *heat,int *low,int *high,int *minimum,int *maximum) {
    int lo=30000,hi=-4000;
    for(unsigned i=0;i<768;i++)if(heat[i]!=INVALID_TEMP) {
        if(heat[i]<lo)lo=heat[i];if(heat[i]>hi)hi=heat[i];
    }
    *minimum=lo;*maximum=hi;
    *low=(lo>=0?lo/100:(lo-99)/100)*100;
    *high=(hi>=0?(hi+99)/100:hi/100)*100;
    // At least 6 C prevents sensor noise in a uniform scene filling the palette.
    if(*high-*low<600) {
        *low=((*low+*high)/200)*100-300;*high=*low+600;
    }
}
static const int16_t *select_thermal(uint32_t stamp,uint32_t *age) {
    const int16_t *selected=0;*age=0xffffffffu;
    if(thermal_ok && (int32_t)(stamp-thermal_end)>=0) {
        *age=stamp-thermal_stamp;selected=heat;
    }
    if(previous_thermal_ok && (int32_t)(stamp-previous_thermal_end)>=0 &&
       (uint32_t)(stamp-previous_thermal_stamp)<*age) {
        *age=stamp-previous_thermal_stamp;selected=previous_heat;
    }
    return selected;
}
static const char *event_name="NORMAL";
static unsigned event_confidence,event_mode,event_lost;
static unsigned camera_fps_seen,camera_fps_valid,camera_fps_tenths;
static uint32_t camera_fps_frame,camera_fps_stamp;
void sensors_event(unsigned state,unsigned confidence,unsigned weighted,unsigned lost) {
    event_name=state==2?"FALL":state==1?"SUSPECT":"NORMAL";
    event_confidence=confidence>100?100:confidence;event_mode=weighted;event_lost=lost;
}
static void publish(void) {
    unsigned s=IO[5],request=s&1;
    if(((s>>1)&1)!=request)return; // Do not overwrite a pending display bank.
    unsigned bank=request^1;
    uint32_t t=now();
    uint32_t video_stamp,video_frame;
    do {video_frame=IO[7];video_stamp=IO[6];}while(video_frame!=IO[7]);
    // Causal nearest sample to the DDR frame being read: no future sensor data.
    uint32_t best_age=0xffffffffu;int best=-1;
    for(unsigned i=0;i<env_count;i++) {
        int32_t age=(int32_t)(video_stamp-env_history[i].stamp);
        if(age>=0&&(uint32_t)age<best_age){best_age=age;best=i;}
    }
    unsigned video_live=video_frame&&(uint32_t)(t-video_stamp)<1000000;
    // Source camera rate from the frame ID/time tags used by HDMI, independent of inference.
    if(!video_live)camera_fps_seen=camera_fps_valid=0;
    else if(!camera_fps_seen) {
        camera_fps_seen=1;camera_fps_frame=video_frame;camera_fps_stamp=video_stamp;
    } else {
        uint32_t frames=video_frame-camera_fps_frame,elapsed=video_stamp-camera_fps_stamp;
        if((int32_t)frames<0 || (int32_t)elapsed<0) {
            camera_fps_valid=0;camera_fps_frame=video_frame;camera_fps_stamp=video_stamp;
        } else if(elapsed>=1000000) {
            camera_fps_tenths=((uint64_t)frames*10000000+elapsed/2)/elapsed;
            camera_fps_valid=1;camera_fps_frame=video_frame;camera_fps_stamp=video_stamp;
        }
    }
    uint32_t heat_age;
    const int16_t *display_heat=select_thermal(video_stamp,&heat_age);
    unsigned ht=video_live&&display_heat&&heat_age<4000000;
    unsigned et=video_live&&best>=0&&best_age<3000000;
    if(et){env_t=env_history[best].t;env_rh=env_history[best].rh;}
    int low=1500,high=4500,minimum=0,maximum=0;
    if(ht)heat_range(display_heat,&low,&high,&minimum,&maximum);
    for(unsigned i=0;i<768;i++) {
        unsigned rgb=0x3186;
        if(ht && display_heat[i]!=INVALID_TEMP)rgb=heat_color(display_heat[i],low,high);
        DISPLAY[bank*1024+i]=rgb;
    }
    for(unsigned i=0;i<216;i++)text[i]=' ';
    pos=0;text_end=96;put(CN_ENV);if(et)decimal(env_t);else put("--");put(CN_UNIT);
    put(CN_RH);if(et)decimal(env_rh);else put("--");put("% ");put(et?CN_OK:CN_STALE);
    put(CN_IR);put(ht?CN_OK:CN_STALE);
    put(CN_MIN);if(ht)decimal(minimum);else put("--");put(CN_UNIT);
    put(CN_MAX);if(ht)decimal(maximum);else put("--");put(CN_UNIT);
    put(CN_CENTER);if(ht&&display_heat[400]!=INVALID_TEMP)decimal(display_heat[400]);else put("--");put(CN_UNIT);
    put(" CAM FPS:");
    if(camera_fps_valid) {
        number(camera_fps_tenths/10);put(".");number(camera_fps_tenths%10);
    } else put("--");
    pos=96;text_end=192;put(event_name);put(" ");number(event_confidence);put("% ");put(event_mode?"W3 ":"V-DEGRADED ");if(event_lost)put("LOST/CONFLICT ");put(CN_FRAME);number(video_frame);put(CN_TIME);seconds(video_stamp);
    put(CN_IR_AGE);if(ht)seconds((uint32_t)(heat_age));else put("--s");
    put(CN_ENV_AGE);if(et)seconds(best_age);else put("--s");
    put(CN_ERROR);number(errors_mlx);put("/");number(errors_sht);
    put(CN_AUTO);
    if(ht) {
        if(low<0){put("-");number((uint32_t)(-low/100));}else number((uint32_t)(low/100));
        put("~");
        if(high<0){put("-");number((uint32_t)(-high/100));}else number((uint32_t)(high/100));
    } else put("--~--");
    put(CN_UNIT);
    for(unsigned i=0;i<192;i++)DISPLAY[bank*1024+768+i]=(uint8_t)text[i];
    // Legend and heatmap use the same range/function and the same display bank.
    for(unsigned i=0;i<24;i++)
        DISPLAY[bank*1024+960+i]=ht?heat_color(high-(high-low)*(int)i/23,low,high):0x3186;
    for(unsigned i=0;i<3;i++) {
        pos=192+i*8;text_end=pos+8;
        if(ht)decimal(high-(high-low)*(int)i/2);else put("--");put(CN_UNIT);
    }
    for(unsigned i=0;i<24;i++)DISPLAY[bank*1024+984+i]=(uint8_t)text[192+i];
    IO[5]=bank;
}
static uint32_t last_sht, last_init, last_display, last_poll;
void sensors_snapshot(uint32_t video_stamp, SensorSnapshot *out) {
    out->valid=0;
    uint32_t best_age;
    const int16_t *selected=select_thermal(video_stamp,&best_age);
    uint32_t env_age=0xffffffffu;int best=-1;
    for(unsigned i=0;i<env_count;i++) {
        int32_t age=(int32_t)(video_stamp-env_history[i].stamp);
        if(age>=0 && (uint32_t)age<env_age){env_age=age;best=i;}
    }
    // Position support needs a tighter age limit than the informational HUD.
    if(!selected || best_age>V6_THERMAL_AGE_US || best<0 || env_age>2000000)return;
    out->ambient_centi=env_history[best].t;out->humidity_centi=env_history[best].rh;
    out->thermal_age_us=best_age;out->environment_age_us=env_age;
    // Raw temperatures provide localization; display smoothing must not trail into fusion.
    const int16_t *raw=selected==heat ? raw_heat : previous_raw_heat;
    for(unsigned i=0;i<768;i++)out->heat[i]=raw[i];
    out->valid=1;
}
void sensors_readings(SensorReadings *out) {
    uint32_t t=now();
    out->oldest_us=thermal_stamp;out->newest_us=thermal_end;
    out->age_us=t-thermal_stamp;out->gap_us=thermal_end-thermal_stamp;
    out->thermal_valid=thermal_ok && out->age_us<=V6_THERMAL_AGE_US && out->gap_us<=V6_THERMAL_PAIR_US;
    for(unsigned i=0;i<768;i++)out->heat[i]=raw_heat[i];
    out->environment_valid=0;out->environment_age_us=0xffffffffu;
    for(unsigned i=0;i<env_count;i++) {
        uint32_t age=t-env_history[i].stamp;
        if(age<out->environment_age_us) {
            out->ambient_centi=env_history[i].t;out->humidity_centi=env_history[i].rh;
            out->environment_age_us=age;out->environment_valid=age<=2000000;
        }
    }
    out->capture_frames=IO[4];out->frames=thermal_frames;out->overwritten=overwritten;out->torn=torn;out->pair_drops=pair_drops;
    out->mlx_errors=errors_mlx;out->sht_errors=errors_sht;
}
int sensors_init(void) {
#ifndef SENSOR_TEST
    alert_monitoring=0;background_completed=now()-1000000u;
    if(IO[8]!=0x53454e31u)return -1;
    if(HW[15]!=0x41435132u)return -1;
    HW[0]=0;
#endif
    for(unsigned i=0;i<768;i++)heat[i]=INVALID_TEMP;
    lines(0,0,0);lines(1,0,0);delay_us(100000);recover(0);
    last_sht=now()-1000000;last_init=now()-2000000;
    last_display=now()-250000;last_poll=now()-50000;
    publish();
#ifndef SENSOR_TEST
    service_enabled=1;
#endif
    return 0;
}
void sensors_step(void) {
    uint32_t t=now();
    if((uint32_t)(t-last_sht)>=250000) {last_sht=t;sample_sht();}
    if(!mlx_ready && (uint32_t)(t-last_init)>=2000000) {
        last_init=t;mlx_ready=initialise_mlx()==0;if(!mlx_ready)errors_mlx++;
#ifndef SENSOR_TEST
        if(mlx_ready)HW[0]=1;
#endif
    }
    if(mlx_ready && (
#ifndef SENSOR_TEST
        thermal_work_due(t,background_completed,alert_monitoring,calculation_pending,submask)) && (
        calculation_pending ||
#endif
        (uint32_t)(t-last_poll)>=5000)) {
        last_poll=t;sample_mlx();
#ifndef SENSOR_TEST
        sample_mlx();
        unsigned hw_errors=HW[9];errors_mlx+=hw_errors-hw_errors_seen;hw_errors_seen=hw_errors;
#endif
    }
    if((uint32_t)(now()-last_display)>=250000) {last_display=now();publish();}
}

void sensors_monitor_alert(unsigned active) {
#ifndef SENSOR_TEST
    alert_monitoring=active;
#endif
}

extern void telemetry_service(void);
extern void onboard_http_poll(void) __attribute__((weak));
void v6_service(void) {
#ifndef SENSOR_TEST
    if(service_enabled){sensors_step();telemetry_service();if(onboard_http_poll)onboard_http_poll();}
#endif
}
