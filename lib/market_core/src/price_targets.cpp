#include "price_targets.h"

bool checkTarget(PriceTarget& target, bool above, float price, int32_t today) {
  if (!targetArmed(target) || !(price > 0)) return false;
  bool reached = above ? price >= target.price : price <= target.price;
  if (!reached) return false;
  target.hitDay = today;
  return true;
}

bool targetValid(float target, bool above, float currentPrice) {
  if (!(target > 0)) return false;
  if (!(currentPrice > 0)) return true;
  return above ? target > currentPrice : target < currentPrice;
}
