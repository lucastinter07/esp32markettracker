#include "net/quote_client.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "config.h"
#include "data/market_data.h"
#include "data/sectors.h"
#include "market_calendar.h"
#include "metrics_parser.h"
#include "net/finnhub_ca.h"
#include "net/time_sync.h"
#include "profile_parser.h"
#include "quote_parser.h"
#include "storage/history.h"
#include "symbol_text.h"

#ifndef FINNHUB_API_KEY
#define FINNHUB_API_KEY ""   // normally set from .env by tools/load_env.py
#endif
#ifndef TWELVEDATA_API_KEY
#define TWELVEDATA_API_KEY ""   // optional: history backfill is skipped without it
#endif

static const uint32_t CLOSED_PASS_GAP_MS = 1500;       // between requests while catching up
static const uint32_t CLOSE_PASS_DELAY_MS = 120000;    // let closing prices settle first
static const uint32_t IDLE_CHECK_MS = 5000;            // how often to re-check while idle
static const uint32_t ERROR_RETRY_MS = 10000;
static const uint32_t RATE_LIMIT_BACKOFF_MS = 60000;   // Finnhub's limit is per minute
static const uint32_t BAD_KEY_RETRY_MS = 600000;
static const uint32_t METRICS_RETRY_MS = 600000;       // after a failed stats download
static const uint32_t METRICS_CHECK_MS = 10000;        // how often the main loop checks if stats/names are due
static const uint32_t TASK_STACK_BYTES = 12288;        // HTTPS + JSON streaming need a roomy stack
static const uint32_t TD_GAP_MS = 8000;                // Twelve Data free plan: 8 requests/minute
static const uint32_t HISTORY_RETRY_MS = 600000;       // after a failed history download

// One message from the background task to the main loop.
enum class MsgKind : uint8_t { Quote, NewSession, Metrics, Profile, Earnings, Sector };
struct QuoteMsg {
  MsgKind kind;
  int8_t index;        // symbol number (for Sector: the sector number, see data/sectors.h)
  uint8_t gen;         // the slot's generation when fetched (a changed ticker drops old results)
  bool addToHistory;   // only quotes taken while the market is open become chart points
  uint32_t fetchedAt;  // unix seconds when the quote was fetched: its time on the chart
  int32_t day;         // for Metrics/Profile: day number of the fetch
  Quote quote;
  MetricData metrics;
  Profile profile;
  Earnings earnings;
};

// History is ~800 bytes per stock, so it has its own small queue rather than making
// every quote message that big.
struct HistoryMsg {
  int8_t index;
  uint8_t gen;
  History history;
};

// Gap-fill: 5-minute closes for the session the day chart shows, used to fill holes the
// tracker missed while it was off (start, middle or end of the day).
static const int GAP_BARS = 80;   // a full session is 78 five-minute bars
struct GapMsg {
  int8_t index;
  uint8_t gen;
  uint8_t count;
  uint32_t sessionOpen;       // the session these bars belong to
  uint32_t sessionClose;
  uint32_t times[GAP_BARS];   // bar END times, unix seconds
  float prices[GAP_BARS];
};
static volatile uint32_t gapSessionOpen = 0;    // which session the main loop asked to fill
static volatile uint32_t gapSessionClose = 0;

static QueueHandle_t queue = nullptr;
static QueueHandle_t historyQueue = nullptr;
static QueueHandle_t gapQueue = nullptr;
// "Which stocks need this?" as bits: bit i = stock i. The main loop sets bits, the task
// clears them as each stock is done, so only stocks that are actually missing data (e.g. a
// newly added ticker) are downloaded. Changes go through a lock because the two run on
// different cores and could otherwise overwrite each other's update.
static portMUX_TYPE dueLock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t gapDue = 0;

static void markDue(volatile uint8_t& mask, int stockIndex) {
  taskENTER_CRITICAL(&dueLock);
  mask |= (uint8_t)(1u << stockIndex);
  taskEXIT_CRITICAL(&dueLock);
}

static void clearDue(volatile uint8_t& mask, int stockIndex) {
  taskENTER_CRITICAL(&dueLock);
  mask &= (uint8_t)~(1u << stockIndex);
  taskEXIT_CRITICAL(&dueLock);
}

// Crosses a stock off only if its ticker hasn't changed since the download started:
// otherwise the bit now means "the NEW ticker needs this" and must stay set.
static void clearDueIf(volatile uint8_t& mask, int stockIndex, uint8_t gen) {
  taskENTER_CRITICAL(&dueLock);
  if (symbolGeneration(stockIndex) == gen) mask &= (uint8_t)~(1u << stockIndex);
  taskEXIT_CRITICAL(&dueLock);
}

static bool isDue(uint8_t mask, int stockIndex) {
  return (mask >> stockIndex) & 1u;
}

// The first stock (or symbol, with count = NUM_SYMBOLS) with its bit set, or -1.
static int firstDue(uint8_t mask, int count = cfg::NUM_TICKERS) {
  for (int i = 0; i < count; i++) {
    if (isDue(mask, i)) return i;
  }
  return -1;
}
static volatile FeedStatus status = FeedStatus::Starting;   // written by the task, read by the UI
static volatile uint8_t metricsDue = 0;    // company stats
static volatile uint8_t profilesDue = 0;   // company names
static volatile uint8_t earningsDue = 0;   // earnings dates
static volatile uint8_t historyDue = 0;    // Twelve Data backfill
static volatile bool benchmarkHistoryDue = false;   // the same for SPY (compare charts, 8B)
static volatile uint8_t quoteDue = 0;      // a slot got a new ticker: price it next (all symbols)
static bool recheckRequested = false;      // main loop: re-run the "what's missing" checks now

