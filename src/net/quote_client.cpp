#include "net/quote_client.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "config.h"
#include "data/market_data.h"
#include "market_calendar.h"
#include "metrics_parser.h"
#include "net/finnhub_ca.h"
#include "net/time_sync.h"
#include "profile_parser.h"
#include "quote_parser.h"
#include "storage/history.h"

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
enum class MsgKind : uint8_t { Quote, NewSession, Metrics, Profile };
struct QuoteMsg {
  MsgKind kind;
  int8_t index;        // symbol number
  bool addToHistory;   // only quotes taken while the market is open become chart points
  int32_t day;         // for Metrics/Profile: day number of the fetch
  Quote quote;
  MetricData metrics;
  Profile profile;
};

// History is ~800 bytes per stock, so it has its own small queue rather than making
// every quote message that big.
struct HistoryMsg {
  int8_t index;
  History history;
};

static QueueHandle_t queue = nullptr;
static QueueHandle_t historyQueue = nullptr;
static volatile FeedStatus status = FeedStatus::Starting;   // written by the task, read by the UI
static volatile bool metricsRequested = false;              // set by the main loop, cleared by the task
static volatile bool profilesRequested = false;             // same, for company names
static volatile bool historyRequested = false;              // same, for the Twelve Data backfill

enum class Fetch { Ok, UnknownSymbol, BadKey, RateLimited, Failed };

static Fetch fromHttpCode(int code, const char* what, const char* symbol) {
  if (code == 401 || code == 403) return Fetch::BadKey;
  if (code == 429) return Fetch::RateLimited;
  Serial.printf("quotes: %s %s: request failed (%d)\n", what, symbol, code);   // negative = connection error
  return Fetch::Failed;
}

static Fetch fetchQuote(HTTPClient& http, WiFiClientSecure& tls, const char* symbol, Quote& out) {
  String url = String("https://finnhub.io/api/v1/quote?symbol=") + symbol;
  if (!http.begin(tls, url)) return Fetch::Failed;
  // The key travels in a header rather than in the address.
  http.addHeader("X-Finnhub-Token", FINNHUB_API_KEY);

  int code = http.GET();
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
  int code = http.GET();
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

  int code = http.GET();
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

static void sendToMainLoop(const QuoteMsg& msg) {
  xQueueSend(queue, &msg, portMAX_DELAY);   // waits if the main loop is behind
}

// Downloads company stats for every stock (not the index ETFs). Returns false if any
// failed, so the task can try again later.
static bool fetchAllMetrics(WiFiClientSecure& tls, int32_t today) {
  bool allOk = true;
  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    QuoteMsg msg{};
    msg.kind = MsgKind::Metrics;
    msg.index = (int8_t)i;
    msg.day = today;
    Fetch r = fetchMetrics(tls, symbolByIndex(i).symbol, msg.metrics);
    if (r == Fetch::Ok) sendToMainLoop(msg);
    else if (r != Fetch::UnknownSymbol) allOk = false;
    vTaskDelay(pdMS_TO_TICKS(CLOSED_PASS_GAP_MS));
  }
  Serial.printf("quotes: company stats %s\n", allOk ? "updated" : "incomplete, will retry");
  return allOk;
}

// Downloads the company name for every stock (ETFs have no profile). Returns false if
// any failed, so the task can try again later.
static bool fetchAllProfiles(HTTPClient& http, WiFiClientSecure& tls, int32_t today) {
  bool allOk = true;
  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    QuoteMsg msg{};
    msg.kind = MsgKind::Profile;
    msg.index = (int8_t)i;
    msg.day = today;
    Fetch r = fetchProfile(http, tls, symbolByIndex(i).symbol, msg.profile);
    if (r == Fetch::Ok) sendToMainLoop(msg);
    else if (r != Fetch::UnknownSymbol) allOk = false;
    vTaskDelay(pdMS_TO_TICKS(CLOSED_PASS_GAP_MS));
  }
  Serial.printf("quotes: company names %s\n", allOk ? "updated" : "incomplete, will retry");
  return allOk;
}

// ---------- Twelve Data history backfill ----------

