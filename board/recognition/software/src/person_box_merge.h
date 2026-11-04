#pragma once
#include "model/yolo.h"
#include <cmath>

// shortcut: heavily overlapping people can merge; validate crowded scenes before extending this demo.
inline int merge_person_boxes(box* boxes,int count) {
  int remaining=0;
  for(int i=0;i<count;++i) {
    box& a=boxes[i];
    if(!(a.class_probabilities[0]>0)) continue;
    float weight=a.class_probabilities[0];
    for(int j=i+1;j<count;++j) {
      box& b=boxes[j];float score=b.class_probabilities[0];
      if(!(score>0) || !(calc_iou(a,b)>0.35f)) continue;
      float combined=weight+score;
      a.x_min=(a.x_min*weight+b.x_min*score)/combined;
      a.y_min=(a.y_min*weight+b.y_min*score)/combined;
      a.x_max=(a.x_max*weight+b.x_max*score)/combined;
      a.y_max=(a.y_max*weight+b.y_max*score)/combined;
      a.class_probabilities[0]=std::fmax(a.class_probabilities[0],score);
      weight=combined;b.class_probabilities[0]=0;
    }
    ++remaining;
  }
  return remaining;
}

inline bool check_person_box_merge() {
  float scores[3]={0.9f,0.8f,0.85f};
  box boxes[3]={{0.1f,0.1f,0.3f,0.9f,0,&scores[0],0},
                {0.12f,0.15f,0.32f,0.95f,0,&scores[1],0},
                {0.6f,0.1f,0.8f,0.9f,0,&scores[2],0}};
  return merge_person_boxes(boxes,3)==2 && scores[1]==0 && scores[2]==0.85f &&
    std::fabs(boxes[0].x_min-0.10941176f)<0.00001f &&
    std::fabs(boxes[0].y_max-0.92352941f)<0.00001f && scores[0]==0.9f;
}
