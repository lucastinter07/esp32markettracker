// Passive buzzer feedback sounds.
#pragma once

void buzzerInit();
void buzzerClick();   // short tick on button/touch input
void buzzerChime();   // two-note chime (e.g. market open/close)
void buzzerAlert(bool up);   // price alert: three rising (up) or falling (down) notes
void buzzerTarget();         // price target reached: a high two-note trill, played twice
void buzzerSetMuted(bool muted);
bool buzzerMuted();
