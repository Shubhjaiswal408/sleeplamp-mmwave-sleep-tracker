// SleepLamp · Settings.ino — persist lamp + alarm settings in NVS so they survive reboot
#include "types.h"
#include <Preferences.h>

static Preferences prefs;

void settingsBegin() {
  prefs.begin("sleeplamp", true);   // read-only
  light.mode   = prefs.getInt ("lmode", light.mode);
  light.r      = prefs.getInt ("lr",    light.r);
  light.g      = prefs.getInt ("lg",    light.g);
  light.b      = prefs.getInt ("lb",    light.b);
  light.bright = prefs.getInt ("lbri",  light.bright);
  alarmCfg.enabled = prefs.getBool("aen",  alarmCfg.enabled);
  alarmCfg.hour    = prefs.getInt ("ah",   alarmCfg.hour);
  alarmCfg.minute  = prefs.getInt ("am",   alarmCfg.minute);
  alarmCfg.window  = prefs.getInt ("awin", alarmCfg.window);
  prefs.end();
  // guard: a corrupted / near-black saved manual colour shows as "on" but emits no
  // light (we hit exactly this when a Matter round-trip once saved rgb(3,1,0) at 1%).
  if (light.mode == 1 && (max(max(light.r, light.g), light.b) < 8 || light.bright < 5)) {
    light.r = 255; light.g = 150; light.b = 60; light.bright = 60;
  }
  Serial.println(F("[NVS] settings restored"));
}

void settingsSaveLight() {
  prefs.begin("sleeplamp", false);
  prefs.putInt("lmode", light.mode); prefs.putInt("lr", light.r); prefs.putInt("lg", light.g);
  prefs.putInt("lb", light.b);       prefs.putInt("lbri", light.bright);
  prefs.end();
}

void settingsSaveAlarm() {
  prefs.begin("sleeplamp", false);
  prefs.putBool("aen", alarmCfg.enabled); prefs.putInt("ah", alarmCfg.hour);
  prefs.putInt("am", alarmCfg.minute);    prefs.putInt("awin", alarmCfg.window);
  prefs.end();
}