// Symbol check for the watchlist editor: the main loop puts a symbol here and signals the
// task, which answers through lookupQueue between (or during the pauses of) its other work.
static char lookupSymbol[SYMBOL_MAX_LEN + 1];
static volatile bool lookupPending = false;
static SemaphoreHandle_t lookupSignal = nullptr;
static QueueHandle_t lookupQueue = nullptr;

// The task's connections. File-level (not inside quoteTask) so the lookup can use them.
static WiFiClientSecure tls;     // Finnhub, kept open between requests
static HTTPClient http;
static WiFiClientSecure tdTls;   // Twelve Data
static HTTPClient tdHttp;
static int quoteFailures = 0;              // in a row; QUOTE ERROR shows only after 3

enum class Fetch { Ok, UnknownSymbol, BadKey, RateLimited, Failed };

// ---------- numbers for the Diagnostics screen ----------
// Written by the task, read by the screen. Counters are single 32-bit values (safe to read
// any time); the error text and the request-time ring are changed under dueLock.
static uint32_t finnhubTimes[64];   // millis() of the latest Finnhub requests (ring)
static uint8_t finnhubNext = 0;
static volatile uint32_t finnhubTotal = 0;
static volatile uint32_t tdTotal = 0;
static volatile uint32_t tdToday = 0;
static int32_t tdDay = -1;
static volatile uint32_t lastQuoteOkMs = 0;
static volatile uint32_t lastTdOkMs = 0;
static char lastError[48] = "";
static volatile uint32_t lastErrorMs = 0;
static volatile bool refreshAllRequested = false;

static void noteError(const char* text) {
  taskENTER_CRITICAL(&dueLock);
  strncpy(lastError, text, sizeof(lastError) - 1);
  lastError[sizeof(lastError) - 1] = '\0';
  lastErrorMs = millis();
  taskEXIT_CRITICAL(&dueLock);
}

// http.GET(), counted per service (Finnhub allows 60 a minute, Twelve Data 800 a day).
static int countedGet(HTTPClient& client, bool twelveData) {
  if (twelveData) {
    EtTime now;
    if (timeNowET(now)) {
      int32_t day = dayNumber(now.year, now.month, now.day);
      if (day != tdDay) {   // the daily allowance starts again
        tdDay = day;
        tdToday = 0;
      }
    }
    tdTotal++;
    tdToday++;
  } else {
    taskENTER_CRITICAL(&dueLock);
    finnhubTimes[finnhubNext] = millis();
    finnhubNext = (finnhubNext + 1) % 64;
    taskEXIT_CRITICAL(&dueLock);
    finnhubTotal++;
  }
  return client.GET();
}

static Fetch fromHttpCode(int code, const char* what, const char* symbol) {
  char text[48];
  if (code < 0) snprintf(text, sizeof(text), "%s %s: no connection (%d)", what, symbol, code);
  else snprintf(text, sizeof(text), "%s %s: HTTP %d", what, symbol, code);
  noteError(text);
  if (code == 401 || code == 403) return Fetch::BadKey;
  if (code == 429) return Fetch::RateLimited;
  // Negative codes are connection errors. Free memory helps tell a network problem from
  // running out of RAM for another encrypted connection.
  Serial.printf("quotes: %s %s: request failed (%d), free memory %u bytes\n", what, symbol, code,
                (unsigned)ESP.getFreeHeap());
  return Fetch::Failed;
}

static Fetch fetchQuote(HTTPClient& http, WiFiClientSecure& tls, const char* symbol, Quote& out) {
  String url = String("https://finnhub.io/api/v1/quote?symbol=") + symbol;
  if (!http.begin(tls, url)) return Fetch::Failed;
  // The key travels in a header rather than in the address.
  http.addHeader("X-Finnhub-Token", FINNHUB_API_KEY);

  int code = countedGet(http, false);
  Fetch result;
  if (code == 200) {
    String body = http.getString();
    result = parseQuote(body.c_str(), out) ? Fetch::Ok : Fetch::UnknownSymbol;
    if (result == Fetch::UnknownSymbol) {
      Serial.printf("quotes: %s: no data (unknown symbol?) %s\n", symbol, body.c_str());
    }
  } else {
    result = fromHttpCode(code, "quote", symbol);
  }
  // With setReuse(true), end() keeps the encrypted connection open for the next
  // request, which saves a new TLS handshake (several KB of data) every time.
  http.end();
  if (result == Fetch::Failed) tls.stop();   // start clean after an error
  return result;
}

// Company name and industry: a small (~440 B) reply, so it uses the same kept-open
// connection as the quotes.
static Fetch fetchProfile(HTTPClient& http, WiFiClientSecure& tls, const char* symbol, Profile& out) {
  String url = String("https://finnhub.io/api/v1/stock/profile2?symbol=") + symbol;
  if (!http.begin(tls, url)) return Fetch::Failed;
  http.addHeader("X-Finnhub-Token", FINNHUB_API_KEY);
  int code = countedGet(http, false);
  Fetch result;
  if (code == 200) {
    String body = http.getString();
    result = parseProfile(body.c_str(), out) ? Fetch::Ok : Fetch::UnknownSymbol;
  } else {
    result = fromHttpCode(code, "profile", symbol);
  }
  http.end();
  if (result == Fetch::Failed) tls.stop();
  return result;
}

