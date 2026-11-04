///////////////////////////////////////////////////////////////////////////////////
// Copyright 2024 Efinix.Inc. All Rights Reserved.
// You may obtain a copy of the license at
//    https://www.efinixinc.com/software-license.html
///////////////////////////////////////////////////////////////////////////////////

#include <stdlib.h>
#include <malloc.h>
#include <cstring>
#include <stdint.h>
#include "riscv.h"
#include "soc.h"
#include "bsp.h"
#include <math.h>
#include "clint.h"
#include "print.h"
//Tinyml Header File
#include "intc.h"
#include "tinyml.h"
#include "ops/ops_api.h"

//Import TensorFlow lite libraries
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
//#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/testing/micro_test.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/debug_log.h"
#include "tensorflow/lite/micro/micro_time.h"

//Model data
#include "model/yolo_person_detect_model_data.h"

//Static test image data
#include "model/input_img_000000018380_96x96x3.h"
#include "model/benchmark_images.h"
#include "model/person_reference.h"

//Yolo layer
#include "model/yolo.h"
#include "live_camera.h"
bool check_nms();
volatile int result_check_passed = 0;
volatile unsigned measured_network_ms = 0, measured_yolo_ms = 0, measured_total_ms = 0;
#ifdef PERSON_DRIVER_PROFILE
unsigned depthwise_profile_count;
uint32_t depthwise_profile[14][8]; // driver us, status, input W/H/C, output W/H, filter W.
#endif
#ifdef PERSON_FPGA_ACCEL
volatile unsigned benchmark_iterations = 100;
#else
volatile unsigned benchmark_iterations = 1;
#endif
// GDB exports these buffers for exact CPU/FPGA and before/after comparisons.
uint32_t benchmark_us[3][100][5]; // copy, network, dequantize, YOLO, total (no UART).
int8_t benchmark_raw[3][810];
float benchmark_boxes[3][135][6];
unsigned benchmark_counts[3], benchmark_completed[3];
int benchmark_passed[3];
unsigned benchmark_heap_before, benchmark_heap_after;
extern "C" __attribute__((noinline)) void benchmark_done() { asm volatile("" ::: "memory"); }

#define SCALE 1
#define CLASSES 1
#define TOTAL_ANCHORS 3
#define OBJECTNESS_THRESHOLD 0.25
#define IOU_THRESHOLD 0.5
namespace {
   tflite::ErrorReporter* error_reporter = nullptr;
   const tflite::Model* model = nullptr;
   tflite::MicroInterpreter* interpreter = nullptr;
   TfLiteTensor* model_input = nullptr;
   
   //Create an area of memory to use for input, output, and other TensorFlow
   //arrays. You'll need to adjust this by combiling, running, and looking
   //for errors.
   constexpr int kTensorArenaSize = 10000 * 1024;
   uint8_t tensor_arena[kTensorArenaSize];
   int total_output_layers = 0;
   float anchors[2][TOTAL_ANCHORS * 2] = {{115, 73, 119, 199, 242, 238}, {12, 18, 37, 49, 52, 132}};
   uint8_t yolo_cache[16*1024];
}


static void print_box_float(double value) {
   int scaled = (int)(value * 1000.0);
   if (scaled < 0) { MicroPrintf("-"); scaled = -scaled; }
   MicroPrintf("%d.%d%d%d", scaled / 1000, scaled / 100 % 10, scaled / 10 % 10, scaled % 10);
}

