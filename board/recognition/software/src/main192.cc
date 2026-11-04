#include "bsp.h"
#include "riscv.h"
#include "intc.h"
#include "live_camera.h"
#include "person_box_merge.h"
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include <stdlib.h>
#include <cstring>

extern const unsigned char live192_model[];
extern const int8_t live192_inputs[],live192_reference[];
bool check_nms();
static uint8_t tensor_arena[10000*1024];
volatile int result_check_passed;
static void require(bool ok,const char* error) { if(!ok) { MicroPrintf("%s\r\n",error);while(1) {} } }
void release_boxes(box* boxes,int count) {
  for(int i=0;i<count;++i) free(boxes[i].class_probabilities);
  free(boxes);
}
extern "C" int main() {
  bsp_init();csr_write(mtvec,trap_entry);IntcInitialize();
  MicroPrintf("V6_PERSON_192: pretrained weights unchanged; spatial resize; FPGA accelerator\r\n");
  require(check_nms(),"NMS_CHECK=FAIL");MicroPrintf("NMS_CHECK=PASS\r\n");
  require(check_person_box_merge(),"DUPLICATE_CHECK=FAIL");MicroPrintf("DUPLICATE_CHECK=PASS (weighted overlap merge; IoU=0.35)\r\n");
  static tflite::MicroErrorReporter reporter;
  static tflite::AllOpsResolver resolver;
  static tflite::MicroInterpreter interpreter(tflite::GetModel(live192_model),resolver,
    tensor_arena,sizeof(tensor_arena),&reporter,nullptr);
  require(interpreter.AllocateTensors()==kTfLiteOk,"MODEL192_ALLOC_FAIL");
  TfLiteTensor* input=interpreter.input(0);
  require(input->type==kTfLiteInt8 && input->bytes==192*192*3 && interpreter.outputs_size()==2,"MODEL192_INPUT_MISMATCH");
  float anchors[2][6]={{115,73,119,199,242,238},{12,18,37,49,52,132}};
  layer layers[2]={};
  for(unsigned i=0;i<2;++i) {
    TfLiteTensor* output=interpreter.output(i);unsigned size=i==0?6:12;
    require(output->dims->data[1]==size && output->dims->data[2]==size && output->bytes==size*size*18,"MODEL192_OUTPUT_MISMATCH");
    layers[i].channels=1;layers[i].height=size;layers[i].width=size;layers[i].classes=1;
    layers[i].boxes_per_scale=3;layers[i].total_anchors=3;layers[i].scale=1;layers[i].anchors=anchors[i];
    layers[i].outputs=static_cast<float*>(calloc(output->bytes,sizeof(float)));
    require(layers[i].outputs!=nullptr,"MODEL192_OUTPUT_ALLOC_FAIL");
  }
  for(unsigned sample=0;sample<3;++sample) {
    std::memcpy(input->data.int8,live192_inputs+sample*input->bytes,input->bytes);
    require(interpreter.Invoke()==kTfLiteOk,"MODEL192_FIXED_INVOKE_FAIL");
    unsigned offset=0;
    for(unsigned i=0;i<2;++i) {
      TfLiteTensor* output=interpreter.output(i);
      require(std::memcmp(output->data.int8,live192_reference+sample*3240+offset,output->bytes)==0,"MODEL192_RAW_CHECK_FAIL");
      for(unsigned j=0;j<output->bytes;++j)
        layers[i].outputs[j]=(output->data.int8[j]-output->params.zero_point)*output->params.scale;
      offset+=output->bytes;
    }
    int count=0;box* boxes=perform_inference(layers,2,&count,192,192,.78f,.5f);
    require(boxes || count==0,"MODEL192_FIXED_BOX_ALLOC_FAIL");
    require(count==(sample<2?1:0),"MODEL192_FIXED_BOX_CHECK_FAIL");
    release_boxes(boxes,count);MicroPrintf("STATIC192_CHECK sample=%u boxes=%u raw=EXACT PASS\r\n",sample,count);
  }
  result_check_passed=1;
  MicroPrintf("RESULT_CHECK=PASS (three fixed images vs RISC-V reference; not vendor 96x96 reference)\r\n");
  live_camera_run(&interpreter,input,layers,2);
}
