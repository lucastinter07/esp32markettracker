// Fixed-size ring buffer of prices: the stored history that the charts are drawn from.
// Each point also stores when it was taken, so charts can place it at its real time
// of day. Pure C++ with no Arduino.h, so it compiles and is unit-tested on the PC (test/).
#pragma once
#include <stddef.h>
#include <stdint.h>

class PriceSeries {
 public:
  // 390 = one point per minute across a 6.5-hour trading session.
  static const size_t CAPACITY = 390;

  // time: unix seconds when the price was taken (0 = unknown, e.g. demo prices).
  void push(float price, uint32_t time = 0);
  void clear();

  size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }

  // i = 0 is the oldest point, size() - 1 the newest.
  float at(size_t i) const;
  uint32_t timeAt(size_t i) const;
  float first() const { return at(0); }
  float last() const { return at(count_ - 1); }

  float minValue() const;
  float maxValue() const;
  size_t indexOfMin() const;   // position of the lowest price (0 if empty)
  size_t indexOfMax() const;

  // True when every point has a real time, so a chart can use a time axis.
  bool hasTimes() const;

  // The first point taken at or after `time`. False if there is none.
  bool firstAtOrAfter(uint32_t time, float& price) const;

  // True if the points leave a hole longer than `maxGap` seconds anywhere between `from`
  // and `to`: at the start, between two points, or at the end. (Always true when empty.)
  bool hasGap(uint32_t from, uint32_t to, uint32_t maxGap) const;

  // Fills holes with extra points (e.g. 5-minute bars, oldest first). Only points between
  // `from` and `to` are used, and only where no existing point is within `minSpacing`
  // seconds, so the existing (live) points are kept and nothing is duplicated. The result
  // stays in time order; if it's over CAPACITY, the oldest points are dropped.
  void fillGaps(const uint32_t* times, const float* prices, int count, uint32_t from, uint32_t to,
                uint32_t minSpacing);

 private:
  size_t slot(size_t i) const;   // array position of the i-th oldest point

  float data_[CAPACITY] = {};
  uint32_t times_[CAPACITY] = {};
  size_t head_ = 0;   // where the next push goes
  size_t count_ = 0;
};
