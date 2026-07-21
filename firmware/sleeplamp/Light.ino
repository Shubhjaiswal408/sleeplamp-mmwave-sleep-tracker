// SleepLamp · Light.ino — NeoPixel ring lamp (WS2812/SK6812) + adaptive + sunrise.
// The whole ring shows one colour (a lamp, not an animation). All output goes
// through writeRGB()/writeScaled(), so the adaptive + sunrise logic is unchanged
// from the old single-LED version — only the driver underneath is different.
//
// Library: Adafruit NeoPixel (>= 1.12.3; uses the ESP32-S3 RMT peripheral, which
// is free here — the radar is on UART). All calls run from loop()/core 1.
#include "types.h"
#include "config.h"
#include <Adafruit_NeoPixel.h>

#if USE_RGB
static Adafruit_NeoPixel ring(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
#endif

// paint the entire ring one colour (raw 0..255 per channel)
static void writeRGB(int r, int g, int b) {
#if USE_RGB
  r = constrain(r, 0, 255); g = constrain(g, 0, 255); b = constrain(b, 0, 255);
  uint32_t c = ring.Color(r, g, b);
  for (int i = 0; i < NEOPIXEL_COUNT; i++) ring.setPixelColor(i, c);
  ring.show();
  matterReflect(r, g, b);          // keep the smart-home app in sync (no-op if Matter off)
#endif
}
static void writeScaled(int r, int g, int b, int bright) {
  writeRGB(r * bright / 100, g * bright / 100, b * bright / 100);
}

void lightBegin() {
#if USE_RGB
  ring.begin();
  ring.setBrightness(255);      // we scale colours ourselves in writeScaled()
  ring.clear();
  ring.show();
#endif
}

// apply manual/off immediately (auto handled by lightAuto / alarm by lightSunrise)
void lightApply() {
  if (light.mode == 2)       writeRGB(0,0,0);
  else if (light.mode == 1)  writeScaled(light.r, light.g, light.b, light.bright);
}

// adaptive: colour from presence + OUR sleep engine (responds from minute 1)
void lightAuto() {
  if (light.mode != 0) return;
  SensorData d; SleepLive lv;
  xSemaphoreTake(mux, portMAX_DELAY); d = g; lv = live; xSemaphoreGive(mux);
  int r, gg, b, br = light.bright;
  bool asleep = lv.active && (lv.stage == 0 || lv.stage == 1);
  // Coherent status state-machine, highest priority first. A fresh HR lock briefly
  // overrides all of this with the red pulse (handled in loop / lightHeartbeat*).
  //   asleep            -> OFF (dark for sleep)
  //   got up mid-sleep  -> dim AMBER night-light (3 a.m. guide)
  //   in bed (awake)    -> dim BLUE (calm)
  //   present + HR lock -> GREEN (vitals locked, you're up)
  //   idle / searching  -> dim WHITE (no one, or person not yet locked)
  if      (asleep)                   { r=0;   gg=0;   b=0; }
  else if (!d.presence && lv.active) { r=255; gg=50;  b=0;   br=min(br,6);  }
  else if (d.inBed)                  { r=30;  gg=90;  b=255; br=min(br,40); }
  else if (d.heartRate > 0)          { r=0;   gg=220; b=120; br=min(br,60); }
  else                               { r=255; gg=255; b=255; br=min(br,15); }
  writeScaled(r, gg, b, br);
}

// sunrise ramp for the alarm. p = 0..1 (amber/dim -> bright warm-white)
void lightSunrise(float p) {
  p = constrain(p, 0.0f, 1.0f);
  int r = 255;
  int g = (int)(40 + p * 175);     // 40 -> 215
  int b = (int)(0  + p * 130);     // 0  -> 130
  int br = (int)(3 + p * 97);      // 3% -> 100%
  writeScaled(r, g, b, br);
}

// boot wiring check: shows pure R, G, B so you can verify DIN + colour order
void lightBootTest() {
#if USE_RGB && RGB_BOOT_TEST
  Serial.println(F("[LIGHT] boot test -> RED, GREEN, BLUE (0.8s each) on the ring."));
  Serial.println(F("        Colours wrong order? change NEO_GRB in Light.ino. Nothing lights? check DIN pin / 5V."));
  writeRGB(255,0,0); Serial.println(F("  RED"));   delay(800);
  writeRGB(0,255,0); Serial.println(F("  GREEN")); delay(800);
  writeRGB(0,0,255); Serial.println(F("  BLUE"));  delay(800);
  writeRGB(0,0,0);   Serial.println(F("  OFF"));   delay(300);
#endif
}

// ---- heart-rate lock visual: pulse the ring RED at the measured BPM on a fresh
// lock, for HR_PULSE_SEC seconds, so you can SEE that HR detection kicked in. ----
#if USE_RGB && USE_HR_PULSE
static bool     hbOn = false;
static uint32_t hbStart = 0;
static int      hbBpm = 60, hbPrevHR = 0;

// raw ring write that BYPASSES matterReflect — animation frames must not spam the
// smart-home fabric (and the pulse is transient, not the real lamp state).
static void writeRingRaw(int r, int g, int b) {
  r = constrain(r,0,255); g = constrain(g,0,255); b = constrain(b,0,255);
  uint32_t c = ring.Color(r,g,b);
  for (int i = 0; i < NEOPIXEL_COUNT; i++) ring.setPixelColor(i, c);
  ring.show();
}

// 1 Hz tick: detect a fresh HR lock (0 -> >0) and arm the pulse (not while asleep)
void lightHeartbeatCheck() {
  int hr, stg;
  xSemaphoreTake(mux, portMAX_DELAY); hr = g.heartRate; stg = live.stage; xSemaphoreGive(mux);
  bool asleep = (stg == 0 || stg == 1);
  if (hr > 0 && hbPrevHR == 0 && !asleep) {
    hbOn = true; hbStart = millis(); hbBpm = hr;
    Serial.printf("[LIGHT] HR lock %d bpm -> red pulse %ds\n", hr, HR_PULSE_SEC);
  }
  if (hr > 0) hbBpm = hr;             // keep the rhythm at the current rate
  hbPrevHR = hr;
}

bool lightHeartbeatActive() { return hbOn; }

// every loop: animate the red beat; restores the normal lamp when the 10 s is up
void lightHeartbeatStep() {
  if (!hbOn) return;
  uint32_t now = millis(), el = now - hbStart;
  if (el > (uint32_t)HR_PULSE_SEC * 1000UL) {  // done -> hand the ring back to normal
    hbOn = false; lightAuto(); lightApply(); return;
  }
  static uint32_t lastFrame = 0;
  if (now - lastFrame < 25) return;            // ~40 fps cap
  lastFrame = now;
  float period = 60000.0f / (hbBpm > 0 ? hbBpm : 60);   // ms per beat
  float ph = fmod((float)el, period) / period;          // 0..1 within a beat
  float b  = ph < 0.15f ? (ph / 0.15f)                  // sharp attack...
                        : powf(1.0f - (ph - 0.15f) / 0.85f, 1.5f);  // ...soft decay
  if (b < 0) b = 0;
  int rr = (int)((8 + b * 92) * 255.0f / 100.0f);       // 8%..100% red
  writeRingRaw(rr, 0, 0);
}
#else
void lightHeartbeatCheck() {}
bool lightHeartbeatActive() { return false; }
void lightHeartbeatStep() {}
#endif
