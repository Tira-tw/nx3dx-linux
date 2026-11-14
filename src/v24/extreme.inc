/* (v2.4) the versus' EXTREME rule (extreme.h): included by hud.c */
#include "v24/extreme.h"

static int extreme_on;

int extreme_area_ok(int a) { return a == EXTREME_AREA_C1 || a == EXTREME_AREA_OSAKA; }

void hud_set_extreme(int on) { extreme_on = on != 0; }

int hud_extreme(void) { return extreme_on; }
