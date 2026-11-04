#include "live_camera.h"
#include "soc.h"
#include "bsp.h"
#include "clint.h"
#include "camera_input.h"
#include "camera_preview.h"
#include "onboard_http.h"
#include "fall_state.h"
#include "visual_fall.h"
#include "yellow_timeout.h"
#include "single_person.h"
#include "iou_tracker.h"
#include "tracking_runtime.h"
#include "sensors/sensors.h"
#include "person_fusion.h"
#include "person_candidates.h"
#include "telemetry.h"
#include "care_demo.h"
#include "care_replay.h"
#include "voice.h"
#include "power_policy.h"
#include "event_report.h"
static const bool verbose_frames=false;
extern "C" void v6_profile_begin(void);
extern "C" void v6_profile_end(void);
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include <cmath>
#include <stdint.h>

static volatile uint32_t* const camera=reinterpret_cast<volatile uint32_t*>(IO_APB_SLAVE_0_INPUT);
#ifdef PERSON_LIVE_192
static const float confidence_threshold=0.60f;
#else
static const float confidence_threshold=0.40f;
#endif
static uint32_t reg_read(unsigned offset) { asm volatile("fence iorw,iorw" ::: "memory"); return camera[offset/4]; }
static void reg_write(unsigned offset,uint32_t value) { camera[offset/4]=value;asm volatile("fence iorw,iorw" ::: "memory"); }
static void update_power_display() {
  static bool dimmed=false;
  const bool requested=power_policy.mode==WORK_WATCH;
  if(requested!=dimmed){reg_write(0x3c,requested);dimmed=requested;}
}
static void wait_status(unsigned mask,unsigned desired,const char* error) {
  uint64_t start=clint_getTime(BSP_CLINT);
  while((reg_read(8)&mask)!=desired) {
    if(clint_getTime(BSP_CLINT)-start>2ULL*SYSTEM_CLINT_HZ) {
      MicroPrintf("%s\r\n",error);while(1) {}
    }
  }
}
static unsigned coordinate(float value,unsigned size) {
  if(value<=0) return 0;
  if(value>=1) return size-1;
  return static_cast<unsigned>(value*size);
}
void live_camera_run(tflite::MicroInterpreter* interpreter,TfLiteTensor* input,layer* layers,int count) {
  if(reg_read(0)!=0x56364631 || input->bytes!=CAMERA_INPUT_SIZE*CAMERA_INPUT_SIZE*3 || input->type!=kTfLiteInt8 || count!=2) {
    MicroPrintf("LIVE_INTERFACE_MISMATCH\r\n");while(1) {}
  }
  int8_t lookup[256];
  for(unsigned value=0;value<256;++value) {
    int q=static_cast<int>(roundf((value/255.0f)/input->params.scale))+input->params.zero_point;
    lookup[value]=static_cast<int8_t>(q<-128 ? -128 : q>127 ? 127 : q);
  }
  static uint8_t rgb_pixels[96*96*3];
  static uint8_t motion_map[3600];
  static FallMachine fall_machine = {};
  static VisualFallState observations[16] = {};
  static YellowTimeoutState yellow_timers[16] = {};
  static SensorSnapshot sensor_snapshot = {};
  static EventLedger ledger={};
  if(sensors_init()!=0) { MicroPrintf("SENSOR_INTERFACE_MISMATCH\r\n");while(1) {} }
  MicroPrintf("V6_SENSORS_READY: MLX90640/SHT30 on Sapphire; hardware validation pending\r\n");
  MicroPrintf("FUSION_TRIAL calibrated=%u weights=60/25/15 threshold=780; registered thermal branch retained\r\n",FUSION_CALIBRATED);
  MicroPrintf("ALARM_RULES: registered_fusion OR yellow_timeout_10s; timeout does not require thermal or stillness\r\n");
  MicroPrintf("V6_FALL_RULES_TRIAL: green=normal yellow=suspected red=rule_confirmed; not sitting/lying classification\r\n");
  MicroPrintf("V6_PERSON_LIVE_READY input=%ux%u RGB full_snapshot=%ux%u+%u ISP_WB=1.51953125/1/2 gamma=2.2 threshold_milli=%u\r\n",
    CAMERA_INPUT_SIZE,CAMERA_INPUT_SIZE,CAMERA_INPUT_SIZE,CAMERA_INPUT_ROWS,CAMERA_INPUT_TOP,
    static_cast<unsigned>(confidence_threshold*1000));
#ifdef PERSON_LIVE_192
  MicroPrintf("PERSON_CONFIDENCE: new_person=780 tracked_person=600; local overlap required; gap_ms=2800 missed_max=2\r\n");
#endif
  MicroPrintf("@V6BOOT protocol=1 device=v6-task12\r\n");
  MicroPrintf("V6_MODEL candidate=personal_single96 parameters=110236 int8_bytes=213472 weights=finetuned single_person=1 model_sha=9cca374fde1774c62\r\n");
  MicroPrintf("V6_DIAGNOSTICS detection_chain=1 post_nms=1 max_boxes=16\r\n");
  MicroPrintf("TRACKING_MODES default=single optional=multi_iou max_targets=16\r\n");
  unsigned frame=0;
  onboard_http_init();
  care_init();
  MicroPrintf("VOICE_INTERFACE %s; local audio hardware validation pending\r\n",
    voice_init(static_cast<uint32_t>(clint_getTime(BSP_CLINT)/(SYSTEM_CLINT_HZ/1000)))?"VTC2":"disabled");
  const bool network_video=reg_read(0x2c)==0x4e455431 || onboard_http_active();
  uint64_t previous_frame_start=0, capture_epoch=0;
  uint32_t previous_capture=0;
  bool alert_monitoring=false;
  unsigned capture_attempt=0,invalid_snapshots=0;
  auto& power=power_policy;
  power.init(static_cast<uint32_t>(clint_getTime(BSP_CLINT)/(SYSTEM_CLINT_HZ/1000)));
  reg_write(0x3c,0);
  static SensorReadings health={};
  static TelemetryTrack reports[16]={};
  unsigned last_count=0,last_image_frame=0,last_capture=0,last_period=0;
  MicroPrintf("POWER_POLICY auto=1 empty_ms=30000 fallback_ms=2000 camera_fps_unchanged=1\r\n");
  while(1) {
    const uint32_t now_ms=static_cast<uint32_t>(clint_getTime(BSP_CLINT)/(SYSTEM_CLINT_HZ/1000));
    if(monitor_reset_completed!=monitor_reset_requested) {
      // Apply between frames so an in-flight inference cannot restore the old alarm.
      memset(fall_machine.tracks,0,sizeof(fall_machine.tracks));
      memset(observations,0,sizeof(observations));memset(yellow_timers,0,sizeof(yellow_timers));
      ledger.reset();alert_monitoring=false;last_count=last_period=0;previous_frame_start=0;
      memset(reports,0,sizeof(reports));
      care_action("cmd=reset");sensors_monitor_alert(0);sensors_event(0,0,0,0);voice_update(0,now_ms);
      wait_status(2,0,"OVERLAY_TIMEOUT");reg_write(12,0);reg_write(0x458,0);
      tracking_publish(reports,0,last_image_frame,last_capture);
      monitor_reset_completed=monitor_reset_requested;
      onboard_http_update(last_image_frame,last_capture,now_ms,0,reports,0,ledger);
      telemetry_publish(last_image_frame,last_capture,now_ms,0,0,reports,0,ledger);
    }
    power.tick(now_ms);
    if(power.mode!=WORK_ACTIVE){
      sensors_monitor_alert(1);sensors_step();telemetry_service();onboard_http_poll();care_poll();
      if((ledger.alarm() || alert_monitoring || care_pending()) && power.mode==WORK_WATCH)power.wake(WAKE_FAULT,now_ms);
    }
    update_power_display();
    if(!power.probe_due(now_ms))continue;
    const bool multi_mode=tracking_begin_frame();
    DetectionDiagnostics diagnostic={};diagnostic.attempt=++capture_attempt;diagnostic.selected_source=-1;
    const uint64_t frame_start=clint_getTime(BSP_CLINT);
    uint32_t preview_frame=reg_read(0x18);
    reg_write(4,1);
    bool camera_timeout=false;
    while(!(reg_read(8)&1)) {
      uint32_t started_frame=reg_read(0x18);
      if(!network_video && started_frame!=preview_frame) {camera_preview_capture(started_frame);preview_frame=started_frame;}
      onboard_http_poll();
      if(clint_getTime(BSP_CLINT)-frame_start>2ULL*SYSTEM_CLINT_HZ) {
        MicroPrintf("CAMERA_TIMEOUT: resuming active monitoring\r\n");camera_timeout=true;break;
      }
    }
    const bool infrared_assist=infrared_assist_enabled;
    sensor_snapshot.valid=0;
    if(camera_timeout || !(reg_read(0x1c)&1)){
      power.wake(WAKE_FAULT,static_cast<uint32_t>(clint_getTime(BSP_CLINT)/(SYSTEM_CLINT_HZ/1000)));
      for(auto& timer:yellow_timers)timer=YellowTimeoutState{};
      diagnostic.invalid_snapshots=++invalid_snapshots;
      const uint32_t capture=reg_read(0x14),image_frame=reg_read(0x18);
      reg_write(12,0);sensors_step();
      telemetry_publish(image_frame,capture,static_cast<uint32_t>(clint_getTime(BSP_CLINT)/(SYSTEM_CLINT_HZ/1000)),0,0,
                        nullptr,0,ledger,0,0,0,nullptr,0,&diagnostic);
      telemetry_service();continue;
    }
    diagnostic.snapshot_valid=1;diagnostic.invalid_snapshots=invalid_snapshots;
    const unsigned period_ms=previous_frame_start ? static_cast<unsigned>((frame_start-previous_frame_start)/(SYSTEM_CLINT_HZ/1000)) : 0;
    if(infrared_assist)sensors_snapshot(reg_read(0x14),&sensor_snapshot);
    uint32_t motion_status=reg_read(0x20);
    bool motion_valid=period_ms>0 && period_ms<=2600 && (motion_status&3)==3 && (motion_status>>2)<2520;
    diagnostic.motion_valid=motion_valid;diagnostic.motion_cells=motion_status>>2;
    asm volatile("fence iorw,iorw" ::: "memory");
    if(motion_valid)for(unsigned word=0;word<225;word++) {
      uint32_t packed=camera[0xf900/4+word];
      for(unsigned bit=0;bit<16;bit++)motion_map[word*16+bit]=(packed>>(bit*2))&3;
    }
    sensors_readings(&health);
    unsigned valid_heat=0;for(int16_t t:health.heat)valid_heat+=t!=-32768;
    const bool healthy=health.frames && health.age_us<=2000000 && health.gap_us<=V6_THERMAL_PAIR_US && health.environment_valid && valid_heat>=64;
    power.motion(motion_status>>2,(motion_status&3)==3,now_ms);
    if(power.mode==WORK_WATCH && !healthy)power.wake(WAKE_FAULT,now_ms);
    update_power_display();
    if(!power.full_due(now_ms)){
      onboard_http_update(last_image_frame,last_capture,now_ms,last_period,reports,last_count,ledger);
      continue;
    }
    previous_frame_start=frame_start;
    uint32_t checksum=2166136261u;
    unsigned minimum=255,maximum=0;
    for(unsigned i=0;i<96*96;++i) {
      uint32_t rgb=camera[0x1000/4+i];
      const unsigned values[3]={(rgb>>16)&255,(rgb>>8)&255,rgb&255};
      for(unsigned c=0;c<3;++c) {
        const unsigned value=values[c];rgb_pixels[i*3+c]=value;
        checksum=(checksum^value)*16777619u;
        if(value<minimum) minimum=value;if(value>maximum) maximum=value;
      }
    }
    asm volatile("fence iorw,iorw" ::: "memory");
    prepare_camera_input(rgb_pixels,input->data.int8,lookup);
    diagnostic.input_hash=checksum;diagnostic.input_min=minimum;diagnostic.input_max=maximum;
    uint64_t start=clint_getTime(BSP_CLINT);
    if(frame==2)v6_profile_begin();
    if(interpreter->Invoke()!=kTfLiteOk) { MicroPrintf("LIVE_INFERENCE_FAIL\r\n");while(1) {} }
    const uint64_t network_end=clint_getTime(BSP_CLINT);
    if(frame==2)v6_profile_end();
    for(int i=0;i<count;++i) {
      TfLiteTensor* output=interpreter->output(i);
      for(unsigned j=0;j<output->bytes;++j)
        layers[i].outputs[j]=(output->data.int8[j]-output->params.zero_point)*output->params.scale;
    }
    const uint32_t capture=reg_read(0x14),image_frame=reg_read(0x18);
    if(capture<previous_capture)capture_epoch+=1ULL<<32;
    previous_capture=capture;
    const uint64_t capture_tick=(capture_epoch+capture)*(SYSTEM_CLINT_HZ/1000000);
    int boxes_count=0;
    box* boxes=perform_inference(layers,count,&boxes_count,CAMERA_INPUT_SIZE,CAMERA_INPUT_SIZE,confidence_threshold,0.30f);
    if(!boxes && boxes_count>0) { MicroPrintf("LIVE_BOX_ALLOC_FAIL\r\n");while(1) {} }
    diagnostic_decode(diagnostic,boxes,boxes_count);
#ifdef PERSON_LIVE_192
    select_person_candidates(boxes,boxes_count,fall_machine,capture_tick,SYSTEM_CLINT_HZ,nullptr);
#else
    select_person_candidates(boxes,boxes_count,fall_machine,capture_tick,SYSTEM_CLINT_HZ,nullptr,confidence_threshold,confidence_threshold,.65f);
#endif
    for(int i=0;i<boxes_count;i++) {
      const bool accepted=boxes[i].class_probabilities[0]>0;
      diagnostic.candidates+=accepted;
      if(i<16)diagnostic.boxes[i].accepted=accepted;
    }
    wait_status(2,0,"OVERLAY_TIMEOUT");
    unsigned detected=0;
    box frame_detections[16] = {};
    uint32_t box_x[16],box_y[16];
    unsigned detection_sources[16];
    for(int i=0;i<boxes_count && detected<16;++i) {
      const box& b=boxes[i];
      if(!(b.class_probabilities[0]>0)) continue;
      if(multi_mode && (!std::isfinite(b.objectness)||b.objectness<.40f||b.class_probabilities[0]<.40f))continue;
      if(!std::isfinite(b.x_min)||!std::isfinite(b.y_min)||!std::isfinite(b.x_max)||!std::isfinite(b.y_max)) continue;
      unsigned x0=coordinate(b.x_min,1920),x1=coordinate(b.x_max,1920);
      unsigned y0=coordinate(camera_y(b.y_min),1080),y1=coordinate(camera_y(b.y_max),1080);
      if(x0>=x1 || y0>=y1) continue;
      detection_sources[detected]=i;
      if(i<16)diagnostic.boxes[i].mapped=detected;
      box& detection=frame_detections[detected++];
      detection=b;
      detection.x_min=x0/1920.0f;detection.x_max=x1/1920.0f;
      detection.y_min=y0/1080.0f;detection.y_max=y1/1080.0f;
      box_x[detected-1]=x0|(x1<<16);box_y[detected-1]=y0|(y1<<16);
      if(verbose_frames)MicroPrintf("PERSON_BOX frame=%u x0=%u y0=%u x1=%u y1=%u confidence_milli=%u\r\n",frame,x0,y0,x1,y1,static_cast<unsigned>(b.class_probabilities[0]*1000));
    }
    const int selected=multi_mode ? -1 : single_person_candidate(frame_detections,detected,fall_machine,capture_tick,SYSTEM_CLINT_HZ);
    diagnostic.mapped=detected;
    if(selected>=0)diagnostic.selected_source=detection_sources[selected];
    if(!multi_mode && selected<0)detected=0;
    else if(!multi_mode) {
      frame_detections[0]=frame_detections[selected];
      box_x[0]=box_x[selected];box_y[0]=box_y[selected];detected=1;
    }
    float motion[16]={},recent[16]={};
    for(unsigned i=0;i<detected;i++)motion[i]=recent[i]=-1;
    for(unsigned i=0;i<detected && motion_valid;i++) {
      unsigned cells=0,total_cells=0,changed=0,changed_recent=0;
      const box& d=frame_detections[i];
      for(unsigned row=0;row<45;row++)for(unsigned col=0;col<80;col++) {
        float x=col/80.0f,y=row/45.0f;
        if(x>=d.x_min && x<=d.x_max && y>=d.y_min && y<=d.y_max) {
          total_cells++;bool shared=false;
          for(unsigned j=0;j<detected;j++)if(j!=i && x>=frame_detections[j].x_min && x<=frame_detections[j].x_max &&
              y>=frame_detections[j].y_min && y<=frame_detections[j].y_max)shared=true;
          if(shared)continue;
          cells++;changed+=motion_map[row*80+col]&1;changed_recent+=(motion_map[row*80+col]>>1)&1;
        }
      }
      if(cells>=4 && cells*2>=total_cells){motion[i]=(float)changed/cells;recent[i]=(float)changed_recent/cells;}
    }
    int detection_tracks[16];
    if(multi_mode) {
      int associations[16];
      iou_associate(fall_machine,frame_detections,detected,capture_tick,SYSTEM_CLINT_HZ,associations);
      fall_update(fall_machine,frame_detections,detected,capture_tick,SYSTEM_CLINT_HZ,detection_tracks,motion,motion_valid,recent,associations);
    } else fall_update(fall_machine,frame_detections,detected,capture_tick,SYSTEM_CLINT_HZ,detection_tracks,motion,motion_valid,recent);
    for(unsigned i=0;i<detected;i++)if(detection_tracks[i]>=0)diagnostic.tracked++;
    for(unsigned j=0;j<16;j++)if(fall_machine.tracks[j].missed) {
      observations[j].missing=true;observations[j].recovery_tick=0;
      if(capture_tick-observations[j].last_tick>SYSTEM_CLINT_HZ*26ULL/10) {
        observations[j].count=0;observations[j].still_ms=0;
      }
    }
    for(unsigned i=0;i<detected;i++)if(detection_tracks[i]>=0) {
      const int j=detection_tracks[i];
      visual_fall_observe(fall_machine.tracks[j],observations[j],capture_tick,
        motion_valid?(observations[j].state==NORMAL?motion[i]:recent[i]):-1,SYSTEM_CLINT_HZ);
    }
    alert_monitoring=false;
    for(const auto& track:fall_machine.tracks)if(track.active && track.state!=NORMAL)alert_monitoring=true;
    sensors_monitor_alert(alert_monitoring || (infrared_assist && FUSION_CALIBRATED));
    unsigned event_state=0,event_confidence=0,event_weighted=0;
    unsigned displayed=0;
    memset(reports,0,sizeof(reports));
    // Tracking may reject a box; preserve the detector's count and pixel coordinates.
    for(unsigned i=0;i<detected;++i) {
      const FallTrack* track=detection_tracks[i]>=0 ? &fall_machine.tracks[detection_tracks[i]] : nullptr;
      const FallState state=track ? track->state : NORMAL;
      const box& d=frame_detections[i];
      const FusionEvidence evidence=infrared_assist ? fusion_evidence(d,sensor_snapshot) : FusionEvidence{};
      const bool person_confirmed=fusion_person_confirmed(infrared_assist,evidence);
      const bool fusion_confirmed=fusion_alarm(track && state==FALLEN && fall_still(*track,capture_tick,SYSTEM_CLINT_HZ),
                                               evidence,track?track->visual_score:0);
      const bool timeout_confirmed=track && yellow_timeout_alarm(yellow_timers[detection_tracks[i]],track->id,
          state==FALLING || state==FALLEN,capture_tick,SYSTEM_CLINT_HZ);
      const bool alarm=fusion_confirmed || timeout_confirmed;

      unsigned confidence=static_cast<unsigned>((evidence.valid?fusion_score(track?track->visual_score:0,evidence):(track?track->visual_score:0))*100);
      if((unsigned)(alarm?2:state==FALLING || state==FALLEN?1:0)>event_state) {
        event_state=alarm?2:1;event_confidence=confidence;event_weighted=alarm?fusion_confirmed:evidence.valid;
      }
      const FallState display_state=alarm ? FALLEN : state==FALLEN ? FALLING : state;
      const unsigned visible_state=static_cast<unsigned>(display_state);
      uint32_t display_x=box_x[i],display_y=box_y[i];
      // Smooth normal display only; action and thermal evidence always use the raw box.
      if(track && state==NORMAL) {
        display_x=coordinate(track->display[0],1920)|(coordinate(track->display[2],1920)<<16);
        display_y=coordinate(track->display[1],1080)|(coordinate(track->display[3],1080)<<16);
      }
      reports[i]={track?track->id:0,visible_state,static_cast<unsigned>(d.class_probabilities[0]*1000),
                  static_cast<unsigned>((track?track->visual_score:0)*1000),confidence*10,evidence.valid,
                  box_x[i]&0xffff,box_y[i]&0xffff,box_x[i]>>16,box_y[i]>>16,evidence.reason,
                  display_x&0xffff,display_y&0xffff,display_x>>16,display_y>>16,!person_confirmed};
      if(track && ledger.observe(track->id,visible_state,static_cast<uint32_t>(capture_tick/(SYSTEM_CLINT_HZ/1000)),image_frame)) {
        telemetry_capture(ledger.events[(ledger.sequence-1)%16],reports[i],rgb_pixels);
        event_report_capture(ledger.events[(ledger.sequence-1)%16],rgb_pixels);
      }
      // Keep RGB tracking and fall alerts even when infrared cannot confirm a person.
      if(person_confirmed || display_state!=NORMAL) {
        reg_write(0x100+displayed*8,display_x|(static_cast<uint32_t>(display_state)<<30));
        reg_write(0x104+displayed*8,display_y);displayed++;
      }
      if(verbose_frames)MicroPrintf("FUSION_PERSON frame=%u id=%u mode=%s score_milli=%u thermal_milli=%u alarm=%u x0=%u y0=%u x1=%u y1=%u\r\n",
        frame,track ? track->id : 0,evidence.valid ? "WEIGHTED" : "VISUAL_ONLY",
        static_cast<unsigned>((evidence.valid ? fusion_score(track?track->visual_score:0,evidence) : (track?track->visual_score:0))*1000),
        static_cast<unsigned>(evidence.thermal*1000),alarm,
        box_x[i]&0xffff,box_y[i]&0xffff,box_x[i]>>16,box_y[i]>>16);
      if(verbose_frames)MicroPrintf("FALL_TRACK frame=%u id=%u state=%s rule_alarm=%u ratio_milli=%u cy_milli=%u history=%u\r\n",frame,track ? track->id : 0,
        state==FALLEN ? "RULE_CONFIRMED" : state==FALLING ? "SUSPECTED" : "NORMAL",state==FALLEN,
        static_cast<unsigned>((d.x_max-d.x_min)*(1920.0f/1080.0f)/(d.y_max-d.y_min)*1000),
        static_cast<unsigned>((d.y_min+d.y_max)*500),track ? track->sample_count : 0);
    }
    unsigned observation_lost=ledger.alarm() && event_state<2;
    if(observation_lost){event_state=2;event_confidence=0;}
    reg_write(0x458,(ledger.alarm()?1:0)|(event_state<<8)|(event_confidence<<16));
    sensors_event(event_state,event_confidence,event_weighted,observation_lost);
    if(verbose_frames)MicroPrintf("MOTION frame=%u valid=%u global_cells=%u event=%u score_percent=%u weighted=%u\r\n",
      frame,motion_valid,motion_status>>2,event_state,event_confidence,event_weighted);
    reg_write(12,displayed);
    const uint64_t overlay_end=clint_getTime(BSP_CLINT);
    diagnostic.displayed=displayed;
    infrared_assist_active=infrared_assist;
    release_boxes(boxes,boxes_count);
    tracking_publish(reports,detected,image_frame,capture);
    care_update(reports,detected,capture);
    bool replay_fall=false;for(unsigned i=0;i<detected;i++)if(reports[i].state==2)replay_fall=true;
    replay_record(rgb_pixels,image_frame,capture,replay_fall);
    // Publish person boxes before the shared CPU reads the slower sensors.
    sensors_step();
    voice_update(event_state,static_cast<uint32_t>(clint_getTime(BSP_CLINT)/(SYSTEM_CLINT_HZ/1000)));
    const uint64_t sensor_end=clint_getTime(BSP_CLINT);
    sensors_readings(&health);
    valid_heat=0;for(int16_t t:health.heat)valid_heat+=t!=-32768;
    const bool entry_healthy=health.frames && health.age_us<=2000000 && health.gap_us<=V6_THERMAL_PAIR_US && health.environment_valid && valid_heat>=64;
    power.detected(static_cast<uint32_t>(sensor_end/(SYSTEM_CLINT_HZ/1000)),now_ms,diagnostic.candidates,
      ledger.alarm() || alert_monitoring || care_pending(),entry_healthy,static_cast<unsigned>((network_end-start)/(SYSTEM_CLINT_HZ/1000000)));
    last_count=detected;last_image_frame=image_frame;last_capture=capture;last_period=period_ms;
    onboard_http_update(image_frame,capture,static_cast<uint32_t>(sensor_end/(SYSTEM_CLINT_HZ/1000)),period_ms,reports,detected,ledger);
    telemetry_publish(image_frame,capture,static_cast<uint32_t>(sensor_end/(SYSTEM_CLINT_HZ/1000)),period_ms,
                      static_cast<unsigned>((network_end-start)/(SYSTEM_CLINT_HZ/1000)),reports,detected,ledger,static_cast<unsigned>((start-frame_start)/(SYSTEM_CLINT_HZ/1000)),
                      static_cast<unsigned>((overlay_end-network_end)/(SYSTEM_CLINT_HZ/1000)),static_cast<unsigned>((sensor_end-overlay_end)/(SYSTEM_CLINT_HZ/1000)),network_video?nullptr:rgb_pixels,
                      static_cast<unsigned>((overlay_end-capture_tick)/(SYSTEM_CLINT_HZ/1000)),&diagnostic);
    if(verbose_frames)MicroPrintf("LIVE_FRAME frame=%u rgb_hash=%x min=%u max=%u boxes=%u capture_prepare_ms=%u network_ms=%u detect_overlay_ms=%u sensors_ms=%u period_ms=%u\r\n",
      frame,checksum,minimum,maximum,detected,
      static_cast<unsigned>((start-frame_start)/(SYSTEM_CLINT_HZ/1000)),
      static_cast<unsigned>((network_end-start)/(SYSTEM_CLINT_HZ/1000)),
      static_cast<unsigned>((overlay_end-network_end)/(SYSTEM_CLINT_HZ/1000)),
      static_cast<unsigned>((sensor_end-overlay_end)/(SYSTEM_CLINT_HZ/1000)),period_ms);
    frame++;
    onboard_http_poll();
  }
}
