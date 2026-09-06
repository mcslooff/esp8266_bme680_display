#include "MeasurementStore.h"
#include <stdlib.h>

MeasurementStore::MeasurementStore(size_t capacity) : capacity_(capacity) {
  if (capacity_ == 0) return;
  data_ = static_cast<Measurement*>(calloc(capacity_, sizeof(Measurement)));
  if (!data_) capacity_ = 0;
}
MeasurementStore::~MeasurementStore() { free(data_); }

bool MeasurementStore::add(const Measurement& m) {
  if (!data_ || capacity_ == 0 || !m.valid()) return false;
  head_ = (head_ + capacity_ - 1) % capacity_;
  data_[head_] = m;
  if (count_ < capacity_) ++count_;
  return true;
}

bool MeasurementStore::latest(Measurement& out) const { return get(0, out); }

bool MeasurementStore::get(size_t index, Measurement& out) const {
  if (!data_ || index >= count_) return false;
  out = data_[(head_ + index) % capacity_];
  return true;
}

bool MeasurementStore::average(size_t start, size_t end, Measurement& out) const {
  if (start > end || end >= count_) return false;
  double t=0,h=0,p=0,g=0,b=0; size_t n=0; Measurement m;
  for (size_t i=start; i<=end; ++i) {
    if (!get(i,m)) continue;
    t += m.temperature; h += m.humidity; p += m.pressurePa;
    g += m.gasResistanceOhm; b += m.batteryVoltage; ++n;
  }
  if (!n) return false;
  out.timestamp = time(nullptr);
  out.temperature=t/n; out.humidity=h/n; out.pressurePa=p/n;
  out.gasResistanceOhm=g/n; out.batteryVoltage=b/n;
  return true;
}
void MeasurementStore::clear() { count_=0; head_=0; }
