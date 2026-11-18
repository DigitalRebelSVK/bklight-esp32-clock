#include "ics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "text.h"

namespace {

constexpr size_t kMaxLine = 1200;  // longer lines (DESCRIPTION...) are cut; we never need them

struct Civil {
  int y, m, d;
};

Civil civilFromDays(long z) {
  z += 719468;
  long era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = (unsigned)(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long y = (long)yoe + era * 400;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  unsigned d = doy - (153 * mp + 2) / 5 + 1;
  unsigned m = mp < 10 ? mp + 3 : mp - 9;
  return {(int)(y + (m <= 2)), (int)m, (int)d};
}

int weekday(long days) { return (int)(((days + 3) % 7 + 7) % 7); }  // 0 = Monday

int daysInMonth(int y, int m) {
  static const int k[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return m == 2 && leap ? 29 : k[m - 1];
}

int dayCode(const char* s) {
  static const char* const k[7] = {"MO", "TU", "WE", "TH", "FR", "SA", "SU"};
  for (int i = 0; i < 7; ++i)
    if (s[0] == k[i][0] && s[1] == k[i][1]) return i;
  return -1;
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t start = 0;
  while (true) {
    size_t p = s.find(sep, start);
    out.push_back(s.substr(start, p == std::string::npos ? std::string::npos : p - start));
    if (p == std::string::npos) break;
    start = p + 1;
  }
  return out;
}

std::string upper(std::string s) {
  for (char& c : s)
    if (c >= 'a' && c <= 'z') c -= 32;
  return s;
}

std::string unescape(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char n = s[++i];
      out += (n == 'n' || n == 'N') ? ' ' : n;
    } else {
      out += s[i];
    }
  }
  size_t a = out.find_first_not_of(' ');
  size_t b = out.find_last_not_of(' ');
  return a == std::string::npos ? "" : out.substr(a, b - a + 1);
}

// "2" -> 2, "-1" -> -1; returns false if it isn't a whole number.
bool toInt(const std::string& s, int* v) {
  if (s.empty()) return false;
  char* end = nullptr;
  long x = strtol(s.c_str(), &end, 10);
  if (*end) return false;
  *v = (int)x;
  return true;
}

}  // namespace

IcsReader::IcsReader(long todayDays, int days, UtcToLocal utcToLocal)
    : today_(todayDays), days_(days), utcToLocal_(utcToLocal) {}

void IcsReader::feed(const char* data, size_t len) {
  bytes_ += len;
  for (size_t i = 0; i < len; ++i) {
    char c = data[i];
    if (c == '\n') {
      if (!phys_.empty() && phys_.back() == '\r') phys_.pop_back();
      if (!phys_.empty() && (phys_[0] == ' ' || phys_[0] == '\t')) {
        if (cur_.size() < kMaxLine) cur_.append(phys_, 1, std::string::npos);  // folded continuation
      } else {
        if (!cur_.empty()) line(cur_);
        cur_ = phys_;
      }
      phys_.clear();
    } else if (phys_.size() < kMaxLine) {
      phys_ += c;
    }
  }
}

std::vector<CalEvent> IcsReader::finish(size_t maxEvents) {
  if (!phys_.empty()) feed("\n", 1);
  if (!cur_.empty()) {
    line(cur_);
    cur_.clear();
  }

  std::vector<Candidate> keep;
  for (const Candidate& c : found_) {
    bool moved = false;
    if (c.fromRule)
      for (const auto& o : overridden_)
        if (o.first == c.uid && o.second == c.day) moved = true;
    bool dup = false;
    for (const Candidate& k : keep)
      if (k.day == c.day && k.ev.minute == c.ev.minute && k.ev.title == c.ev.title) dup = true;
    if (!moved && !dup) keep.push_back(c);
  }
  std::stable_sort(keep.begin(), keep.end(), [](const Candidate& a, const Candidate& b) {
    if (a.day != b.day) return a.day < b.day;
    return a.ev.minute < b.ev.minute;
  });
  std::vector<CalEvent> out;
  for (const Candidate& c : keep) {
    if (out.size() >= maxEvents) break;
    out.push_back(c.ev);
  }
  return out;
}