// One CSV request. Twelve Data answers errors as JSON with an HTTP error code.
static Fetch fetchTwelveData(HTTPClient& http, WiFiClientSecure& tls, const char* symbol,
                             const char* interval, int rows, String& body) {
  String url = String("https://api.twelvedata.com/time_series?symbol=") + symbol +
               "&interval=" + interval + "&outputsize=" + rows + "&timezone=UTC&format=CSV";
  if (!http.begin(tls, url)) return Fetch::Failed;
  http.addHeader("Authorization", String("apikey ") + TWELVEDATA_API_KEY);
  int code = http.GET();
  Fetch result;
  if (code == 200) {
    body = http.getString();
    result = Fetch::Ok;
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
  vTaskDelay(pdMS_TO_TICKS(TD_GAP_MS));

  r = fetchTwelveData(http, tls, symbol, "30min", HISTORY_BARS, body);
  if (r != Fetch::Ok) return r;
  if (!parseBarsCsv(body.c_str(), out)) return Fetch::UnknownSymbol;
  out.valid = true;
  out.throughDay = throughDay;
  return Fetch::Ok;
}

// Runs forever in its own FreeRTOS task.
static void quoteTask(void*) {
  // static: these objects are large, so keep them off this task's stack
  static WiFiClientSecure tls;
  static HTTPClient http;
  tls.setCACert(FINNHUB_ROOT_CAS);   // only trust Finnhub's real certificate
  http.setReuse(true);
  http.setTimeout(8000);
  // A second connection for Twelve Data, so switching between the two services doesn't
  // tear down the kept-open Finnhub connection. Its certificate chain uses the same roots.
  static WiFiClientSecure tdTls;
  static HTTPClient tdHttp;
  tdTls.setCACert(FINNHUB_ROOT_CAS);
  tdHttp.setReuse(true);
  tdHttp.setTimeout(10000);

  const uint32_t openGapMs = (uint32_t)cfg::QUOTE_REFRESH_S * 1000 / NUM_SYMBOLS;
  bool fetched[NUM_SYMBOLS] = {};   // progress of the current closed-market pass
  int8_t lastOpen = -1;             // -1 = not known yet (clock not synced)
  uint32_t passAllowedAt = 0;       // a closed-market pass waits until this millis()
  uint32_t metricsRetryAt = 0;
  uint32_t profilesRetryAt = 0;
  uint32_t historyRetryAt = 0;
  int historyNext = 0;              // which stock the backfill does next
  bool historyJustRan = false;      // during the session, alternate history and quotes
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
        passAllowedAt = millis() + CLOSE_PASS_DELAY_MS;  // after the closing prints settle
      }
      lastOpen = open ? 1 : 0;
    }

    if (profilesRequested && haveTime && (int32_t)(millis() - profilesRetryAt) >= 0) {
      if (fetchAllProfiles(http, tls, dayNumber(now.year, now.month, now.day))) {
        profilesRequested = false;
      } else {
        profilesRetryAt = millis() + METRICS_RETRY_MS;
      }
      continue;
    }

    if (metricsRequested && haveTime && (int32_t)(millis() - metricsRetryAt) >= 0) {
      if (fetchAllMetrics(tls, dayNumber(now.year, now.month, now.day))) {
        metricsRequested = false;
      } else {
        metricsRetryAt = millis() + METRICS_RETRY_MS;
      }
      continue;
    }

    // History backfill: one stock per pass. While the market is open, a quote runs
    // between each stock, so prices keep updating (just more slowly) during the backfill.
    bool historyTurn = !(open && historyJustRan);
    if (historyRequested && haveTime && historyTurn && (int32_t)(millis() - historyRetryAt) >= 0) {
      HistoryMsg hmsg{};
      hmsg.index = (int8_t)historyNext;
      const char* symbol = symbolByIndex(historyNext).symbol;
      Fetch r = fetchHistory(tdHttp, tdTls, symbol, lastCompletedSessionDay(now), hmsg.history);
      if (r == Fetch::Ok) xQueueSend(historyQueue, &hmsg, portMAX_DELAY);
      if (r == Fetch::Ok || r == Fetch::UnknownSymbol) {
        historyNext++;
      } else if (r == Fetch::BadKey) {
        Serial.println("quotes: Twelve Data rejected its API key (check .env, then rebuild)");
        historyRequested = false;
      } else {
        historyRetryAt = millis() + (r == Fetch::RateLimited ? RATE_LIMIT_BACKOFF_MS : HISTORY_RETRY_MS);
      }
      if (historyNext >= cfg::NUM_TICKERS) {
        historyNext = 0;
        historyRequested = false;
        Serial.println("quotes: price history updated");
      }
      historyJustRan = true;
      vTaskDelay(pdMS_TO_TICKS(TD_GAP_MS));
      continue;
    }
    historyJustRan = false;

    if (!open) {
      int i = 0;
      while (i < NUM_SYMBOLS && fetched[i]) i++;
      bool passDone = i == NUM_SYMBOLS;
      bool mustWait = (int32_t)(millis() - passAllowedAt) < 0;   // safe across millis() wrap
      if (passDone || mustWait) {
        if (passDone) status = FeedStatus::Idle;
        vTaskDelay(pdMS_TO_TICKS(IDLE_CHECK_MS));
        continue;
      }
      next = i;
    }

    QuoteMsg msg{};
    msg.kind = MsgKind::Quote;
    msg.index = (int8_t)next;
    msg.addToHistory = open;
    switch (fetchQuote(http, tls, symbolByIndex(next).symbol, msg.quote)) {
      case Fetch::Ok:
        sendToMainLoop(msg);
        if (status != FeedStatus::Live) Serial.println("quotes: live");
        status = FeedStatus::Live;
        break;
      case Fetch::UnknownSymbol:
        break;   // skip it rather than retrying forever
      case Fetch::BadKey:
        status = FeedStatus::BadKey;
        Serial.println("quotes: Finnhub rejected the API key (check .env, then rebuild)");
        vTaskDelay(pdMS_TO_TICKS(BAD_KEY_RETRY_MS));
        continue;
      case Fetch::RateLimited:
        status = FeedStatus::RateLimited;
        Serial.println("quotes: rate limited, pausing 60 s");
        vTaskDelay(pdMS_TO_TICKS(RATE_LIMIT_BACKOFF_MS));
        continue;
      case Fetch::Failed:
        status = FeedStatus::NetError;
        vTaskDelay(pdMS_TO_TICKS(ERROR_RETRY_MS));
        continue;   // retry the same symbol
    }

    if (open) {
      next = (next + 1) % NUM_SYMBOLS;
      vTaskDelay(pdMS_TO_TICKS(openGapMs));
    } else {
      fetched[next] = true;
      vTaskDelay(pdMS_TO_TICKS(CLOSED_PASS_GAP_MS));
    }
  }
}