// The metrics response is ~196 KB, so it's parsed while it streams in, keeping only
// the four fields we need (see metrics_parser.h).
static Fetch fetchMetrics(WiFiClientSecure& tls, const char* symbol, MetricData& out) {
  HTTPClient http;
  // HTTP/1.0 makes the server send the body as one plain stream. With HTTP/1.1 it arrives
  // in "chunks" with size markers mixed into the text, which would confuse the JSON parser.
  // The cost: the connection closes afterwards, so the next quote opens a new one.
  http.useHTTP10(true);
  http.setTimeout(15000);
  tls.stop();   // drop the kept-alive quote connection; this request needs its own

  String url = String("https://finnhub.io/api/v1/stock/metric?metric=all&symbol=") + symbol;
  if (!http.begin(tls, url)) return Fetch::Failed;
  http.addHeader("X-Finnhub-Token", FINNHUB_API_KEY);

  int code = countedGet(http, false);
  Fetch result;
  if (code == 200) {
    JsonDocument filter;
    buildMetricsFilter(filter);
    JsonDocument doc;
    DeserializationError err =
        deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    if (err) {
      Serial.printf("quotes: stats %s: %s\n", symbol, err.c_str());
      result = Fetch::Failed;
    } else {
      result = metricsFromJson(doc, out) ? Fetch::Ok : Fetch::UnknownSymbol;
    }
  } else {
    result = fromHttpCode(code, "stats", symbol);
  }
  http.end();
  tls.stop();
  return result;
}

// Answers a pending symbol check (watchlist editor): quote first (an unknown symbol has
// no price), then the company name (ETFs have none, which is fine).
static void serviceLookup() {
  char symbol[SYMBOL_MAX_LEN + 1];
  taskENTER_CRITICAL(&dueLock);
  bool pending = lookupPending;
  lookupPending = false;
  memcpy(symbol, lookupSymbol, sizeof(symbol));
  taskEXIT_CRITICAL(&dueLock);
  if (!pending) return;

  static SymbolLookup result;   // static: keeps it off the task's stack
  result = SymbolLookup();
  memcpy(result.symbol, symbol, sizeof(symbol));
  Fetch r = fetchQuote(http, tls, symbol, result.quote);
  if (r == Fetch::Ok) {
    result.status = LookupStatus::Found;
    if (fetchProfile(http, tls, symbol, result.profile) != Fetch::Ok) result.profile = Profile();
  } else {
    result.status = r == Fetch::UnknownSymbol ? LookupStatus::NotFound : LookupStatus::Failed;
  }
  Serial.printf("quotes: lookup %s -> %s\n", symbol,
                result.status == LookupStatus::Found      ? "found"
                : result.status == LookupStatus::NotFound ? "not found"
                                                          : "failed");
  xQueueOverwrite(lookupQueue, &result);
}

// Like vTaskDelay, but wakes up to answer a symbol check straight away, so the editor
// doesn't have to wait for a long pause between downloads to end.
static void taskWait(uint32_t ms) {
  uint32_t start = millis();
  for (;;) {
    uint32_t waited = millis() - start;
    if (waited >= ms) return;
    if (xSemaphoreTake(lookupSignal, pdMS_TO_TICKS(ms - waited)) != pdTRUE) return;
    if (WiFi.isConnected()) serviceLookup();
  }
}

static void sendToMainLoop(const QuoteMsg& msg) {
  xQueueSend(queue, &msg, portMAX_DELAY);   // waits if the main loop is behind
}

// Downloads company stats for the stocks that need them (not the index ETFs). Each one
// that succeeds (or that Finnhub has no stats for) is crossed off; the rest are retried later.
static void fetchDueMetrics(WiFiClientSecure& tls, int32_t today) {
  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    if (!isDue(metricsDue, i)) continue;
    QuoteMsg msg{};
    msg.kind = MsgKind::Metrics;
    msg.index = (int8_t)i;
    msg.day = today;
    char symbol[SYMBOL_MAX_LEN + 1];
    msg.gen = symbolSnapshot(i, symbol);
    Fetch r = fetchMetrics(tls, symbol, msg.metrics);
    if (r == Fetch::Ok) sendToMainLoop(msg);
    if (r == Fetch::Ok || r == Fetch::UnknownSymbol) clearDueIf(metricsDue, i, msg.gen);
    taskWait(CLOSED_PASS_GAP_MS);
  }
  Serial.printf("quotes: company stats %s\n", metricsDue == 0 ? "updated" : "incomplete, will retry");
}

// Same for company names (ETFs have no profile).
static void fetchDueProfiles(HTTPClient& http, WiFiClientSecure& tls, int32_t today) {
  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    if (!isDue(profilesDue, i)) continue;
    QuoteMsg msg{};
    msg.kind = MsgKind::Profile;
    msg.index = (int8_t)i;
    msg.day = today;
    char symbol[SYMBOL_MAX_LEN + 1];
    msg.gen = symbolSnapshot(i, symbol);
    Fetch r = fetchProfile(http, tls, symbol, msg.profile);
    if (r == Fetch::Ok) sendToMainLoop(msg);
    if (r == Fetch::Ok || r == Fetch::UnknownSymbol) clearDueIf(profilesDue, i, msg.gen);
    taskWait(CLOSED_PASS_GAP_MS);
  }
  Serial.printf("quotes: company names %s\n", profilesDue == 0 ? "updated" : "incomplete, will retry");
}

// Next earnings report: a small (~185 B) reply looking 120 days ahead, on the kept-open
// Finnhub connection.
static Fetch fetchEarnings(HTTPClient& http, WiFiClientSecure& tls, const char* symbol, int32_t today,
                           Earnings& out) {
  int y1, m1, d1, y2, m2, d2;
  civilFromDay(today, y1, m1, d1);
  civilFromDay(today + 120, y2, m2, d2);
  char range[48];
  snprintf(range, sizeof(range), "&from=%04d-%02d-%02d&to=%04d-%02d-%02d", y1, m1, d1, y2, m2, d2);
  String url = String("https://finnhub.io/api/v1/calendar/earnings?symbol=") + symbol + range;
  if (!http.begin(tls, url)) return Fetch::Failed;
  http.addHeader("X-Finnhub-Token", FINNHUB_API_KEY);
  int code = countedGet(http, false);
  Fetch result;
  if (code == 200) {
    String body = http.getString();
    result = parseEarnings(body.c_str(), today, out) ? Fetch::Ok : Fetch::Failed;
  } else {
    result = fromHttpCode(code, "earnings", symbol);
  }
  http.end();
  if (result == Fetch::Failed) tls.stop();
  return result;
}

