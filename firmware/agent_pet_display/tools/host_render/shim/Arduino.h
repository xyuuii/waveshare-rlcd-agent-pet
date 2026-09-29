#pragma once

// Minimal Arduino surface for rendering firmware screens on a desktop host.
// Only what the display code touches is provided; hardware calls are no-ops.

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#ifndef LOW
#define LOW 0
#endif
#ifndef HIGH
#define HIGH 1
#endif
#ifndef INPUT
#define INPUT 0x01
#endif
#ifndef OUTPUT
#define OUTPUT 0x03
#endif
#ifndef INPUT_PULLUP
#define INPUT_PULLUP 0x05
#endif

uint32_t millis();
void delay(uint32_t ms);
void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);
int digitalRead(int pin);

// Host-only helpers used by the render harness.
void hostSetMillis(uint32_t value);

class String {
 public:
  String(const char* text = "") : value_(text ? text : "") {}
  String(const std::string& text) : value_(text) {}
  String(int number) : value_(std::to_string(number)) {}
  String(unsigned int number) : value_(std::to_string(number)) {}
  String(long number) : value_(std::to_string(number)) {}
  String(unsigned long number) : value_(std::to_string(number)) {}
  const char* c_str() const { return value_.c_str(); }
  size_t length() const { return value_.size(); }

 private:
  std::string value_;
};
