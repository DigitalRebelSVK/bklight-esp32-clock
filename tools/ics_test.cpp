// Host tests for the ICS reader:  g++ -std=c++17 -Isrc tools/ics_test.cpp src/ics.cpp src/calendar.cpp src/text.cpp
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <random>
#include <string>

#include "../src/calendar.h"
#include "../src/ics.h"

static void utcToLocal(long long epoch, long* days, int* minute) {
  time_t t = (time_t)epoch;
  struct tm lt;
  localtime_r(&t, &lt);
  *days = daysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
  *minute = lt.tm_hour * 60 + lt.tm_min;
}

static const char* kIcs =
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Apple Inc.//iCloud\r\n"
    "X-WR-CALNAME:Rodina\r\n"
    // 1 timed today, TZID, with an alarm whose SUMMARY must not leak
    "BEGIN:VEVENT\r\nUID:t1\r\nDTSTART;TZID=Europe/Bratislava:20261118T183000\r\n"
    "DTEND;TZID=Europe/Bratislava:20261118T200000\r\nSUMMARY:Tréning s Jankom\r\n"
    "BEGIN:VALARM\r\nACTION:DISPLAY\r\nSUMMARY:ALARM TEXT\r\nTRIGGER:-PT15M\r\nEND:VALARM\r\nEND:VEVENT\r\n"
    // 2 all-day today
    "BEGIN:VEVENT\r\nUID:a1\r\nDTSTART;VALUE=DATE:20261118\r\nDTEND;VALUE=DATE:20261119\r\n"
    "SUMMARY:Babka\\, dedko na návšteve\r\nEND:VEVENT\r\n"
    // 3 UTC tomorrow 07:00Z -> 08:00 local
    "BEGIN:VEVENT\r\nUID:u1\r\nDTSTART:20261119T070000Z\r\nSUMMARY:Zubár\r\nEND:VEVENT\r\n"
    // 4 bi-weekly WE,TH from 2026-11-04, tomorrow's excluded
    "BEGIN:VEVENT\r\nUID:w1\r\nDTSTART;TZID=Europe/Bratislava:20261104T170000\r\n"
    "RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=WE,TH\r\nEXDATE;TZID=Europe/Bratislava:20261119T170000\r\n"
    "SUMMARY:Plávanie\r\nEND:VEVENT\r\n"
    // 5 monthly 3rd Wednesday
    "BEGIN:VEVENT\r\nUID:m1\r\nDTSTART;TZID=Europe/Bratislava:20260121T190000\r\n"
    "RRULE:FREQ=MONTHLY;BYDAY=3WE\r\nSUMMARY:Schôdza domu\r\nEND:VEVENT\r\n"
    // 6 yearly birthday, all-day, tomorrow
    "BEGIN:VEVENT\r\nUID:y1\r\nDTSTART;VALUE=DATE:19801119\r\nDTEND;VALUE=DATE:19801120\r\n"
    "RRULE:FREQ=YEARLY\r\nSUMMARY:Narodeniny Ľubka\r\nEND:VEVENT\r\n"
    // 7 weekly Thursday; tomorrow's occurrence moved to Friday (outside window)
    "BEGIN:VEVENT\r\nUID:mv\r\nDTSTART;TZID=Europe/Bratislava:20261001T090000\r\n"
    "RRULE:FREQ=WEEKLY;BYDAY=TH\r\nSUMMARY:Porada\r\nEND:VEVENT\r\n"
    "BEGIN:VEVENT\r\nUID:mv\r\nRECURRENCE-ID;TZID=Europe/Bratislava:20261119T090000\r\n"
    "DTSTART;TZID=Europe/Bratislava:20261120T100000\r\nSUMMARY:Porada (presunutá)\r\nEND:VEVENT\r\n"
    // 8 cancelled
    "BEGIN:VEVENT\r\nUID:c1\r\nDTSTART;TZID=Europe/Bratislava:20261118T120000\r\nSTATUS:CANCELLED\r\n"
    "SUMMARY:Zrušené\r\nEND:VEVENT\r\n"
    // 9 weekly, already ended
    "BEGIN:VEVENT\r\nUID:e1\r\nDTSTART;TZID=Europe/Bratislava:20250101T080000\r\n"
    "RRULE:FREQ=WEEKLY;UNTIL=20251231T230000Z\r\nSUMMARY:Staré\r\nEND:VEVENT\r\n"
    // 10 daily COUNT=2 from yesterday -> only today
    "BEGIN:VEVENT\r\nUID:d2\r\nDTSTART;TZID=Europe/Bratislava:20261117T070000\r\n"
    "RRULE:FREQ=DAILY;COUNT=2\r\nSUMMARY:Lieky\r\nEND:VEVENT\r\n"
    // 11 folded summary (fold inside a multi-byte char boundary is avoided by Apple)
    "BEGIN:VEVENT\r\nUID:f1\r\nDTSTART;TZID=Europe/Bratislava:20261119T160000\r\n"
    "SUMMARY:Veľmi dlhý\r\n  názov udalosti\r\nEND:VEVENT\r\n"
    // 12 multi-day all-day vacation 16.-20.
    "BEGIN:VEVENT\r\nUID:v1\r\nDTSTART;VALUE=DATE:20261116\r\nDTEND;VALUE=DATE:20261121\r\n"
    "SUMMARY:Dovolenka\r\nEND:VEVENT\r\n"
    // 13 a recurring cancelled occurrence (today) of a daily series
    "BEGIN:VEVENT\r\nUID:dc\r\nDTSTART;TZID=Europe/Bratislava:20261101T060000\r\n"
    "RRULE:FREQ=DAILY\r\nSUMMARY:Ranný beh\r\nEND:VEVENT\r\n"
    "BEGIN:VEVENT\r\nUID:dc\r\nRECURRENCE-ID;TZID=Europe/Bratislava:20261118T060000\r\n"
    "DTSTART;TZID=Europe/Bratislava:20261118T060000\r\nSTATUS:CANCELLED\r\nSUMMARY:Ranný beh\r\nEND:VEVENT\r\n"
    "END:VCALENDAR\r\n";