static void fetchDueEarnings(HTTPClient& http, WiFiClientSecure& tls, int32_t today) {
  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    if (!isDue(earningsDue, i)) continue;
    QuoteMsg msg{};
    msg.kind = MsgKind::Earnings;
    msg.index = (int8_t)i;
    msg.day = today;
    char symbol[SYMBOL_MAX_LEN + 1];
    msg.gen = symbolSnapshot(i, symbol);
    if (fetchEarnings(http, tls, symbol, today, msg.earnings) == Fetch::Ok) {
      sendToMainLoop(msg);
      clearDueIf(earningsDue, i, msg.gen);
    }
    taskWait(CLOSED_PASS_GAP_MS);
  }
  Serial.println(earningsDue == 0 ? "quotes: earnings dates updated" : "quotes: earnings dates incomplete, will retry");
}

// ---------- sector heat map ----------
// The 11 sector ETFs + SPY are fetched as one "pass", lowest number first, SPY last.
// Bit k of the pending mask = sector k still to fetch in this pass. Only the task uses it.

static const uint16_t ALL_SECTORS = (uint16_t)((1u << NUM_SECTOR_QUOTES) - 1);

static int firstSector(uint16_t pending) {
  for (int k = 0; k < NUM_SECTOR_QUOTES; k++) {
    if ((pending >> k) & 1u) return k;
  }
  return -1;
}

// Fetches sector k and sends it to the main loop. Crossed off on success (or if Finnhub
// doesn't know the symbol); otherwise it stays pending and is retried after a pause.
static void fetchSector(int k, uint16_t& pending, bool open) {
  QuoteMsg msg{};
  msg.kind = MsgKind::Sector;
  msg.index = (int8_t)k;
  msg.addToHistory = open;   // SPY's quote is also a point on the compare chart
  msg.fetchedAt = (uint32_t)time(nullptr);
  Fetch r = fetchQuote(http, tls, sectorSymbol(k), msg.quote);
  if (r == Fetch::Ok) {
    sendToMainLoop(msg);
    lastQuoteOkMs = millis();
  }
  if (r == Fetch::Ok || r == Fetch::UnknownSymbol) {
    pending &= (uint16_t)~(1u << k);
    taskWait(CLOSED_PASS_GAP_MS);
  } else if (r == Fetch::RateLimited) {
    status = FeedStatus::RateLimited;
    taskWait(RATE_LIMIT_BACKOFF_MS);
  } else {
    taskWait(ERROR_RETRY_MS);   // bad key / network: the stock quotes report it
  }
}

// ---------- Twelve Data history backfill ----------

// One CSV request. Twelve Data answers errors as JSON with an HTTP error code.
static Fetch fetchTwelveData(HTTPClient& http, WiFiClientSecure& tls, const char* symbol,
                             const char* interval, int rows, String& body) {
  String url = String("https://api.twelvedata.com/time_series?symbol=") + symbol +
               "&interval=" + interval + "&outputsize=" + rows + "&timezone=UTC&format=CSV";
  if (!http.begin(tls, url)) return Fetch::Failed;
  http.addHeader("Authorization", String("apikey ") + TWELVEDATA_API_KEY);
  int code = countedGet(http, true);
  Fetch result;
  if (code == 200) {
    body = http.getString();
    result = Fetch::Ok;
    lastTdOkMs = millis();
  } else if (code == 400 || code == 404) {
    result = Fetch::UnknownSymbol;
  } else {
    result = fromHttpCode(code, "history", symbol);
  }
  http.end();
  if (result == Fetch::Failed) tls.stop();
  return result;
}

// Daily closes (1M, 1W/1M change) then 30-minute closes (5D) for one stock: two
// requests, spaced to respect the free plan's 8 per minute.
static Fetch fetchHistory(HTTPClient& http, WiFiClientSecure& tls, const char* symbol, int32_t throughDay,
                          History& out) {
  String body;
  Fetch r = fetchTwelveData(http, tls, symbol, "1day", HISTORY_DAYS - 2, body);
  if (r != Fetch::Ok) return r;
  if (!parseDailyCsv(body.c_str(), out)) return Fetch::UnknownSymbol;
  taskWait(TD_GAP_MS);

  r = fetchTwelveData(http, tls, symbol, "30min", HISTORY_BARS, body);
  if (r != Fetch::Ok) return r;
  if (!parseBarsCsv(body.c_str(), out)) return Fetch::UnknownSymbol;
  out.valid = true;
  out.throughDay = throughDay;
  return Fetch::Ok;
}

// Today's 5-minute closes so far, for one stock.
static Fetch fetchGap(HTTPClient& http, WiFiClientSecure& tls, const char* symbol, uint32_t open,
                      uint32_t close, GapMsg& out) {
  out.sessionOpen = open;
  out.sessionClose = close;
  // Bars from 9:30 to now (or to the close, if it's over), plus a little margin. The
  // newest rows come first, and the session we want is the most recent one, so this
  // reaches back exactly far enough (also on weekends, for Friday).
  uint32_t nowUnix = (uint32_t)time(nullptr);
  uint32_t end = nowUnix < close ? nowUnix : close;
  int rows = end > open ? (int)((end - open) / 300) + 2 : 2;
  if (rows > GAP_BARS) rows = GAP_BARS;
  String body;
  Fetch r = fetchTwelveData(http, tls, symbol, "5min", rows, body);
  if (r != Fetch::Ok) return r;
  out.count = parseBarsCsvInto(body.c_str(), out.times, out.prices, GAP_BARS, 300);
  return out.count > 0 ? Fetch::Ok : Fetch::UnknownSymbol;
}

