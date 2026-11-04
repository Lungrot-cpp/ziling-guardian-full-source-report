// Included by main.cc only in the separate REPLAY=yes build.
#include "model/fall_state.h"
#include "replay_inputs.h"

struct ReplayTrack {
   unsigned id, state, missed;
   float coordinates[4];
};
unsigned replay_completed, replay_ok, replay_heap_before, replay_heap_after;
unsigned replay_counts[REPLAY_FRAMES];
uint32_t replay_us[REPLAY_FRAMES][6]; // copy, network, dequantize, YOLO, tracking, total.
float replay_boxes[REPLAY_FRAMES][135][6];
ReplayTrack replay_tracks[REPLAY_FRAMES][16];
extern "C" __attribute__((noinline)) void replay_done() { asm volatile("" ::: "memory"); }

static void video_replay(layer* layers) {
   static FallMachine machine;
   unsigned previous_clip = ~0u;
   replay_heap_before = mallinfo().uordblks;
   MicroPrintf("SOURCE=RECORDED_VIDEO; timestamp=source_ms; physical_alarm=none\r\n");
   for (unsigned frame = 0; frame < REPLAY_FRAMES; ++frame) {
      const unsigned clip = replay_clip[frame], timestamp = replay_timestamp_ms[frame];
      if (clip != previous_clip) {
         std::memset(&machine, 0, sizeof(machine));
         previous_clip = clip;
         MicroPrintf("CLIP=%s\r\n", replay_names[clip]);
      }
      uint64_t time[6];
      time[0] = clint_getTime(BSP_CLINT);
      std::memcpy(model_input->data.int8, replay_frames[frame], model_input->bytes);
      time[1] = clint_getTime(BSP_CLINT);
      if (interpreter->Invoke() != kTfLiteOk) { MicroPrintf("REPLAY_INVOKE_FAIL frame=%u\r\n", frame); break; }
      time[2] = clint_getTime(BSP_CLINT);
      for (int head = 0; head < total_output_layers; ++head) {
         TfLiteTensor* output = interpreter->output(head);
         for (unsigned j = 0; j < output->bytes; ++j)
            layers[head].outputs[j] = (output->data.int8[j] - output->params.zero_point) * output->params.scale;
      }
      time[3] = clint_getTime(BSP_CLINT);
      int count = 0;
      box* boxes = perform_inference(layers, total_output_layers, &count, 96, 96, OBJECTNESS_THRESHOLD, IOU_THRESHOLD);
      time[4] = clint_getTime(BSP_CLINT);
      if (count < 0 || count > 135 || (!boxes && count)) { MicroPrintf("REPLAY_BOX_FAIL frame=%u\r\n", frame); break; }
      bool valid = true;
      for (int i = 0; i < count; ++i) {
         const float values[] = {boxes[i].x_min, boxes[i].y_min, boxes[i].x_max, boxes[i].y_max,
                                 boxes[i].objectness, boxes[i].class_probabilities[0]};
         for (int j = 0; j < 6; ++j) {
            if (!isfinite(values[j])) valid = false;
            replay_boxes[frame][i][j] = values[j];
         }
      }
      if (!valid) { release_boxes(boxes, count); MicroPrintf("REPLAY_NONFINITE frame=%u\r\n", frame); break; }
      // YOLO may extend beyond the image; preserve raw records, clip only tracker input.
      for (int i = 0; i < count; ++i) {
         boxes[i].x_min = fall_max(0.0f, fall_min(1.0f, boxes[i].x_min));
         boxes[i].y_min = fall_max(0.0f, fall_min(1.0f, boxes[i].y_min));
         boxes[i].x_max = fall_max(0.0f, fall_min(1.0f, boxes[i].x_max));
         boxes[i].y_max = fall_max(0.0f, fall_min(1.0f, boxes[i].y_max));
      }
      unsigned old_id[16], old_state[16];
      for (int i = 0; i < 16; ++i) { old_id[i] = machine.tracks[i].id; old_state[i] = machine.tracks[i].state; }
      fall_update(machine, boxes, count, timestamp, 1000);
      time[5] = clint_getTime(BSP_CLINT);
      for (int stage = 0; stage < 5; ++stage)
         replay_us[frame][stage] = (time[stage+1] - time[stage]) / (SYSTEM_CLINT_HZ / 1000000);
      replay_us[frame][5] = (time[5] - time[0]) / (SYSTEM_CLINT_HZ / 1000000);
      replay_counts[frame] = count;
      unsigned fresh = 0, alarms = 0;
      for (int i = 0; i < 16; ++i) {
         const FallTrack& track = machine.tracks[i];
         if (!track.active) continue;
         ReplayTrack& record = replay_tracks[frame][i];
         record.id = track.id; record.state = track.state; record.missed = track.missed;
         std::memcpy(record.coordinates, track.box, sizeof(record.coordinates));
         if (!track.missed) ++fresh;
         if (track.state == FALLEN) ++alarms;
         if (track.id != old_id[i] || track.state != old_state[i])
            MicroPrintf("STATE frame=%u t_ms=%u id=%u state=%u alarm=%u\r\n", frame, timestamp,
                        track.id, (unsigned)track.state, track.state == FALLEN);
      }
      release_boxes(boxes, count);
      replay_completed = frame + 1;
      MicroPrintf("REPLAY frame=%u t_ms=%u boxes=%u fresh_tracks=%u alarms=%u process_us=%u\r\n",
                  frame, timestamp, replay_counts[frame], fresh, alarms, replay_us[frame][5]);
   }
   replay_heap_after = mallinfo().uordblks;
   replay_ok = replay_completed == REPLAY_FRAMES;
   MicroPrintf("REPLAY_COMPLETE=%u/%u EXECUTION_CHECK=%s heap_before=%u after=%u\r\n", replay_completed,
               REPLAY_FRAMES, replay_ok ? "PASS" : "FAIL", replay_heap_before, replay_heap_after);
   replay_done();
}