void IcsReader::line(const std::string& l) {
  // NAME[;PARAM=..;PARAM="a:b"]:VALUE  - the first ':' outside quotes splits.
  bool quoted = false;
  size_t colon = std::string::npos;
  for (size_t i = 0; i < l.size(); ++i) {
    if (l[i] == '"') quoted = !quoted;
    if (l[i] == ':' && !quoted) {
      colon = i;
      break;
    }
  }
  if (colon == std::string::npos) return;
  std::string head = l.substr(0, colon);
  std::string value = l.substr(colon + 1);
  size_t semi = head.find(';');
  std::string name = upper(head.substr(0, semi));
  std::string params = semi == std::string::npos ? "" : upper(head.substr(semi + 1));

  if (name == "BEGIN") {
    std::string what = upper(value);
    if (depth_ == 0 && what == "VEVENT") {
      depth_ = 1;
      ev_ = Event();
      ++vevents_;
    } else if (depth_ >= 1) {
      ++depth_;  // VALARM etc.
    }
    return;
  }
  if (name == "END") {
    if (depth_ == 1 && upper(value) == "VEVENT") {
      endEvent();
      depth_ = 0;
    } else if (depth_ > 1) {
      --depth_;
    }
    return;
  }
  if (depth_ == 1) property(name, params, value);
}

IcsReader::DateVal IcsReader::parseDate(const std::string& params, const std::string& value) const {
  DateVal d;
  int y, mo, da, h = 0, mi = 0, s = 0;
  if (value.size() < 8 || sscanf(value.c_str(), "%4d%2d%2d", &y, &mo, &da) != 3) return d;
  if (mo < 1 || mo > 12 || da < 1 || da > 31) return d;
  long days = daysFromCivil(y, mo, da);
  bool dateOnly = params.find("VALUE=DATE") != std::string::npos && params.find("VALUE=DATE-TIME") == std::string::npos;
  if (dateOnly || value.size() < 15 || value[8] != 'T') {
    d.valid = true;
    d.allDay = true;
    d.days = days;
    d.minute = -1;
    return d;
  }
  if (sscanf(value.c_str() + 9, "%2d%2d%2d", &h, &mi, &s) < 2) return d;
  d.valid = true;
  if (value.size() >= 16 && value[15] == 'Z' && utcToLocal_) {
    long long epoch = (long long)days * 86400 + h * 3600 + mi * 60 + s;
    utcToLocal_(epoch, &d.days, &d.minute);
  } else {
    // TZID=... or floating: taken as the clock's local time
    d.days = days;
    d.minute = h * 60 + mi;
  }
  return d;
}

void IcsReader::parseRule(const std::string& value, Rule& r) const {
  r = Rule();
  r.present = true;
  for (const std::string& part : split(upper(value), ';')) {
    size_t eq = part.find('=');
    if (eq == std::string::npos) continue;
    std::string k = part.substr(0, eq), v = part.substr(eq + 1);
    int n;
    if (k == "FREQ") {
      r.freq = v == "DAILY" ? 1 : v == "WEEKLY" ? 2 : v == "MONTHLY" ? 3 : v == "YEARLY" ? 4 : 0;
    } else if (k == "INTERVAL" && toInt(v, &n) && n > 0) {
      r.interval = n;
    } else if (k == "COUNT" && toInt(v, &n) && n > 0) {
      r.count = n;
    } else if (k == "UNTIL") {
      DateVal u = parseDate("", v);
      if (u.valid) {
        r.untilDays = u.days;
        r.hasUntil = true;
      }
    } else if (k == "WKST" && v.size() >= 2) {
      int w = dayCode(v.c_str());
      if (w >= 0) r.wkst = w;
    } else if (k == "BYDAY") {
      for (const std::string& d : split(v, ',')) {
        if (d.size() < 2) continue;
        int w = dayCode(d.c_str() + d.size() - 2);
        int ord = 0;
        if (d.size() > 2) toInt(d.substr(0, d.size() - 2), &ord);
        if (w >= 0) r.byDay.push_back({ord, w});
      }
    } else if (k == "BYMONTHDAY") {
      for (const std::string& d : split(v, ','))
        if (toInt(d, &n)) r.byMonthDay.push_back(n);
    } else if (k == "BYMONTH") {
      for (const std::string& d : split(v, ','))
        if (toInt(d, &n)) r.byMonth.push_back(n);
    }
  }
  if (!r.freq) r.present = false;  // HOURLY etc.: treat as a single event
}

void IcsReader::property(const std::string& name, const std::string& params, const std::string& value) {
  if (name == "UID") {
    ev_.uid = value;
  } else if (name == "SUMMARY") {
    ev_.summary = text::truncate(unescape(value), 60);
  } else if (name == "DTSTART") {
    ev_.start = parseDate(params, value);
  } else if (name == "DTEND") {
    ev_.end = parseDate(params, value);
  } else if (name == "RRULE") {
    parseRule(value, ev_.rule);
  } else if (name == "EXDATE") {
    for (const std::string& v : split(value, ',')) {
      DateVal d = parseDate(params, v);
      if (d.valid) ev_.exdates.push_back(d.days);
    }
  } else if (name == "RECURRENCE-ID") {
    ev_.recurrenceId = parseDate(params, value);
  } else if (name == "STATUS") {
    ev_.cancelled = upper(value) == "CANCELLED";
  }
}