// Runs forever in its own FreeRTOS task.
static void quoteTask(void*) {
  tls.setCACert(FINNHUB_ROOT_CAS);   // only trust Finnhub's real certificate
  http.setReuse(true);
  http.setTimeout(8000);
  // A second connection for Twelve Data, so switching between the two services doesn't
  // tear down the kept-open Finnhub connection. Its certificate chain uses the same roots.
  tdTls.setCACert(FINNHUB_ROOT_CAS);
  tdHttp.setReuse(true);
  tdHttp.setTimeout(10000);

  const uint32_t openGapMs = (uint32_t)cfg::QUOTE_REFRESH_S * 1000 / NUM_SYMBOLS;
  bool fetched[NUM_SYMBOLS] = {};   // progress of the current closed-market pass
  int8_t lastOpen = -1;             // -1 = not known yet (clock not synced)
  uint32_t passAllowedAt = 0;       // a closed-market pass waits until this millis()
  uint32_t metricsRetryAt = 0;
  uint32_t profilesRetryAt = 0;
  uint32_t earningsRetryAt = 0;
  uint32_t historyRetryAt = 0;
  bool historyJustRan = false;      // during the session, alternate history and quotes
  uint16_t sectorPending = ALL_SECTORS;   // the heat map's boot pass
  uint32_t sectorPassAt = 0;        // while open, the next sector pass starts at this millis()
  bool sectorJustRan = false;       // while open, alternate sector and stock quotes
  int next = 0;

  for (;;) {
    if (FINNHUB_API_KEY[0] == '\0') {
      status = FeedStatus::NoKey;
      vTaskDelay(pdMS_TO_TICKS(60000));
      continue;
    }
    if (!WiFi.isConnected()) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    if (lookupPending) {
      xSemaphoreTake(lookupSignal, 0);   // consume the signal; the request is handled here
      serviceLookup();
    }

    EtTime now;
    bool haveTime = timeNowET(now);
    bool open = haveTime && marketStatus(now).phase == MarketPhase::Open;
    if (haveTime) {
      // Only real transitions count. Booting mid-session must not wipe the chart that
      // storage just restored, which is why lastOpen starts as "unknown".
      if (lastOpen == 0 && open) {
        QuoteMsg msg{};
        msg.kind = MsgKind::NewSession;   // a new session starts: clear yesterday's charts
        sendToMainLoop(msg);
      }
      if (lastOpen == 1 && !open) {
        for (bool& f : fetched) f = false;               // fetch final prices once more,
        sectorPending = ALL_SECTORS;                     // (sectors too)
        passAllowedAt = millis() + CLOSE_PASS_DELAY_MS;  // after the closing prints settle
      }
      lastOpen = open ? 1 : 0;
    }

    if (profilesDue && haveTime && (int32_t)(millis() - profilesRetryAt) >= 0) {
      fetchDueProfiles(http, tls, dayNumber(now.year, now.month, now.day));
      if (profilesDue) profilesRetryAt = millis() + METRICS_RETRY_MS;   // some failed: later
      continue;
    }

    if (earningsDue && haveTime && (int32_t)(millis() - earningsRetryAt) >= 0) {
      fetchDueEarnings(http, tls, dayNumber(now.year, now.month, now.day));
      if (earningsDue) earningsRetryAt = millis() + METRICS_RETRY_MS;
      continue;
    }

    if (metricsDue && haveTime && (int32_t)(millis() - metricsRetryAt) >= 0) {
      fetchDueMetrics(tls, dayNumber(now.year, now.month, now.day));
      if (metricsDue) metricsRetryAt = millis() + METRICS_RETRY_MS;
      continue;
    }

    bool historyTurn = !(open && historyJustRan);

    // Morning gap-fill (only asked for during the session): one stock per pass,
    // alternating with quotes like the history backfill below.
    if (gapDue && historyTurn) {
      static GapMsg gmsg;   // static: ~650 bytes
      int i = firstDue(gapDue);
      gmsg.index = (int8_t)i;
      char symbol[SYMBOL_MAX_LEN + 1];
      gmsg.gen = symbolSnapshot(i, symbol);
      Fetch r = fetchGap(tdHttp, tdTls, symbol, gapSessionOpen, gapSessionClose, gmsg);
      if (r == Fetch::Ok) xQueueSend(gapQueue, &gmsg, portMAX_DELAY);
      clearDueIf(gapDue, i, gmsg.gen);   // one try per stock: if it fails, the chart just starts at boot time
      if (gapDue == 0) {
        Serial.println("quotes: filled in today's earlier chart");
        if (!historyDue) tdTls.stop();   // free the Twelve Data connection's memory
      }
      historyJustRan = true;
      taskWait(TD_GAP_MS);
      continue;
    }

    // History backfill: one stock per pass. While the market is open, a quote runs
    // between each stock, so prices keep updating (just more slowly) during the backfill.
    // The stocks first, then SPY (index -1 in the message).
    if ((historyDue || benchmarkHistoryDue) && haveTime && historyTurn && (int32_t)(millis() - historyRetryAt) >= 0) {
      HistoryMsg hmsg{};
      int i = historyDue ? firstDue(historyDue) : -1;
      hmsg.index = (int8_t)i;
      char symbol[SYMBOL_MAX_LEN + 1];
      if (i >= 0) {
        hmsg.gen = symbolSnapshot(i, symbol);
      } else {
        strncpy(symbol, cfg::BENCHMARK_SYMBOL, SYMBOL_MAX_LEN);
        symbol[SYMBOL_MAX_LEN] = '\0';
      }
      Fetch r = fetchHistory(tdHttp, tdTls, symbol, lastCompletedSessionDay(now), hmsg.history);
      if (r == Fetch::Ok) xQueueSend(historyQueue, &hmsg, portMAX_DELAY);
      if (r == Fetch::Ok || r == Fetch::UnknownSymbol) {
        if (i >= 0) clearDueIf(historyDue, i, hmsg.gen);
        else benchmarkHistoryDue = false;
      } else if (r == Fetch::BadKey) {
        Serial.println("quotes: Twelve Data rejected its API key (check .env, then rebuild)");
        historyDue = 0;
        benchmarkHistoryDue = false;
      } else {
        historyRetryAt = millis() + (r == Fetch::RateLimited ? RATE_LIMIT_BACKOFF_MS : HISTORY_RETRY_MS);
      }
      if (historyDue == 0 && !benchmarkHistoryDue) {
        Serial.println("quotes: price history updated");
        if (!gapDue) tdTls.stop();   // free the Twelve Data connection's memory
      }
      historyJustRan = true;
      taskWait(TD_GAP_MS);
      continue;
    }
    historyJustRan = false;

    // A slot just got a new ticker: price it before anything else. (Closed market: it's
    // put back into the pass; open: it jumps the round-robin queue once.)
    int priority = firstDue(quoteDue, NUM_SYMBOLS);
    if (priority >= 0) {
      clearDue(quoteDue, priority);
      fetched[priority] = false;
      if (!open) passAllowedAt = millis();
    }

    // Sector heat map while open: a pass every SECTOR_REFRESH_S, one sector between each
    // stock quote, so the stocks keep their pace. A new ticker's first price goes first.
    if (open && sectorPending == 0 && (int32_t)(millis() - sectorPassAt) >= 0) sectorPending = ALL_SECTORS;
    if (open && priority < 0 && sectorPending && !sectorJustRan) {
      int k = firstSector(sectorPending);
      if (k == 0) sectorPassAt = millis() + cfg::SECTOR_REFRESH_S * 1000;   // a pass starts
      fetchSector(k, sectorPending, true);
      sectorJustRan = true;
      continue;
    }
    sectorJustRan = false;

    if (!open) {
      int i = 0;
      while (i < NUM_SYMBOLS && fetched[i]) i++;
      bool passDone = i == NUM_SYMBOLS;
      bool mustWait = (int32_t)(millis() - passAllowedAt) < 0;   // safe across millis() wrap
      if (passDone && sectorPending && !mustWait) {   // stocks first, then the sectors
        fetchSector(firstSector(sectorPending), sectorPending, false);
        continue;
      }
      if (passDone || mustWait) {
        if (passDone && !sectorPending) status = FeedStatus::Idle;
        taskWait(IDLE_CHECK_MS);
        continue;
      }
      next = i;
    }

    int target = open && priority >= 0 ? priority : next;
    QuoteMsg msg{};
    msg.kind = MsgKind::Quote;
    msg.index = (int8_t)target;
    msg.addToHistory = open;
    msg.fetchedAt = (uint32_t)time(nullptr);
    char symbol[SYMBOL_MAX_LEN + 1];
    msg.gen = symbolSnapshot(target, symbol);
    switch (fetchQuote(http, tls, symbol, msg.quote)) {
      case Fetch::Ok:
        sendToMainLoop(msg);
        lastQuoteOkMs = millis();
        if (status != FeedStatus::Live) Serial.println("quotes: live");
        status = FeedStatus::Live;
        quoteFailures = 0;
        break;
      case Fetch::UnknownSymbol:
        break;   // skip it rather than retrying forever
      case Fetch::BadKey:
        status = FeedStatus::BadKey;
        Serial.println("quotes: Finnhub rejected the API key (check .env, then rebuild)");
        taskWait(BAD_KEY_RETRY_MS);
        continue;
      case Fetch::RateLimited:
        status = FeedStatus::RateLimited;
        Serial.println("quotes: rate limited, pausing 60 s");
        taskWait(RATE_LIMIT_BACKOFF_MS);
        continue;
      case Fetch::Failed:
        // A single hiccup (e.g. while a big download is running) shouldn't flash a red
        // notice; three in a row means something is actually wrong.
        if (++quoteFailures >= 3) status = FeedStatus::NetError;
        if (target != next) markDue(quoteDue, target);   // retry the new ticker first
        taskWait(ERROR_RETRY_MS);
        continue;   // retry the same symbol
    }

    if (open) {
      if (target == next) next = (next + 1) % NUM_SYMBOLS;
      taskWait(openGapMs);
    } else {
      fetched[next] = true;
      taskWait(CLOSED_PASS_GAP_MS);
    }
  }
}

