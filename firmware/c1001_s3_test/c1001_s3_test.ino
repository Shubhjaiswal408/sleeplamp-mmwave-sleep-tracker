/*
 * C1001 radar test for ESP32-S3 — uses the project's own ShubhSensor driver
 * PLUS the same cache-first read the main firmware uses (robust in sleep mode,
 * so it never hangs on "mode fail").
 *   C1001 TX -> GPIO18 (RX) , C1001 RX -> GPIO17 (TX) , VCC=5V , GND=GND
 *
 * Prints presence / motion / range / heart-rate / breathing on Serial @115200.
 * No WiFi, no web — isolates the sensor.
 */
#include "ShubhSensor.h"   // project's single-file C1001 driver (no external lib needed)

#define RADAR_RX 17
#define RADAR_TX 18

ShubhSensor hu(&Serial1);

const char* breathTxt(int v){ switch(v){case 1:return "Normal";case 2:return "Fast";case 3:return "Slow";case 4:return "None";} return "-"; }
const char* motionTxt(int v){ switch(v){case 0:return "None";case 1:return "Still";case 2:return "Active";} return "-"; }

// cache-first read: the radar PUSHES report frames in sleep mode; the pushed
// report cmd = the query cmd with the 0x80 bit cleared (same trick as Sensor.ino).
static int rep1(uint8_t con, uint8_t qcmd) {
  uint8_t d[16];
  int n = hu.cacheGet(con, qcmd & 0x7F, d, sizeof(d), 1600);
  return n > 0 ? d[0] : -1;
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1, RADAR_RX, RADAR_TX);
  delay(300);
  Serial.println("\n=== C1001 radar test (cache-read, same as main firmware) ===");

  int tries = 0;
  while (hu.begin() != 0) {                 // radar answering on UART yet?
    Serial.println("init error, retry");
    delay(600);
    if (++tries > 30) { Serial.println("!! no UART reply — check TX->18 / RX->17 / 5V / GND"); tries = 0; }
  }
  Serial.println("radar OK — responds on UART.");

  // getWorkMode() returns 2 on a flooded/failed query too, and 2 == sleep mode,
  // so we NEVER hang on 'mode fail'. Only switch if it's really not in sleep mode.
  uint8_t mode = hu.getWorkMode();
  Serial.printf("work mode = %d (2 = sleep)\n", mode);
  if (mode != 2) {
    Serial.println("switching to sleep mode (~20 s, once)...");
    hu.configWorkMode(hu.eSleepMode);
    hu.sensorRet();
    delay(12000);
    while (Serial1.available()) Serial1.read();
  }
  Serial.println("Ready. Sit/lie STILL ~0.5 m facing sensor; HR appears after lock (1-2 min).\n");
}

void loop() {
  hu.pump();   // harvest every pushed report frame into the cache

  int pres = rep1(0x80, 0x81); if (pres < 0) pres = hu.smHumanData(hu.eHumanPresence);
  int mov  = rep1(0x80, 0x82); if (mov  < 0) mov  = hu.smHumanData(hu.eHumanMovement);
  int rng  = rep1(0x80, 0x83); if (rng  < 0) rng  = hu.smHumanData(hu.eHumanMovingRange);
  int hr   = rep1(0x85, 0x82); if (hr   < 0) hr   = hu.getHeartRate();
  int rsp  = rep1(0x81, 0x82); if (rsp  < 0) rsp  = hu.getBreatheValue();
  int bst  = rep1(0x81, 0x81); if (bst  < 0) bst  = hu.getBreatheState();
  if (hr  == 255) hr  = 0;                  // 255 = no fresh value
  if (rsp == 255) rsp = 0;

  Serial.printf("presence:%-2d motion:%-6s range:%-3d  HR:%-3d bpm  resp:%-3d rpm  breath:%s\n",
                pres, motionTxt(mov < 0 ? 0 : mov), rng < 0 ? 0 : rng, hr, rsp, breathTxt(bst));
  delay(1000);
}
