#pragma once
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "model/yolo.h"
void live_camera_run(tflite::MicroInterpreter*, TfLiteTensor*, layer*, int);
void release_boxes(box*, int);