// Main loop: asks the task for company names and stats when they're missing or older
// than cfg::PROFILE_REFRESH_DAYS / cfg::METRICS_REFRESH_DAYS. Waits until saved data is
// restored, so a restart doesn't re-download it.
static void requestDataIfDue(uint32_t nowMs) {
  static uint32_t lastCheckMs = 0;
  static int32_t profilesRequestedDay = -1;
  static int32_t metricsRequestedDay = -1;
  if (nowMs - lastCheckMs < METRICS_CHECK_MS) return;
  lastCheckMs = nowMs;

  EtTime now;
  if (!storageRestored() || !timeNowET(now)) return;
  int32_t today = dayNumber(now.year, now.month, now.day);

  // Names don't depend on prices, so they can be requested straight away.
  if (today != profilesRequestedDay) {
    for (int i = 0; i < cfg::NUM_TICKERS; i++) {
      const Profile& p = stock(i).profile;
      if (!p.valid || today - p.fetchedDay >= cfg::PROFILE_REFRESH_DAYS) {
        profilesRequested = true;
        profilesRequestedDay = today;
        break;
      }
    }
  }

  // History: missing, or doesn't include the last finished session yet (re-checked
  // 15 min after each close). Asked for at most once per finished session.
  static int32_t historyRequestedFor = -1;
  int32_t completed = lastCompletedSessionDay(now);
  if (TWELVEDATA_API_KEY[0] != '\0' && completed != historyRequestedFor) {
    for (int i = 0; i < cfg::NUM_TICKERS; i++) {
      const History& h = stock(i).history;
      if (!h.valid || h.throughDay < completed) {
        historyRequested = true;
        historyRequestedFor = completed;
        break;
      }
    }
  }

  // Stats need every stock's price first (to work out the share count).
  if (today == metricsRequestedDay) return;
  bool due = false;
  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    const SymbolState& s = stock(i);
    if (!s.hasData) return;
    if (!s.fundamentals.valid || today - s.fundamentals.fetchedDay >= cfg::METRICS_REFRESH_DAYS) due = true;
  }
  if (due) {
    metricsRequested = true;
    metricsRequestedDay = today;
  }
}

void quoteClientStart() {
  queue = xQueueCreate(16, sizeof(QuoteMsg));
  historyQueue = xQueueCreate(2, sizeof(HistoryMsg));
  // Core 0 runs the Wi-Fi driver; the Arduino loop (screen, touch) runs on core 1.
  xTaskCreatePinnedToCore(quoteTask, "quotes", TASK_STACK_BYTES, nullptr, 1, nullptr, 0);
}

void quoteClientApply(uint32_t nowMs) {
  QuoteMsg msg;
  while (queue && xQueueReceive(queue, &msg, 0) == pdTRUE) {
    switch (msg.kind) {
      case MsgKind::NewSession:
        for (int i = 0; i < NUM_SYMBOLS; i++) {
          symbolByIndex(i).intraday.clear();
          symbolByIndex(i).version++;   // so the screens redraw the now-empty charts
        }
        break;
      case MsgKind::Quote:
        symbolByIndex(msg.index).applyQuote(msg.quote, nowMs, msg.addToHistory);
        break;
      case MsgKind::Metrics:
        symbolByIndex(msg.index).applyMetrics(msg.metrics, msg.day);
        storageSaveFundamentals();
        break;
      case MsgKind::Profile:
        symbolByIndex(msg.index).applyProfile(msg.profile, msg.day);
        storageSaveProfiles();
        break;
    }
  }
  // static: a HistoryMsg is ~800 bytes, too big to put on the loop's stack every pass
  static HistoryMsg hmsg;
  while (historyQueue && xQueueReceive(historyQueue, &hmsg, 0) == pdTRUE) {
    SymbolState& s = symbolByIndex(hmsg.index);
    s.history = hmsg.history;
    s.version++;
    storageSaveHistory();
  }
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
