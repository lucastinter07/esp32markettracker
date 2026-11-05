#include "market_calendar.h"

namespace {

const int OPEN_MIN = 9 * 60 + 30;     // 9:30
const int CLOSE_MIN = 16 * 60;        // 16:00
const int EARLY_CLOSE_MIN = 13 * 60;  // 13:00
const int PRE_START_MIN = 4 * 60;     // 4:00
const int AFTER_LEN_MIN = 4 * 60;     // extended session lasts 4 h after the close

// Days since 1970-01-01 for a calendar date (Howard Hinnant's algorithm).
// Turning dates into plain day numbers makes "add a day" and "which weekday" simple.
int32_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  int32_t era = (y >= 0 ? y : y - 399) / 400;
  int32_t yoe = y - era * 400;
  int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

void civilFromDays(int32_t z, int& y, int& m, int& d) {
  z += 719468;
  int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  int32_t doe = z - era * 146097;
  int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int32_t mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp + (mp < 10 ? 3 : -9);
  y = yoe + era * 400 + (m <= 2);
}

int weekdayFromDays(int32_t z) {
  return (int)((z % 7 + 7 + 4) % 7);   // 1970-01-01 was a Thursday (4)
}

// Day number of the nth given weekday in a month (n = 1 for the first).
int32_t nthWeekday(int y, int m, int weekday, int n) {
  int32_t first = daysFromCivil(y, m, 1);
  int offset = (weekday - weekdayFromDays(first) + 7) % 7;
  return first + offset + (n - 1) * 7;
}

int32_t lastWeekday(int y, int m, int weekday) {
  int32_t last = daysFromCivil(m == 12 ? y + 1 : y, m == 12 ? 1 : m + 1, 1) - 1;
  int offset = (weekdayFromDays(last) - weekday + 7) % 7;
  return last - offset;
}

// A fixed-date holiday that falls on a weekend moves to Friday (if Saturday)
// or Monday (if Sunday).
int32_t observed(int y, int m, int d) {
  int32_t z = daysFromCivil(y, m, d);
  int wd = weekdayFromDays(z);
  if (wd == 6) return z - 1;
  if (wd == 0) return z + 1;
  return z;
}

bool isHolidayDays(int y, int32_t z) {
  // New Year's Day: moves to Monday if on Sunday, but NYSE does NOT close the
  // Friday before when it falls on Saturday.
  int32_t ny = daysFromCivil(y, 1, 1);
  if (weekdayFromDays(ny) == 0) ny += 1;
  if (weekdayFromDays(ny) != 6 && z == ny) return true;

  if (z == nthWeekday(y, 1, 1, 3)) return true;   // Martin Luther King Jr. Day: 3rd Monday Jan
  if (z == nthWeekday(y, 2, 1, 3)) return true;   // Washington's Birthday: 3rd Monday Feb

  int em, ed;
  easterSunday(y, em, ed);
  if (z == daysFromCivil(y, em, ed) - 2) return true;   // Good Friday

  if (z == lastWeekday(y, 5, 1)) return true;           // Memorial Day: last Monday May
  if (y >= 2022 && z == observed(y, 6, 19)) return true;  // Juneteenth (since 2022)
  if (z == observed(y, 7, 4)) return true;              // Independence Day
  if (z == nthWeekday(y, 9, 1, 1)) return true;         // Labor Day: 1st Monday Sep
  if (z == nthWeekday(y, 11, 4, 4)) return true;        // Thanksgiving: 4th Thursday Nov
  if (z == observed(y, 12, 25)) return true;            // Christmas
  return false;
}

bool isEarlyCloseDays(int y, int32_t z) {
  if (z == nthWeekday(y, 11, 4, 4) + 1) return true;   // day after Thanksgiving
  // July 3 and December 24 close early when they're a Monday-Thursday
  // (on a Friday they're either the observed holiday or a normal day).
  int32_t jul3 = daysFromCivil(y, 7, 3);
  int32_t dec24 = daysFromCivil(y, 12, 24);
  int wdJul = weekdayFromDays(jul3);
  int wdDec = weekdayFromDays(dec24);
  if (z == jul3 && wdJul >= 1 && wdJul <= 4) return true;
  if (z == dec24 && wdDec >= 1 && wdDec <= 4) return true;
  return false;
}

bool isTradingDays(int32_t z) {
  int y, m, d;
  civilFromDays(z, y, m, d);
  int wd = weekdayFromDays(z);
  return wd != 0 && wd != 6 && !isHolidayDays(y, z);
}

}  // namespace

void easterSunday(int year, int& month, int& day) {
  // Anonymous Gregorian algorithm
  int a = year % 19, b = year / 100, c = year % 100;
  int d = b / 4, e = b % 4, f = (b + 8) / 25, g = (b - f + 1) / 3;
  int h = (19 * a + b - d - g + 15) % 30;
  int i = c / 4, k = c % 4;
  int l = (32 + 2 * e + 2 * i - h - k) % 7;
  int m = (a + 11 * h + 22 * l) / 451;
  month = (h + l - 7 * m + 114) / 31;
  day = ((h + l - 7 * m + 114) % 31) + 1;
}

int weekdayOf(int year, int month, int day) {
  return weekdayFromDays(daysFromCivil(year, month, day));
}

bool isHoliday(int year, int month, int day) {
  return isHolidayDays(year, daysFromCivil(year, month, day));
}

bool isEarlyClose(int year, int month, int day) {
  return isEarlyCloseDays(year, daysFromCivil(year, month, day));
}

bool isTradingDay(int year, int month, int day) {
  return isTradingDays(daysFromCivil(year, month, day));
}

MarketStatus marketStatus(const EtTime& now) {
  MarketStatus s{};
  int32_t today = daysFromCivil(now.year, now.month, now.day);
  int minuteOfDay = now.hour * 60 + now.minute;
  bool trading = isTradingDays(today);

  s.holiday = isHolidayDays(now.year, today);
  s.earlyClose = trading && isEarlyCloseDays(now.year, today);
  int close = s.earlyClose ? EARLY_CLOSE_MIN : CLOSE_MIN;

  if (!trading) {
    s.phase = MarketPhase::Closed;
  } else if (minuteOfDay < PRE_START_MIN) {
    s.phase = MarketPhase::Closed;
  } else if (minuteOfDay < OPEN_MIN) {
    s.phase = MarketPhase::PreMarket;
  } else if (minuteOfDay < close) {
    s.phase = MarketPhase::Open;
    s.minutesToClose = close - minuteOfDay;
  } else if (minuteOfDay < close + AFTER_LEN_MIN) {
    s.phase = MarketPhase::AfterHours;
  } else {
    s.phase = MarketPhase::Closed;
  }

  if (s.phase != MarketPhase::Open) {
    // Next open is today if we're before 9:30 on a trading day, otherwise the
    // next trading day. 10 days is more than any run of weekends + holidays.
    int32_t day = (trading && minuteOfDay < OPEN_MIN) ? today : today + 1;
    while (!isTradingDays(day) && day < today + 10) day++;
    s.minutesToOpen = (day - today) * 1440 + OPEN_MIN - minuteOfDay;
    s.nextOpenWeekday = weekdayFromDays(day);
    s.nextOpenIsToday = day == today;
  }
  return s;
}
