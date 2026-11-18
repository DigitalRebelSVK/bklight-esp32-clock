#include "calendar.h"

#include <stdio.h>
#include <stdlib.h>

#include <algorithm>

#include "text.h"

long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468;
}

namespace {

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

bool parseLine(const std::string& raw, CalEvent& ev) {
  std::string line = trim(raw);
  if (line.size() < 12) return false;
  int y, m, d;
  if (sscanf(line.c_str(), "%4d-%2d-%2d", &y, &m, &d) != 3) return false;
  if (m < 1 || m > 12 || d < 1 || d > 31) return false;
  std::string rest = trim(line.substr(10));
  if (rest.empty()) return false;
  ev.y = y; ev.m = m; ev.d = d;
  if (rest[0] == '*') {
    ev.minute = -1;
    rest = trim(rest.substr(1));
  } else {
    int hh, mm;
    if (sscanf(rest.c_str(), "%d:%d", &hh, &mm) != 2) return false;
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
    ev.minute = hh * 60 + mm;
    size_t sp = rest.find(' ');
    rest = sp == std::string::npos ? "" : trim(rest.substr(sp));
  }
  ev.title = text::truncate(rest.empty() ? "?" : rest, 60);
  return true;
}

}  // namespace

int Calendar::setFromText(const std::string& body) {
  std::vector<CalEvent> parsed;
  size_t start = 0;
  while (start <= body.size()) {
    size_t nl = body.find('\n', start);
    std::string line = body.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
    CalEvent ev;
    if (parseLine(line, ev) && (int)parsed.size() < kMaxEvents) parsed.push_back(ev);
    if (nl == std::string::npos) break;
    start = nl + 1;
  }
  events_ = parsed;
  return (int)parsed.size();
}

void Calendar::setEvents(const std::vector<CalEvent>& events) {
  events_ = events;
  if ((int)events_.size() > kMaxEvents) events_.resize(kMaxEvents);
}

std::vector<CalEvent> Calendar::upcoming(const Today& now, int graceMin, int tomorrowFromHour) const {
  long today = daysFromCivil(now.y, now.m, now.d);
  std::vector<CalEvent> out;
  for (const auto& e : events_) {
    long day = daysFromCivil(e.y, e.m, e.d);
    if (day == today) {
      if (e.minute < 0 || e.minute + graceMin >= now.minute) out.push_back(e);
    } else if (day == today + 1 && now.minute >= tomorrowFromHour * 60) {
      out.push_back(e);
    }
  }
  std::stable_sort(out.begin(), out.end(), [](const CalEvent& a, const CalEvent& b) {
    long da = daysFromCivil(a.y, a.m, a.d), db = daysFromCivil(b.y, b.m, b.d);
    if (da != db) return da < db;
    return a.minute < b.minute;  // all-day (-1) first
  });
  return out;
}

std::string Calendar::label(const CalEvent& e, const Today& now, const char* today,
                            const char* tomorrow) {
  bool isTomorrow = daysFromCivil(e.y, e.m, e.d) != daysFromCivil(now.y, now.m, now.d);
  char buf[40];
  if (e.minute < 0) {
    snprintf(buf, sizeof(buf), "%s", isTomorrow ? tomorrow : today);
  } else if (isTomorrow) {
    snprintf(buf, sizeof(buf), "%s %d:%02d", tomorrow, e.minute / 60, e.minute % 60);
  } else {
    snprintf(buf, sizeof(buf), "%d:%02d", e.minute / 60, e.minute % 60);
  }
  return buf;
}