// Main loop: marks each stock whose names / stats / earnings / history are missing or out
// of date, so the task downloads just those. Waits until saved data is restored, so a
// restart doesn't re-download anything. Each kind is checked at most once per day
// (history: once per finished session).
static void requestDataIfDue(uint32_t nowMs) {
  static uint32_t lastCheckMs = 0;
  static int32_t profilesCheckedDay = -1;
  static int32_t earningsCheckedDay = -1;
  static int32_t metricsCheckedDay = -1;
  static int32_t gapCheckedDay = -1;
  static int32_t historyCheckedFor = -1;
  if (refreshAllRequested && storageRestored()) {
    // Settings > Re-download data: everything for every stock, as if nothing was saved.
    refreshAllRequested = false;
    for (int i = 0; i < cfg::NUM_TICKERS; i++) {
      markDue(profilesDue, i);
      markDue(earningsDue, i);
      if (stock(i).hasData) markDue(metricsDue, i);
      if (TWELVEDATA_API_KEY[0] != '\0') markDue(historyDue, i);
    }
    for (int i = 0; i < NUM_SYMBOLS; i++) markDue(quoteDue, i);
    if (TWELVEDATA_API_KEY[0] != '\0') benchmarkHistoryDue = true;
    recheckRequested = true;   // and redo the chart-hole check
    Serial.println("quotes: re-downloading everything (Settings)");
  }
  if (recheckRequested && storageRestored()) {
    // A ticker changed: forget which checks already ran today, so the new one's missing
    // data is found now. Each check only marks stocks that lack something, so the
    // unchanged stocks aren't downloaded again.
    recheckRequested = false;
    profilesCheckedDay = earningsCheckedDay = metricsCheckedDay = gapCheckedDay = historyCheckedFor = -1;
  } else if (nowMs - lastCheckMs < METRICS_CHECK_MS) {
    return;
  }
  lastCheckMs = nowMs;

  EtTime now;
  if (!storageRestored() || !timeNowET(now)) return;
  int32_t today = dayNumber(now.year, now.month, now.day);

  // Names: missing or older than PROFILE_REFRESH_DAYS. They don't depend on prices.
  if (today != profilesCheckedDay) {
    profilesCheckedDay = today;
    for (int i = 0; i < cfg::NUM_TICKERS; i++) {
      const Profile& p = stock(i).profile;
      if (!p.valid || today - p.fetchedDay >= cfg::PROFILE_REFRESH_DAYS) markDue(profilesDue, i);
    }
  }

  // Earnings dates: weekly, and as soon as a stored date has passed (to get the next one).
  if (today != earningsCheckedDay) {
    earningsCheckedDay = today;
    for (int i = 0; i < cfg::NUM_TICKERS; i++) {
      const Earnings& e = stock(i).earnings;
      bool passed = e.valid && e.day != 0 && e.day < today;
      if (!e.valid || passed || today - e.fetchedDay >= cfg::METRICS_REFRESH_DAYS) markDue(earningsDue, i);
    }
  }

  // Day-chart holes: if the chart for the session on screen has a hole of more than 20
  // minutes (the tracker was off), fill it from 5-minute bars. Checked twice per session:
  // once during it (first check after 9:50), and once after it (from 15 min after the
  // close, when the provider has the full day). That also covers the previous session's
  // chart when booting before the open or on a weekend.
  if (TWELVEDATA_API_KEY[0] != '\0' && !gapDue) {
    int32_t session = lastSessionDay(now);   // the session the day chart shows
    uint32_t open, close;
    sessionBoundsForDay(session, open, close);
    uint32_t nowUnix = (uint32_t)time(nullptr);
    bool finished = nowUnix >= close + 15 * 60;
    bool underway = !finished && marketIsOpenNow() && nowUnix > open + 20 * 60;
    int32_t key = session * 2 + (finished ? 1 : 0);   // one check for each of the two moments
    if ((finished || underway) && key != gapCheckedDay) {
      gapCheckedDay = key;
      uint32_t end = finished ? close : nowUnix;
      gapSessionOpen = open;
      gapSessionClose = close;
      for (int i = 0; i < cfg::NUM_TICKERS; i++) {
        if (stock(i).intraday.hasGap(open, end, 20 * 60)) markDue(gapDue, i);
      }
    }
  }

  // History: missing, or doesn't include the last finished session yet (checked again
  // 15 min after each close).
  int32_t completed = lastCompletedSessionDay(now);
  if (TWELVEDATA_API_KEY[0] != '\0' && completed != historyCheckedFor) {
    historyCheckedFor = completed;
    for (int i = 0; i < cfg::NUM_TICKERS; i++) {
      const History& h = stock(i).history;
      if (!h.valid || h.throughDay < completed) markDue(historyDue, i);
    }
    const History& bh = benchmark().history;
    if (!bh.valid || bh.throughDay < completed) benchmarkHistoryDue = true;
  }

  // Stats: need the stock's price first (to work out the share count), so a stock without
  // a price yet is left for a later check today.
  if (today != metricsCheckedDay) {
    bool allPriced = true;
    for (int i = 0; i < cfg::NUM_TICKERS; i++) {
      const SymbolState& s = stock(i);
      if (!s.hasData) {
        allPriced = false;
        continue;
      }
      if (!s.fundamentals.valid || today - s.fundamentals.fetchedDay >= cfg::METRICS_REFRESH_DAYS) {
        markDue(metricsDue, i);
      }
    }
    if (allPriced) metricsCheckedDay = today;
  }
}

