// Streaming iCalendar (.ics) reader for public calendar links (iCloud,
// Google "secret address", Outlook...). Portable, no Arduino deps.
//
// It only keeps what the clock needs: event occurrences on `today` and the
// following days of the window, with title, local date and start time.
// Feed it the raw bytes as they arrive (any chunk size); the whole file is
// never held in memory.
//
// Supported: line folding, VEVENT (nested VALARM ignored), DTSTART as local
// with TZID, floating, UTC (...Z) or all-day (VALUE=DATE), DTEND for multi-day
// all-day events, STATUS:CANCELLED, RRULE with FREQ=DAILY/WEEKLY/MONTHLY/
// YEARLY, INTERVAL, COUNT, UNTIL, BYDAY (incl. 2MO / -1FR), BYMONTHDAY,
// BYMONTH, EXDATE, and RECURRENCE-ID (moved or edited single occurrences).
// Not supported: BYSETPOS, BYWEEKNO, BYYEARDAY, RDATE, and TZIDs other than
// the clock's own zone (they are treated as local time).
#pragma once
#include <stdint.h>
#include <stddef.h>

#include <string>
#include <vector>

#include "calendar.h"

class IcsReader {
 public:
  // todayDays: local date as days since 1970-01-01 (see daysFromCivil).
  // days: how many days from today to collect (2 = today and tomorrow).
  // utcToLocal: converts a UTC epoch to local days + minutes after midnight.
  using UtcToLocal = void (*)(long long epoch, long* days, int* minute);
  IcsReader(long todayDays, int days, UtcToLocal utcToLocal);

  void feed(const char* data, size_t len);
  // Call once after the last byte; returns the events, sorted, at most maxEvents.
  std::vector<CalEvent> finish(size_t maxEvents = Calendar::kMaxEvents);

  size_t bytes() const { return bytes_; }
  size_t veventCount() const { return vevents_; }

 private:
  struct DateVal {
    bool valid = false;
    bool allDay = false;
    long days = 0;    // local date
    int minute = -1;  // local minutes after midnight, -1 for all-day
  };
  struct Rule {
    bool present = false;
    int freq = 0;  // 1 daily, 2 weekly, 3 monthly, 4 yearly
    int interval = 1;
    int count = 0;  // 0 = unlimited
    long untilDays = 0;
    bool hasUntil = false;
    int wkst = 0;  // 0 = Monday
    std::vector<std::pair<int, int>> byDay;  // (ordinal or 0, weekday 0=Mon..6=Sun)
    std::vector<int> byMonthDay;
    std::vector<int> byMonth;
  };
  struct Event {
    std::string uid, summary;
    DateVal start, end;
    Rule rule;
    std::vector<long> exdates;
    DateVal recurrenceId;
    bool cancelled = false;
  };
  struct Candidate {
    CalEvent ev;
    std::string uid;
    long day;
    bool fromRule;
  };

  void line(const std::string& l);
  void property(const std::string& name, const std::string& params, const std::string& value);
  void endEvent();
  DateVal parseDate(const std::string& params, const std::string& value) const;
  void parseRule(const std::string& value, Rule& r) const;
  bool occursOn(const Event& e, long day) const;

  long today_;
  int days_;
  UtcToLocal utcToLocal_;
  std::string cur_;   // logical line being assembled (unfolding)
  std::string phys_;  // physical line being read
  int depth_ = 0;     // 0 outside, 1 in VEVENT, 2+ inside nested blocks (VALARM)
  Event ev_;
  std::vector<Candidate> found_;
  std::vector<std::pair<std::string, long>> overridden_;  // (uid, day) moved by RECURRENCE-ID
  size_t bytes_ = 0;
  size_t vevents_ = 0;
};