void tinyml_init() {
   //Set up logging
   static tflite::MicroErrorReporter micro_error_reporter;
   error_reporter = &micro_error_reporter;
   
   //Map the model into a usable data structure
   model = tflite::GetModel(yolo_person_detect_model_data);
   
   if (model->version() != TFLITE_SCHEMA_VERSION) {
      MicroPrintf("Model version does not match Schema\n\r");
      while(1);
   }
   
   //AllOpsResolver may be used for generalization
   static tflite::AllOpsResolver resolver;
   tflite::MicroOpResolver* op_resolver = nullptr;
   op_resolver = &resolver;
   
   //Build an interpreter to run the model
   
   static tflite::MicroInterpreter static_interpreter(
      model, *op_resolver, tensor_arena, kTensorArenaSize,
//      error_reporter, nullptr); //Without profiler
      error_reporter, nullptr); // Keep UART logging outside the timed inference.
   interpreter = &static_interpreter;
   
   //Allocate memory from the tensor_arena for the model's tensors
   TfLiteStatus allocate_status = interpreter->AllocateTensors();
   if (allocate_status != kTfLiteOk) {
      MicroPrintf("AllocateTensors() failed\n\r");
      while(1);
   }
   
   //Assign model input buffer (tensor) to pointer
   model_input = interpreter->input(0);
   
   //Print loaded model input and output shape
   total_output_layers = interpreter->outputs_size();
   MicroPrintf("\n\rTotal output layers: %d\n\r", total_output_layers);
   MicroPrintf(
      "Input shape: %d %d %d %d %d, type: %d\n\r", model_input->dims->size, model_input->dims->data[0], model_input->dims->data[1], model_input->dims->data[2], model_input->dims->data[3], model_input->type
   );
   
   for (int i = 0; i < total_output_layers; ++i)
      MicroPrintf(
         "Output shape %d: %d %d %d %d %d, type: %d\n\r", i, interpreter->output(i)->dims->size, interpreter->output(i)->dims->data[0], 
         interpreter->output(i)->dims->data[1], interpreter->output(i)->dims->data[2], interpreter->output(i)->dims->data[3], interpreter->output(i)->type
      );
}

void release_boxes(box* boxes, int count) {
   for (int i = 0; i < count; ++i) free(boxes[i].class_probabilities);
   free(boxes);
}

static int compare_time(const void* left, const void* right) {
   const uint32_t a = *(const uint32_t*)left, b = *(const uint32_t*)right;
   return (a > b) - (a < b);
}

static void benchmark(layer* layers) {
   const int8_t* images[] = {input_img_000000018380_96x96x3, benchmark_image_0, benchmark_image_1};
   const unsigned runs = benchmark_iterations;
   if (runs < 1 || runs > 100) { MicroPrintf("BENCHMARK_INVALID_RUNS\r\n"); benchmark_done(); return; }
   for (int image = 0; image < 3; ++image) {
      bool passed = true;
      // Three warmups precede the timed runs; validation and UART are outside timing.
      for (unsigned iteration = 0; iteration < runs + 3; ++iteration) {
         uint64_t time[5];
         time[0] = clint_getTime(BSP_CLINT);
         std::memcpy(model_input->data.int8, images[image], model_input->bytes);
         time[1] = clint_getTime(BSP_CLINT);
         if (interpreter->Invoke() != kTfLiteOk) { passed = false; break; }
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
         if (count < 0 || count > 135 || (!boxes && count)) { passed = false; break; }
         if (iteration >= 3) {
            const unsigned run = iteration - 3;
            for (int stage = 0; stage < 4; ++stage)
               benchmark_us[image][run][stage] = (time[stage+1] - time[stage]) / (SYSTEM_CLINT_HZ / 1000000);
            benchmark_us[image][run][4] = (time[4] - time[0]) / (SYSTEM_CLINT_HZ / 1000000);
            unsigned offset = 0;
            for (int head = 0; head < total_output_layers; ++head) {
               TfLiteTensor* output = interpreter->output(head);
               if (run == 0) std::memcpy(benchmark_raw[image] + offset, output->data.int8, output->bytes);
               else if (std::memcmp(benchmark_raw[image] + offset, output->data.int8, output->bytes)) passed = false;
               offset += output->bytes;
            }
            if (run == 0) benchmark_counts[image] = count;
            else if (benchmark_counts[image] != (unsigned)count) passed = false;
            for (int i = 0; i < count; ++i) {
               const float values[] = {boxes[i].x_min, boxes[i].y_min, boxes[i].x_max, boxes[i].y_max,
                                       boxes[i].objectness, boxes[i].class_probabilities[0]};
               for (int j = 0; j < 6; ++j) {
                  if (!isfinite(values[j])) passed = false;
                  if (run == 0) benchmark_boxes[image][i][j] = values[j];
                  else if (fabsf(values[j] - benchmark_boxes[image][i][j]) > 0.002f) passed = false;
               }
            }
            benchmark_completed[image] = run + 1;
         }
         release_boxes(boxes, count);
         // Measure after every allocation has been freed and allocator warmup has completed.
         if (image == 0 && iteration == 2) benchmark_heap_before = mallinfo().uordblks;
      }
      benchmark_passed[image] = passed && benchmark_completed[image] == runs;
      MicroPrintf("BENCHMARK image=%d runs=%u boxes=%u REPEAT_CHECK=%s\r\n", image,
                  benchmark_completed[image], benchmark_counts[image], benchmark_passed[image] ? "PASS" : "FAIL");
      if (benchmark_completed[image] == runs) {
         const char* stages[] = {"copy", "network", "dequantize", "yolo", "total"};
         uint32_t samples[100];
         for (int stage = 0; stage < 5; ++stage) {
            for (unsigned run = 0; run < runs; ++run) samples[run] = benchmark_us[image][run][stage];
            qsort(samples, runs, sizeof(samples[0]), compare_time);
            MicroPrintf("TIMING_US stage=%s min=%u median=%u p95=%u max=%u\r\n", stages[stage], samples[0],
                        (samples[(runs-1)/2] + samples[runs/2])/2, samples[(runs*95+99)/100-1], samples[runs-1]);
         }
      }
   }
   benchmark_heap_after = mallinfo().uordblks;
   MicroPrintf("BENCHMARK heap_used_before=%u after=%u (bytes)\r\n", benchmark_heap_before, benchmark_heap_after);
   benchmark_done();
}

