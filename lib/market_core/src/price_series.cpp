#include "price_series.h"

void PriceSeries::push(float price, uint32_t time) {
  data_[head_] = price;
  times_[head_] = time;
  head_ = (head_ + 1) % CAPACITY;
  if (count_ < CAPACITY) count_++;
}

void PriceSeries::clear() {
  head_ = 0;
  count_ = 0;
}

size_t PriceSeries::slot(size_t i) const {
  // Once full, the oldest point sits at head_ (the next slot to be overwritten).
  size_t start = (count_ < CAPACITY) ? 0 : head_;
  return (start + i) % CAPACITY;
}

float PriceSeries::at(size_t i) const {
  if (i >= count_) return 0.0f;
  return data_[slot(i)];
}

uint32_t PriceSeries::timeAt(size_t i) const {
  if (i >= count_) return 0;
  return times_[slot(i)];
}

size_t PriceSeries::indexOfMin() const {
  size_t best = 0;
  for (size_t i = 1; i < count_; i++) {
    if (at(i) < at(best)) best = i;
  }
  return best;
}

size_t PriceSeries::indexOfMax() const {
  size_t best = 0;
  for (size_t i = 1; i < count_; i++) {
    if (at(i) > at(best)) best = i;
  }
  return best;
}

float PriceSeries::minValue() const {
  return empty() ? 0.0f : at(indexOfMin());
}

float PriceSeries::maxValue() const {
  return empty() ? 0.0f : at(indexOfMax());
}

bool PriceSeries::firstAtOrAfter(uint32_t time, float& price) const {
  for (size_t i = 0; i < count_; i++) {
    if (timeAt(i) >= time) {
      price = at(i);
      return true;
    }
  }
  return false;
}

bool PriceSeries::hasGap(uint32_t from, uint32_t to, uint32_t maxGap) const {
  if (empty()) return true;
  uint32_t previous = from;
  for (size_t i = 0; i < count_; i++) {
    uint32_t t = timeAt(i);
    if (t > previous && t - previous > maxGap) return true;
    if (t > previous) previous = t;
  }
  return to > previous && to - previous > maxGap;
}

void PriceSeries::fillGaps(const uint32_t* times, const float* prices, int count, uint32_t from, uint32_t to,
                           uint32_t minSpacing) {
  // Copy the existing points out, then rebuild by merging both lists in time order.
  static float oldPrices[CAPACITY];
  static uint32_t oldTimes[CAPACITY];
  size_t oldCount = count_;
  for (size_t i = 0; i < oldCount; i++) {
    oldPrices[i] = at(i);
    oldTimes[i] = timeAt(i);
  }
  clear();
  size_t k = 0;   // next existing point
  for (int i = 0; i < count; i++) {
    uint32_t t = times[i];
    if (t <= from || t > to) continue;
    while (k < oldCount && oldTimes[k] <= t) {   // existing points up to this time go first
      push(oldPrices[k], oldTimes[k]);
      k++;
    }
    // Skip the extra point if an existing one is close by, on either side.
    bool nearBefore = k > 0 && t - oldTimes[k - 1] < minSpacing;
    bool nearAfter = k < oldCount && oldTimes[k] - t < minSpacing;
    if (!nearBefore && !nearAfter) push(prices[i], t);
  }
  while (k < oldCount) {
    push(oldPrices[k], oldTimes[k]);
    k++;
  }
}

bool PriceSeries::hasTimes() const {
  if (empty()) return false;
  for (size_t i = 0; i < count_; i++) {
    if (timeAt(i) == 0) return false;
  }
  return true;
}
