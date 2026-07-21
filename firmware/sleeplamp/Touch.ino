// SleepLamp · Touch.ino — TTP223 capacitive touch button.
// Polled from loop() (core 1), edge-detected and level-debounced. Each tap steps
// the lamp one place around a single cycle:
//   • alarm ringing -> any tap dismisses the alarm
//   • tap           -> Auto -> Dim 20% -> Med 60% -> Max 100% -> Off -> Auto ...
//     (Auto = smart status colours; Dim/Med/Max = plain warm lamp)
#include "types.h"
#include "config.h"

#if USE_TOUCH
static const int TOUCH_ON_LEVEL = TOUCH_ACTIVE_HIGH ? HIGH : LOW;

void touchBegin() {
  // Active-high: pull DOWN so an unconnected / unpowered TTP223 reads a steady
  // LOW instead of floating and firing phantom taps (which flip the lamp on/off
  // by itself). A real TTP223 push-pull output still drives HIGH on touch and
  // easily overrides the internal pulldown.
  pinMode(TOUCH_PIN, TOUCH_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
  Serial.printf("[TOUCH] TTP223 on GPIO%d — tap cycles Auto/Dim/Med/Max/Off\n", TOUCH_PIN);
}

// each tap steps the lamp one place around the cycle:
//   Auto -> Dim 20% -> Med 60% -> Max 100% -> Off -> Auto -> ...
// "Auto" hands the ring back to the smart status-colour logic (lightAuto); the
// Dim/Med/Max steps are a plain warm-white lamp. One button reaches everything.
static void tapCycle() {
  xSemaphoreTake(mux, portMAX_DELAY);
  light.r = 255; light.g = 150; light.b = 60;                          // warm-white manual lamp
  if      (light.mode == 0)    { light.mode = 1; light.bright = 20; }  // Auto -> Dim
  else if (light.mode == 2)    { light.mode = 0; }                     // Off  -> Auto
  else if (light.bright < 40)  { light.bright = 60;  }                 // Dim  -> Med
  else if (light.bright < 90)  { light.bright = 100; }                 // Med  -> Max
  else                         { light.mode = 2;     }                 // Max  -> Off
  int mode = light.mode, br = light.bright;
  xSemaphoreGive(mux);
  if (mode == 0) lightAuto();        // back to Auto -> repaint the status colour right away
  else           lightApply();       // manual brightness / off
  settingsSaveLight();
  Serial.printf("[TOUCH] tap -> %s\n",
    mode == 0 ? "AUTO (smart colours)" : mode == 2 ? "OFF" :
    br <= 20 ? "DIM 20%" : br <= 60 ? "MED 60%" : "MAX 100%");
}

// Level-debounced: the pin must hold a NEW level steady for SETTLE_MS before we
// accept the change — rejects brief electrical noise / floating-pin glitches,
// while a real touch (held well over 40 ms) registers cleanly. One press = one step.
void handleTouch() {
  static const uint32_t SETTLE_MS = 40;
  static int      stable  = -1;     // current debounced level
  static int      lastRaw = -1;     // previous raw sample
  static uint32_t since   = 0;      // when the raw level last changed
  static uint32_t lastAct = 0;      // last accepted press (action debounce)

  int v = digitalRead(TOUCH_PIN);
  uint32_t now = millis();
  if (v != lastRaw) { lastRaw = v; since = now; }     // raw changed -> restart settle timer
  if (now - since < SETTLE_MS) return;                // not stable yet -> ignore (noise)
  if (stable < 0) { stable = v; return; }             // first stable read = idle level
  if (v == stable) return;                            // no debounced transition

  stable = v;
  // a real, debounced press edge (idle -> touched)
  if (stable == TOUCH_ON_LEVEL && now - lastAct > TOUCH_DEBOUNCE_MS) {
    lastAct = now;
    bool firing;
    xSemaphoreTake(mux, portMAX_DELAY); firing = alarmCfg.firing; xSemaphoreGive(mux);
    if (firing) { g_alarmStop = true; Serial.println(F("[TOUCH] tap -> alarm dismissed")); }
    else        { tapCycle(); }
  }
}
#else
void touchBegin() {}
void handleTouch() {}
#endif