#ifdef PERSON_VIDEO_REPLAY
#include "video_replay.h"
#endif

extern "C" int main() {

   bsp_init();
   csr_write(mtvec, trap_entry);
#ifdef PERSON_FPGA_ACCEL
   MicroPrintf("--Person Inference Demo: STATIC IMAGE, FPGA ACCELERATOR--\r\n");
#ifdef PERSON_VIDEO_REPLAY
   MicroPrintf("VERSION=FPGA_VIDEO_REPLAY_CLIP_V2; original fall_state.h; recorded inputs\r\n");
#else
   MicroPrintf("VERSION=FPGA_BENCHMARK_V1; clock=100MHz; requires person_accel.bit\r\n");
#endif
#else
   MicroPrintf("--Person Inference Demo: STATIC IMAGE, CPU ONLY--\r\n");
   MicroPrintf("VERSION=CPU_FAST_1X1_PAD_ROWS\r\n");
#endif
   const bool nms_matches = check_nms();
   MicroPrintf("NMS_CHECK=%s (32 cases vs vendor implementation)\r\n", nms_matches ? "PASS" : "FAIL");
   if (!nms_matches) { while (1) {} }
   
   MicroPrintf("TinyML Setup...");
   tinyml_init();
   MicroPrintf("Done\n\n\r");
   
   TfLiteStatus invoke_status;
   uint64_t timerCmp0, timerCmp1, timerDiff_0_1;
   uint64_t timerCmp2, timerCmp3, timerDiff_2_3;
   uint64_t timerCmpTotal0, timerCmpTotal1, timerDiffTotal;
   u32 ms;
   
	//Interrupt Initialization
	#ifdef PERSON_FPGA_ACCEL
   IntcInitialize();
#endif
   
   /*************************************************************RUN INFERENCE*************************************************************/

   //Test image data
   MicroPrintf("Yolo Pico Person Inference...");
   timerCmpTotal0 = clint_getTime(BSP_CLINT);
   if (model_input->type != kTfLiteInt8 || model_input->bytes != input_img_000000018380_96x96x3_len) {
      MicroPrintf("INPUT_MISMATCH\r\n"); while (1) {}
   }
   //Copy test image to tflite model input.
   std::memcpy(model_input->data.int8, input_img_000000018380_96x96x3,
               input_img_000000018380_96x96x3_len);  //Pre-normalized to [-128,127]

   //Perform inference
   timerCmp0 = clint_getTime(BSP_CLINT);
   invoke_status = interpreter->Invoke();
   timerCmp1 = clint_getTime(BSP_CLINT);

   if (invoke_status != kTfLiteOk) {
     MicroPrintf("INFERENCE_FAIL\r\n");
     while (1) {}
   }
   MicroPrintf("Done\n\r");

   //Yolo layer
   MicroPrintf("Pass data to Yolo layer...");

   layer* yolo_layers = (layer*)calloc(total_output_layers, sizeof(layer));
   //layer* yolo_layers = (layer*)malloc(total_output_layers * sizeof(layer));
   if (!yolo_layers || total_output_layers != 2) { MicroPrintf("OUTPUT_ALLOC_FAIL\r\n"); while (1) {} }
   for (int i = 0; i < total_output_layers; ++i) {
      yolo_layers[i].channels = interpreter->output(i)->dims->data[0];
      yolo_layers[i].height = interpreter->output(i)->dims->data[1];
      yolo_layers[i].width = interpreter->output(i)->dims->data[2];
      yolo_layers[i].classes = CLASSES;
      yolo_layers[i].boxes_per_scale = interpreter->output(i)->dims->data[3] / (5 + yolo_layers[i].classes);
      yolo_layers[i].total_anchors = TOTAL_ANCHORS;
      yolo_layers[i].scale = SCALE;
      const int grid=yolo_layers[i].width;
      if(grid!=3 && grid!=6) { MicroPrintf("OUTPUT_GRID_MISMATCH\r\n"); while(1) {} }
      yolo_layers[i].anchors = anchors[grid==3 ? 0 : 1];
       
      int total = (
         interpreter->output(i)->dims->data[0] * interpreter->output(i)->dims->data[1] * interpreter->output(i)->dims->data[2] * interpreter->output(i)->dims->data[3]
      );
      

      yolo_layers[i].outputs = (float*)calloc(total, sizeof(float));
      if (!yolo_layers[i].outputs) { MicroPrintf("OUTPUT_ALLOC_FAIL\r\n"); while (1) {} }

      TfLiteAffineQuantization params = *(static_cast<TfLiteAffineQuantization *>(interpreter->output(i)->quantization.params));
      
      MicroPrintf("\n\rOutput %d Scale: ", i);
      print_box_float(params.scale->data[0]);
      MicroPrintf("\n\rOutput %d Zero point: ", i);
      print_box_float(params.zero_point->data[0]);
      MicroPrintf("\n\r");
      
      for (int j = 0; j < total; ++j)
         yolo_layers[i].outputs[j] = ((float)interpreter->output(i)->data.int8[j] - params.zero_point->data[0]) * params.scale->data[0];
   }
   
   MicroPrintf("Done\n\r");
    
   int total_boxes = 0;
   

   MicroPrintf("Yolo layer inference...");
   timerCmp2 = clint_getTime(BSP_CLINT);
   box* boxes = perform_inference(yolo_layers, total_output_layers, &total_boxes, model_input->dims->data[1], model_input->dims->data[2], OBJECTNESS_THRESHOLD, IOU_THRESHOLD);
   if (!boxes && total_boxes > 0) { MicroPrintf("BOX_ALLOC_FAIL\r\n"); while (1) {} }
   timerCmp3 = clint_getTime(BSP_CLINT);
   MicroPrintf("Done\n\n\r");
   
   timerCmpTotal1 = clint_getTime(BSP_CLINT);
   MicroPrintf("\n\rBoxes:\n\r");
   
   for (int i = 0; i < total_boxes; ++i) {
      print_box_float(boxes[i].x_min);
      MicroPrintf(", ");
      print_box_float(boxes[i].y_min);
      MicroPrintf(", ");
      print_box_float(boxes[i].x_max);
      MicroPrintf(", ");
      print_box_float(boxes[i].y_max);
      MicroPrintf(", ");
      print_box_float(boxes[i].objectness);
      MicroPrintf(", ");
      
      for (int c = 0; c < CLASSES; ++c) {
         print_box_float(boxes[i].class_probabilities[c]);
         if (c < CLASSES - 1)
            MicroPrintf(", ");
         else
            MicroPrintf("\n\r");
      }
   }

   MicroPrintf("Total_boxes = %d\n\n\r", total_boxes);
   
   timerDiff_0_1 = timerCmp1 - timerCmp0;
   u32 *v = (u32 *)&timerDiff_0_1;
   MicroPrintf("Inference clock cycle (hex): %x, %x\n\r", v[1], v[0]); //Timestamp
   //processing_time (second) = timestamp_clock_cycle/SYSTEM_CLINT_HZ
   MicroPrintf("SYSTEM_CLINT_HZ (hex): %x\n\r", SYSTEM_CLINT_HZ);
   MicroPrintf("NOTE: processing_time (second) = timestamp_clock_cycle/SYSTEM_CLINT_HZ\n\r");
   ms = timerDiff_0_1/(SYSTEM_CLINT_HZ/1000);
   measured_network_ms = ms;
   MicroPrintf("inference time (front layers): %ums\n\r", ms);
   
   timerDiff_2_3 = timerCmp3 - timerCmp2;
   u32 *v2 = (u32 *)&timerDiff_2_3;
   MicroPrintf("Inference clock cycle (hex): %x, %x\n\r", v2[1], v2[0]); //Timestamp
   //processing_time (second) = timestamp_clock_cycle/SYSTEM_CLINT_HZ
   MicroPrintf("SYSTEM_CLINT_HZ (hex): %x\n\r", SYSTEM_CLINT_HZ);
   MicroPrintf("NOTE: processing_time (second) = timestamp_clock_cycle/SYSTEM_CLINT_HZ\n\r");
   ms = timerDiff_2_3/(SYSTEM_CLINT_HZ/1000);
   measured_yolo_ms = ms;
   MicroPrintf("inference time (Yolo layer): %ums\n\r", ms);

   timerDiffTotal = timerCmpTotal1 - timerCmpTotal0;
   u32 *v3 = (u32 *)&timerDiffTotal;
   MicroPrintf("Inference clock cycle (hex): %x, %x\n\r", v3[1], v3[0]); //Timestamp
   //processing_time (second) = timestamp_clock_cycle/SYSTEM_CLINT_HZ
   MicroPrintf("SYSTEM_CLINT_HZ (hex): %x\n\r", SYSTEM_CLINT_HZ);
   MicroPrintf("NOTE: processing_time (second) = timestamp_clock_cycle/SYSTEM_CLINT_HZ\n\r");
   ms = timerDiffTotal/(SYSTEM_CLINT_HZ/1000);
   measured_total_ms = ms;
   MicroPrintf("inference time (Total): %ums\n\r", ms);


   bool result_matches=true;
   for(int i=0;i<total_output_layers;++i) {
      const TfLiteTensor* output=interpreter->output(i);
      const bool fine=output->dims->data[1]==6;
      const int8_t* expected=fine?person_reference_fine:person_reference_coarse;
      const unsigned expected_bytes=fine?sizeof(person_reference_fine):sizeof(person_reference_coarse);
      if(output->bytes!=expected_bytes){result_matches=false;continue;}
      for(unsigned j=0;j<expected_bytes;++j)
         if(abs((int)output->data.int8[j]-(int)expected[j])>1)result_matches=false;
   }
   result_check_passed=result_matches;
   MicroPrintf("RESULT_CHECK=%s (new person INT8 raw reference, tolerance1LSB)\r\n",result_matches?"PASS":"FAIL");

   MicroPrintf("INFERENCE_DONE (static image; not a fall video)\r\n");
   release_boxes(boxes, total_boxes);
#ifdef PERSON_LIVE_CAMERA
   if (result_matches) live_camera_run(interpreter, model_input, yolo_layers, total_output_layers);
   else { MicroPrintf("LIVE_BLOCKED_STATIC_CHECK_FAILED\r\n"); while (1) {} }
#elif defined(PERSON_VIDEO_REPLAY)
   if (result_matches) video_replay(yolo_layers);
#else
   benchmark(yolo_layers);
#endif
   for (int i = 0; i < total_output_layers; ++i) free(yolo_layers[i].outputs);
   free(yolo_layers);
   while (1) {}
}
