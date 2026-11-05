// Fixed-size ring buffer of prices: the stored history that the charts are drawn from.
// Pure C++ with no Arduino.h, so it compiles and is unit-tested on the PC (test/).
#pragma once
#include <stddef.h>

class PriceSeries {
 public:
  // 390 = one point per minute across a 6.5-hour trading session.
  static const size_t CAPACITY = 390;

  void push(float price);
  void clear();

  size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }

  // i = 0 is the oldest point, size() - 1 the newest.
  float at(size_t i) const;
  float first() const { return at(0); }
  float last() const { return at(count_ - 1); }

  float minValue() const;
  float maxValue() const;

 private:
  float data_[CAPACITY] = {};
  size_t head_ = 0;   // where the next push goes
  size_t count_ = 0;
};
