// Passive buzzer feedback sounds.
#pragma once

void buzzerInit();
void buzzerClick();   // short tick on button/touch input
void buzzerChime();   // two-note chime (e.g. market open/close)
void buzzerSetMuted(bool muted);
bool buzzerMuted();
