// ui 컴포넌트 내부 공유 선언
#pragma once

#include "lvgl.h"

// 하드웨어 진단 화면: 색상 순서, 터치 좌표/범위, 화면 갱신(tearing) 확인용
void ui_diag_create(lv_obj_t *parent);
