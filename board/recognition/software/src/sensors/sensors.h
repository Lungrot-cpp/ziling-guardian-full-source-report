#ifndef V6_SENSORS_H
#define V6_SENSORS_H
#include <stdint.h>
#ifndef V6_MLX_REFRESH_CODE
#define V6_MLX_REFRESH_CODE 3
#endif
#define V6_THERMAL_PAIR_US (V6_MLX_REFRESH_CODE>=4 ? 250000u : 500000u)
#define V6_THERMAL_AGE_US (V6_MLX_REFRESH_CODE>=4 ? 500000u : 750000u)
typedef struct {
    int16_t heat[768], ambient_centi, humidity_centi;
    uint32_t thermal_age_us, environment_age_us;
    unsigned valid;
} SensorSnapshot;
typedef struct {
    int16_t heat[768], ambient_centi, humidity_centi;
    uint32_t oldest_us,newest_us,age_us,gap_us,environment_age_us;
    uint32_t frames,overwritten,torn,pair_drops,mlx_errors,sht_errors,capture_frames;
    unsigned thermal_valid,environment_valid;
} SensorReadings;
#ifdef __cplusplus
extern "C" {
#endif
int sensors_init(void);
void sensors_step(void);
void sensors_monitor_alert(unsigned active);
void sensors_snapshot(uint32_t video_stamp, SensorSnapshot *out);
void sensors_readings(SensorReadings *out);
#ifdef __cplusplus
}
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif
void sensors_event(unsigned state,unsigned confidence,unsigned weighted,unsigned lost);
#ifdef __cplusplus
}
#endif
