#include "price_series.h"

void PriceSeries::push(float price) {
  data_[head_] = price;
  head_ = (head_ + 1) % CAPACITY;
  if (count_ < CAPACITY) count_++;
}

void PriceSeries::clear() {
  head_ = 0;
  count_ = 0;
}

float PriceSeries::at(size_t i) const {
  if (i >= count_) return 0.0f;
  // Once full, the oldest point sits at head_ (the next slot to be overwritten).
  size_t start = (count_ < CAPACITY) ? 0 : head_;
  return data_[(start + i) % CAPACITY];
}

float PriceSeries::minValue() const {
  if (empty()) return 0.0f;
  float m = at(0);
  for (size_t i = 1; i < count_; i++) {
    if (at(i) < m) m = at(i);
  }
  return m;
}

float PriceSeries::maxValue() const {
  if (empty()) return 0.0f;
  float m = at(0);
  for (size_t i = 1; i < count_; i++) {
    if (at(i) > m) m = at(i);
  }
  return m;
}
