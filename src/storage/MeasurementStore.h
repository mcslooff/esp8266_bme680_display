#pragma once
#include <Arduino.h>
#include "../model/Measurement.h"

class MeasurementStore {
public:
  explicit MeasurementStore(size_t capacity = 120);
  ~MeasurementStore();
  bool add(const Measurement& m);
  bool latest(Measurement& out) const;
  bool get(size_t index, Measurement& out) const; // 0 = newest
  bool average(size_t start, size_t end, Measurement& out) const;
  size_t size() const { return count_; }
  size_t capacity() const { return capacity_; }
  void clear();
private:
  Measurement* data_ = nullptr;
  size_t capacity_ = 0;
  size_t count_ = 0;
  size_t head_ = 0;
};
