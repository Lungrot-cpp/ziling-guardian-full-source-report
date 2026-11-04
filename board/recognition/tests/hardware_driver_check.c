#include "../software/src/sensors/sensors.c"
static void check(int yes,unsigned code) {
 if(!yes){IO[17]=code;asm volatile("ebreak");for(;;){}}
}
int main(void) {
 mlx_ready=1;sample_sht();
 check(env_ok && env_t==2500 && env_rh==5000,1);
 unsigned env_before=env_count;sample_sht();check(env_count==env_before,2);
 calibration.kVdd=1;calibration.KtPTAT=1;calibration.alphaPTAT=8;
 calibration.vPTAT25=32768;calibration.gainEE=100;calibration.alphaScale=10;
 calibration.ct[0]=-40;calibration.ct[1]=0;calibration.ct[2]=100;calibration.ct[3]=200;
 for(unsigned i=0;i<5;i++){calibration.brokenPixels[i]=0xffff;calibration.outlierPixels[i]=0xffff;}
 for(unsigned i=0;i<768;i++){calibration.alpha[i]=10240;temperatures[i]=-999;}
 calibration.calibrationModeEE=128;
 for(unsigned k=0;k<8;k++)sample_mlx();check(submask==1 && hw_seq[0]==2 && !thermal_ok,3);
 for(unsigned k=0;k<8;k++)sample_mlx();check(submask==0 && hw_seq[1]==2 && thermal_ok,4);
 for(unsigned i=0;i<768;i++)check(raw_heat[i]!=INVALID_TEMP && heat[i]==raw_heat[i],5);
 uint32_t completed=thermal_stamp;sample_mlx();check(thermal_stamp==completed,6);
 IO[16]=2;hw_seq[0]=0;sample_mlx();check(hw_seq[0]==0 && submask==0,7);
 SensorSnapshot snapshot={};sensors_snapshot(950000,&snapshot);
 check(snapshot.valid && snapshot.thermal_age_us==150000 && snapshot.environment_age_us==50000,8);
 sensors_snapshot(850000,&snapshot);check(!snapshot.valid,9);
 IO[17]=0x600d;asm volatile("ebreak");return 0;
}

void telemetry_service(void) {}