bool IcsReader::occursOn(const Event& e, long t) const {
  long s = e.start.days;
  if (t < s) return false;
  const Rule& r = e.rule;

  if (!r.present) {
    if (e.start.allDay) {
      long end = e.end.valid && e.end.allDay && e.end.days > s ? e.end.days : s + 1;  // DTEND is exclusive
      return t < end;
    }
    return t == s;
  }

  if (r.hasUntil && t > r.untilDays) return false;
  for (long x : e.exdates)
    if (x == t) return false;

  Civil ct = civilFromDays(t), cs = civilFromDays(s);
  int wd = weekday(t);
  auto inMonths = [&](int m) {
    if (r.byMonth.empty()) return true;
    for (int x : r.byMonth)
      if (x == m) return true;
    return false;
  };
  // BYDAY / BYMONTHDAY inside one month (used by MONTHLY and YEARLY).
  auto dayInMonthMatches = [&](int defaultDay) {
    bool dayOk = true, mdOk = true;
    if (!r.byDay.empty()) {
      dayOk = false;
      int dim = daysInMonth(ct.y, ct.m);
      for (const auto& bd : r.byDay) {
        if (bd.second != wd) continue;
        int nth = (ct.d - 1) / 7 + 1;
        int nthFromEnd = (dim - ct.d) / 7 + 1;
        if (bd.first == 0 || (bd.first > 0 && nth == bd.first) || (bd.first < 0 && nthFromEnd == -bd.first))
          dayOk = true;
      }
    }
    if (!r.byMonthDay.empty()) {
      mdOk = false;
      int dim = daysInMonth(ct.y, ct.m);
      for (int md : r.byMonthDay)
        if ((md > 0 && ct.d == md) || (md < 0 && ct.d == dim + md + 1)) mdOk = true;
    }
    if (r.byDay.empty() && r.byMonthDay.empty()) return ct.d == defaultDay;
    return dayOk && mdOk;
  };

  switch (r.freq) {
    case 1: {  // DAILY
      long diff = t - s;
      if (diff % r.interval) return false;
      if (!inMonths(ct.m)) return false;
      if (!r.byDay.empty()) {
        bool ok = false;
        for (const auto& bd : r.byDay) ok |= bd.second == wd;
        if (!ok) return false;
      }
      return !r.count || diff / r.interval < r.count;
    }
    case 2: {  // WEEKLY
      std::vector<int> days;
      for (const auto& bd : r.byDay) days.push_back(bd.second);
      if (days.empty()) days.push_back(weekday(s));
      if (std::find(days.begin(), days.end(), wd) == days.end()) return false;
      auto weekStart = [&](long x) { return x - (weekday(x) - r.wkst + 7) % 7; };
      long ws = weekStart(s), wt = weekStart(t);
      if (((wt - ws) / 7) % r.interval) return false;
      if (r.count) {  // count the occurrences before t
        long n = 0;
        for (long w = ws; w <= wt; w += 7L * r.interval)
          for (int d : days) {
            long x = w + (d - r.wkst + 7) % 7;
            if (x >= s && x < t) ++n;
          }
        if (n >= r.count) return false;
      }
      return true;
    }
    case 3: {  // MONTHLY
      int md = (ct.y * 12 + ct.m) - (cs.y * 12 + cs.m);
      if (md % r.interval) return false;
      if (r.count && md / r.interval >= r.count) return false;
      return dayInMonthMatches(cs.d);
    }
    case 4: {  // YEARLY
      int yd = ct.y - cs.y;
      if (yd % r.interval) return false;
      if (r.count && yd / r.interval >= r.count) return false;
      if (!(r.byMonth.empty() ? ct.m == cs.m : inMonths(ct.m))) return false;
      return dayInMonthMatches(cs.d);
    }
  }
  return false;
}

void IcsReader::endEvent() {
  if (!ev_.start.valid) return;
  if (ev_.recurrenceId.valid) {
    // This VEVENT replaces one occurrence of a series: hide the original slot.
    overridden_.push_back({ev_.uid, ev_.recurrenceId.days});
    ev_.rule.present = false;  // the override itself is a single event
  }
  if (ev_.cancelled) return;
  for (int i = 0; i < days_; ++i) {
    long day = today_ + i;
    if (!occursOn(ev_, day)) continue;
    Civil c = civilFromDays(day);
    Candidate cand;
    cand.ev.y = c.y;
    cand.ev.m = c.m;
    cand.ev.d = c.d;
    cand.ev.minute = ev_.start.allDay ? -1 : ev_.start.minute;
    cand.ev.title = ev_.summary.empty() ? "?" : ev_.summary;
    cand.uid = ev_.uid;
    cand.day = day;
    cand.fromRule = ev_.rule.present;
    found_.push_back(cand);
  }
}
