// Calendar events pushed from the iPhone Shortcut. Portable.
//
// Body format, one event per line (UTF-8):
//   2026-11-13 18:30 Tréning        timed event
//   2026-11-14 * Babka narodeniny   all-day event
#pragma once
#include <string>
#include <vector>

struct CalEvent {
  int y = 0, m = 0, d = 0;
  int minute = -1;  // minutes after midnight, -1 = all day
  std::string title;
};

struct Today {
  int y, m, d;  // local date
  int minute;   // local minutes after midnight
};

class Calendar {
 public:
  static constexpr int kMaxEvents = 12;

  // Replaces the stored events. Returns how many lines were accepted.
  int setFromText(const std::string& body);
  // Replaces the stored events with already parsed ones (ICS feed).
  void setEvents(const std::vector<CalEvent>& events);

  // Events still worth showing, soonest first:
  //  - today's timed events until `graceMin` minutes after they start
  //  - today's all-day events
  //  - tomorrow's events once the local time is past `tomorrowFromHour`
  std::vector<CalEvent> upcoming(const Today& now, int graceMin, int tomorrowFromHour) const;

  // "18:30", "Dnes", "Zajtra 8:00" - labels come from config so they can be translated.
  static std::string label(const CalEvent& e, const Today& now, const char* today,
                           const char* tomorrow);

  size_t size() const { return events_.size(); }
  const std::vector<CalEvent>& all() const { return events_; }

 private:
  std::vector<CalEvent> events_;
};

// Days since 1970-01-01 for a civil date (proleptic Gregorian).
long daysFromCivil(int y, int m, int d);