static std::vector<CalEvent> run(size_t chunkMax, unsigned seed) {
  IcsReader r(daysFromCivil(2026, 11, 18), 2, utcToLocal);
  std::string all = kIcs;
  std::mt19937 rng(seed);
  size_t i = 0;
  while (i < all.size()) {
    size_t n = 1 + rng() % chunkMax;
    if (i + n > all.size()) n = all.size() - i;
    r.feed(all.data() + i, n);
    i += n;
  }
  return r.finish();
}

int main() {
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  std::vector<CalEvent> ev = run(4096, 1);
  for (auto& e : ev)
    printf("%04d-%02d-%02d %s %s\n", e.y, e.m, e.d,
           e.minute < 0 ? "  *  " : (std::to_string(e.minute / 60) + ":" + (e.minute % 60 < 10 ? "0" : "") +
                                     std::to_string(e.minute % 60)).c_str(),
           e.title.c_str());
  auto has = [&](int d, int minute, const char* title) {
    for (auto& e : ev)
      if (e.d == d && e.minute == minute && e.title == title) return true;
    return false;
  };
  assert(has(18, 18 * 60 + 30, "Tréning s Jankom"));
  assert(has(18, -1, "Babka, dedko na návšteve"));
  assert(has(19, 8 * 60, "Zubár"));
  assert(has(18, 17 * 60, "Plávanie") && !has(19, 17 * 60, "Plávanie"));
  assert(has(18, 19 * 60, "Schôdza domu"));
  assert(has(19, -1, "Narodeniny Ľubka"));
  assert(!has(19, 9 * 60, "Porada"));
  assert(!has(18, 12 * 60, "Zrušené"));
  assert(!has(18, 8 * 60, "Staré"));
  assert(has(18, 7 * 60, "Lieky") && !has(19, 7 * 60, "Lieky"));
  assert(has(19, 16 * 60, "Veľmi dlhý názov udalosti"));
  assert(has(18, -1, "Dovolenka") && has(19, -1, "Dovolenka"));
  assert(!has(18, 6 * 60, "Ranný beh") && has(19, 6 * 60, "Ranný beh"));
  for (auto& e : ev) assert(e.title != "ALARM TEXT");
  fflush(stdout);
  assert(ev.size() == 11);  // 6 today + 5 tomorrow
  // same result no matter how the bytes are chopped up on the way in
  for (unsigned seed = 0; seed < 50; ++seed) {
    auto again = run(1 + seed % 17, seed);
    assert(again.size() == ev.size());
    for (size_t i = 0; i < ev.size(); ++i) assert(again[i].title == ev[i].title && again[i].minute == ev[i].minute);
  }
  // sorted: day, then all-day first, then time
  for (size_t i = 1; i < ev.size(); ++i)
    assert(ev[i - 1].d < ev[i].d || (ev[i - 1].d == ev[i].d && ev[i - 1].minute <= ev[i].minute));
  printf("ics tests ok (%zu events)\n", ev.size());
  return 0;
}
