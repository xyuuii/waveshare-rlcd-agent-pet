#include <Arduino.h>

#include "app_controller.h"

namespace {

AppController gApp;

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);
  gApp.begin();
}

void loop() {
  gApp.tick();
}
