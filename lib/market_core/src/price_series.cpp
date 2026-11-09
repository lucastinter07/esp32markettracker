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

bool PriceSeries::hasTimes() const {
  if (empty()) return false;
  for (size_t i = 0; i < count_; i++) {
    if (timeAt(i) == 0) return false;
  }
  return true;
}
