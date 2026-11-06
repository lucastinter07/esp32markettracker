#include "metrics_parser.h"

void buildMetricsFilter(JsonDocument& filter) {
  filter["metric"]["52WeekHigh"] = true;
  filter["metric"]["52WeekLow"] = true;
  filter["metric"]["epsTTM"] = true;
  filter["metric"]["marketCapitalization"] = true;
}

bool metricsFromJson(const JsonDocument& doc, MetricData& out) {
  JsonObjectConst m = doc["metric"];
  float high = m["52WeekHigh"] | 0.0f;
  if (high <= 0) return false;
  out.high52 = high;
  out.low52 = m["52WeekLow"] | 0.0f;
  out.epsTTM = m["epsTTM"] | 0.0f;                 // can be null or negative (losses)
  out.marketCapM = m["marketCapitalization"] | 0.0f;
  return true;
}

bool parseMetrics(const char* json, MetricData& out) {
  JsonDocument filter;
  buildMetricsFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, json, DeserializationOption::Filter(filter))) return false;
  return metricsFromJson(doc, out);
}
