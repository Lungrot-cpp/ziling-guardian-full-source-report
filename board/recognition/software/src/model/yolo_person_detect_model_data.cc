#include "yolo_person_detect_model_data.h"
const unsigned int yolo_person_detect_model_data_len = 213472;
asm(R"MODEL(
.pushsection .rodata.person_model,"a",@progbits
.balign 16
.global yolo_person_detect_model_data
yolo_person_detect_model_data:
.incbin "src/model/person_demo.tflite"
.popsection
)MODEL");
