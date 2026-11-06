// Reads the few fields we use from Finnhub's /stock/metric response.
//
// That response is ~196 KB (mostly historical series we don't need), far too big to
// hold in memory. ArduinoJson's "filter" keeps only the listed fields while the text
// streams past, so parsing needs about 1 KB of RAM.
#pragma once
#include <ArduinoJson.h>
#include "symbol_state.h"

// Fills `filter` with the fields to keep: metric.52WeekHigh, 52WeekLow, epsTTM,
// marketCapitalization.
void buildMetricsFilter(JsonDocument& filter);

// Reads a (filtered) document. False if the 52-week high is missing, which is what
// Finnhub returns for symbols it has no metrics for.
bool metricsFromJson(const JsonDocument& doc, MetricData& out);

// Convenience for tests: filter + parse a complete JSON string.
bool parseMetrics(const char* json, MetricData& out);
