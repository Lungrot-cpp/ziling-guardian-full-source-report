#include "yolo.h"
#include <stdlib.h>

// DIoU subtracts a nonnegative distance penalty, so DIoU <= IoU.
void perform_nms(box* boxes, int total, int classes, float threshold) {
    for (int c = 0; c < classes; ++c) {
        for (int i = 0; i < total; ++i) boxes[i].sort_class_index = c;
        qsort(boxes, total, sizeof(box), compare_class_probability);
        for (int i = 0; i < total; ++i) {
            if (!(boxes[i].class_probabilities[c] > 0)) continue;
            for (int j = 0; j < i; ++j) {
                if (!(boxes[j].class_probabilities[c] > 0)) continue;
                if (threshold >= 0 &&
                    (boxes[i].x_max <= boxes[j].x_min || boxes[j].x_max <= boxes[i].x_min ||
                     boxes[i].y_max <= boxes[j].y_min || boxes[j].y_max <= boxes[i].y_min)) continue;
                if (calc_iou(boxes[i], boxes[j]) <= threshold) continue;
                if (calc_diou(boxes[i], boxes[j]) > threshold) {
                    boxes[i].class_probabilities[c] = 0;
                    break;
                }
            }
        }
    }
}

// Alias to the unchanged vendor machine code, added by objcopy for this check.
extern "C" void vendor_perform_nms(box*, int, int, float);
volatile int nms_check_passed = 0;

bool check_nms() {
    unsigned random = 20261001;
    for (int trial = 0; trial < 32; ++trial) {
        box expected[12] = {}, actual[12] = {};
        float expected_probs[12][2], actual_probs[12][2];
        const int total = trial % 13, classes = 1 + trial % 2;
        const float thresholds[] = {0, 0.25f, 0.5f, 0.9f};
        for (int i = 0; i < total; ++i) {
            random = random * 1664525u + 1013904223u;
            box b = {};
            b.x_min = (random & 255) / 256.0f;
            b.y_min = ((random >> 8) & 255) / 256.0f;
            b.x_max = b.x_min + 0.1f + ((random >> 16) & 255) / 256.0f;
            b.y_max = b.y_min + 0.1f + ((random >> 24) & 255) / 256.0f;
            if (i && i % 3 == 0) b = expected[i - 1]; // identical boxes, tied scores
            for (int c = 0; c < classes; ++c)
                expected_probs[i][c] = actual_probs[i][c] = ((random >> (c * 8)) & 7) / 8.0f;
            expected[i] = actual[i] = b;
            expected[i].class_probabilities = expected_probs[i];
            actual[i].class_probabilities = actual_probs[i];
        }
        vendor_perform_nms(expected, total, classes, thresholds[trial % 4]);
        perform_nms(actual, total, classes, thresholds[trial % 4]);
        for (int i = 0; i < total; ++i) {
            if (expected[i].x_min != actual[i].x_min || expected[i].y_min != actual[i].y_min ||
                expected[i].x_max != actual[i].x_max || expected[i].y_max != actual[i].y_max) return false;
            for (int c = 0; c < classes; ++c)
                if (expected[i].class_probabilities[c] != actual[i].class_probabilities[c]) return false;
        }
    }
    nms_check_passed = 1;
    return true;
}
