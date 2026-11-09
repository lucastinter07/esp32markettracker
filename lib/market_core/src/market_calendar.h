// NYSE trading calendar: regular hours, extended hours, holidays and early closes.
// Holidays are calculated from NYSE's rules rather than a fixed table, so it keeps
// working in future years. Pure C++ (no Arduino.h), unit-tested on the PC.
#pragma once
#include <stdint.h>

// A date and time in US Eastern Time (the exchange's own clock).
struct EtTime {
  int year;
  int month;    // 1-12
  int day;      // 1-31
  int hour;     // 0-23
  int minute;   // 0-59
};

enum class MarketPhase : uint8_t {
  PreMarket,    // 4:00 - 9:30
  Open,         // 9:30 - 16:00 (13:00 on early-close days)
  AfterHours,   // close - 20:00 (17:00 on early-close days)
  Closed,       // overnight, weekends, holidays
};

struct MarketStatus {
  MarketPhase phase;
  bool holiday;          // today is an exchange holiday
  bool earlyClose;       // today closes at 13:00
  int32_t minutesToClose;  // valid when phase == Open
  int32_t minutesToOpen;   // valid when phase != Open: until the next 9:30 open
  int nextOpenWeekday;     // 0 = Sunday ... 6 = Saturday, day of that next open
  bool nextOpenIsToday;
};

MarketStatus marketStatus(const EtTime& now);

bool isHoliday(int year, int month, int day);
bool isEarlyClose(int year, int month, int day);
bool isTradingDay(int year, int month, int day);

// A date as one number: days since 1970-01-01. Makes "how many days apart" a subtraction.
int32_t dayNumber(int year, int month, int day);

// Day number of the most recent session that has started: today once it's past 9:30
// on a trading day, otherwise the previous trading day (e.g. Friday, all weekend).
int32_t lastSessionDay(const EtTime& now);

// Day number of the most recent session that has finished, counting a session as finished
// 15 minutes after its close (so data providers have published the final prices).
int32_t lastCompletedSessionDay(const EtTime& now);

// Date helpers (also used by the tests)
int weekdayOf(int year, int month, int day);   // 0 = Sunday ... 6 = Saturday
void easterSunday(int year, int& month, int& day);