void quoteClientStart() {
  queue = xQueueCreate(16, sizeof(QuoteMsg));
  historyQueue = xQueueCreate(2, sizeof(HistoryMsg));
  gapQueue = xQueueCreate(1, sizeof(GapMsg));
  lookupQueue = xQueueCreate(1, sizeof(SymbolLookup));
  lookupSignal = xSemaphoreCreateBinary();
  // Core 0 runs the Wi-Fi driver; the Arduino loop (screen, touch) runs on core 1.
  xTaskCreatePinnedToCore(quoteTask, "quotes", TASK_STACK_BYTES, nullptr, 1, nullptr, 0);
}

static bool spyIsIndexSlot() {
  for (int i = 0; i < cfg::NUM_INDEXES; i++) {
    if (strcmp(indexEtf(i).symbol, cfg::BENCHMARK_SYMBOL) == 0) return true;
  }
  return false;
}

void quoteClientApply(uint32_t nowMs) {
  QuoteMsg msg;
  while (queue && xQueueReceive(queue, &msg, 0) == pdTRUE) {
    if (msg.kind == MsgKind::Sector) {
      sectorsApply(msg.index, msg.quote, msg.fetchedAt);
      if (msg.index == BENCHMARK_INDEX) {
        // When SPY is also an index slot, its 60 s quotes already give the chart points
        // (adding these too would overflow the day's 390-point buffer): numbers only.
        bool points = msg.addToHistory && !spyIsIndexSlot();
        benchmark().applyQuote(msg.quote, nowMs, points, msg.fetchedAt);
      }
      if (msg.index == BENCHMARK_INDEX) storageSaveSectors();   // SPY comes last: a pass is complete
      continue;
    }
    // Fetched for a ticker that has since been replaced in that slot: ignore it.
    if (msg.kind != MsgKind::NewSession && msg.gen != symbolGeneration(msg.index)) continue;
    switch (msg.kind) {
      case MsgKind::NewSession:
        for (int i = 0; i < NUM_SYMBOLS; i++) {
          symbolByIndex(i).intraday.clear();
          symbolByIndex(i).version++;   // so the screens redraw the now-empty charts
        }
        benchmark().intraday.clear();
        benchmark().version++;
        break;
      case MsgKind::Quote: {
        SymbolState& s = symbolByIndex(msg.index);
        s.applyQuote(msg.quote, nowMs, msg.addToHistory, msg.fetchedAt);
        // SPY as an index slot: its minute-by-minute quotes also feed the compare charts.
        if (msg.index >= cfg::NUM_TICKERS && strcmp(s.symbol, cfg::BENCHMARK_SYMBOL) == 0) {
          benchmark().applyQuote(msg.quote, nowMs, msg.addToHistory, msg.fetchedAt);
        }
        break;
      }
      case MsgKind::Metrics:
        symbolByIndex(msg.index).applyMetrics(msg.metrics, msg.day);
        storageSaveFundamentals();
        break;
      case MsgKind::Profile:
        symbolByIndex(msg.index).applyProfile(msg.profile, msg.day);
        storageSaveProfiles();
        break;
      case MsgKind::Earnings: {
        SymbolState& s = symbolByIndex(msg.index);
        s.earnings = msg.earnings;
        s.earnings.valid = true;
        s.earnings.fetchedDay = msg.day;
        s.version++;
        storageSaveEarnings();
        break;
      }
      case MsgKind::Sector:
        break;   // handled above
    }
  }
  // static: a HistoryMsg is ~800 bytes, too big to put on the loop's stack every pass
  static HistoryMsg hmsg;
  while (historyQueue && xQueueReceive(historyQueue, &hmsg, 0) == pdTRUE) {
    if (hmsg.index >= 0 && hmsg.gen != symbolGeneration(hmsg.index)) continue;
    SymbolState& s = hmsg.index >= 0 ? symbolByIndex(hmsg.index) : benchmark();
    s.history = hmsg.history;
    s.version++;
    storageSaveHistory();
  }
  static GapMsg gmsg;
  bool filled = false;
  while (gapQueue && xQueueReceive(gapQueue, &gmsg, 0) == pdTRUE) {
    if (gmsg.gen != symbolGeneration(gmsg.index)) continue;
    SymbolState& s = symbolByIndex(gmsg.index);
    // Only that session's bars, and none within 2 min of a live point (live points win).
    s.intraday.fillGaps(gmsg.times, gmsg.prices, gmsg.count, gmsg.sessionOpen, gmsg.sessionClose, 120);
    s.version++;
    filled = true;
  }
  if (filled) storageSaveCharts();   // so a restart doesn't download the fill again
  requestDataIfDue(nowMs);
}

