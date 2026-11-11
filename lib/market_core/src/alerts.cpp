#include "alerts.h"

AlertDirection checkAlert(AlertState& state, float changePct, float thresholdPct, int32_t today) {
  if (state.day != today) {   // a new day: both directions can fire again
    state.day = today;
    state.upFired = false;
    state.downFired = false;
  }
  if (thresholdPct <= 0) return AlertDirection::None;
  if (changePct >= thresholdPct && !state.upFired) {
    state.upFired = true;
    return AlertDirection::Up;
  }
  if (changePct <= -thresholdPct && !state.downFired) {
    state.downFired = true;
    return AlertDirection::Down;
  }
  return AlertDirection::None;
}