FeedStatus quoteFeedStatus() {
  return status;
}

const char* quoteFeedProblem() {
  switch (status) {
    case FeedStatus::NoKey:       return "NO API KEY";
    case FeedStatus::BadKey:      return "API KEY REJECTED";
    case FeedStatus::RateLimited: return "RATE LIMITED";
    case FeedStatus::NetError:    return "QUOTE ERROR";
    default:                      return nullptr;
  }
}

void quoteClientSymbolChanged(int i) {
  markDue(quoteDue, i);
  recheckRequested = true;
}

void quoteClientLookup(const char* symbol) {
  if (!lookupSignal) return;
  taskENTER_CRITICAL(&dueLock);
  strncpy(lookupSymbol, symbol, SYMBOL_MAX_LEN);
  lookupSymbol[SYMBOL_MAX_LEN] = '\0';
  lookupPending = true;
  taskEXIT_CRITICAL(&dueLock);
  xSemaphoreGive(lookupSignal);
}

bool quoteClientLookupResult(SymbolLookup& out) {
  return lookupQueue && xQueueReceive(lookupQueue, &out, 0) == pdTRUE;
}

void quoteClientRefreshAll() {
  refreshAllRequested = true;
}

void quoteClientStats(FeedStats& out) {
  uint32_t now = millis();
  out.lastQuoteOkMs = lastQuoteOkMs;
  out.lastTdOkMs = lastTdOkMs;
  out.failuresInRow = quoteFailures;
  out.finnhubTotal = finnhubTotal;
  out.tdTotal = tdTotal;
  out.tdToday = tdToday;
  out.profilesDue = profilesDue;
  out.earningsDue = earningsDue;
  out.metricsDue = metricsDue;
  out.historyDue = historyDue;
  out.gapDue = gapDue;
  out.quoteDue = quoteDue;
  out.finnhubLastMinute = 0;
  taskENTER_CRITICAL(&dueLock);
  for (int i = 0; i < 64; i++) {
    if (finnhubTimes[i] != 0 && now - finnhubTimes[i] < 60000) out.finnhubLastMinute++;
  }
  memcpy(out.lastError, lastError, sizeof(out.lastError));
  out.lastErrorMs = lastErrorMs;
  taskEXIT_CRITICAL(&dueLock);
}
