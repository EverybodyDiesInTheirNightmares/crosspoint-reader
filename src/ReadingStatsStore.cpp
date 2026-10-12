#include "ReadingStatsStore.h"

#include <Logging.h>
#include <TrustedTime.h>

#include <algorithm>
#include <cstring>
#include <ctime>

namespace {

constexpr size_t MAX_BOOK_ID = 40;
constexpr size_t MAX_BOOK_PATH = 160;
constexpr size_t MAX_BOOK_TITLE = 80;
constexpr size_t MAX_BOOK_AUTHOR = 60;

uint32_t dayOrdinal(const int year, const unsigned month, const unsigned day) {
  int adjustedYear = year - (month <= 2);
  const int era = (adjustedYear >= 0 ? adjustedYear : adjustedYear - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(adjustedYear - era * 400);
  const unsigned dayOfYear = (153u * (month + (month > 2 ? -3 : 9)) + 2u) / 5u + day - 1u;
  const unsigned dayOfEra = yearOfEra * 365u + yearOfEra / 4u - yearOfEra / 100u + dayOfYear;
  return static_cast<uint32_t>(era * 146097 + static_cast<int>(dayOfEra) - 719468);
}

uint32_t dayOrdinal(const int64_t epoch) {
  const time_t timestamp = static_cast<time_t>(epoch);
  std::tm local{};
  if (!localtime_r(&timestamp, &local)) return 0;
  return dayOrdinal(local.tm_year + 1900, static_cast<unsigned>(local.tm_mon + 1),
                    static_cast<unsigned>(local.tm_mday));
}

void discardBookDaysBefore(const std::vector<ReadingStatsDay>& days, std::vector<ReadingStatsBookDay>& bookDays) {
  if (days.empty()) {
    bookDays.clear();
    return;
  }
  uint32_t oldestOrdinal = 0;
  if (!ReadingStatsStore::getDayOrdinal(days.front().date.c_str(), oldestOrdinal)) return;
  bookDays.erase(std::remove_if(bookDays.begin(), bookDays.end(), [oldestOrdinal](const ReadingStatsBookDay& day) {
                   return day.dayOrdinal < oldestOrdinal;
                 }),
                 bookDays.end());
}

bool addBookDay(std::vector<ReadingStatsBookDay>& entries, const char* bookId, const uint32_t ordinal,
                const uint32_t seconds) {
  if (!bookId || bookId[0] == '\0' || ordinal == 0 || seconds == 0) return false;
  auto existing = std::find_if(entries.begin(), entries.end(), [&](const ReadingStatsBookDay& entry) {
    return entry.dayOrdinal == ordinal && strcmp(entry.bookId, bookId) == 0;
  });
  if (existing != entries.end()) {
    existing->seconds += seconds;
    return true;
  }
  const auto sortEntries = [&entries] {
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
      if (left.dayOrdinal != right.dayOrdinal) return left.dayOrdinal < right.dayOrdinal;
      return strcmp(left.bookId, right.bookId) < 0;
    });
  };
  if (entries.size() >= ReadingStatsStore::MAX_BOOK_DAY_ENTRIES) {
    auto oldest = std::min_element(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
      return left.dayOrdinal < right.dayOrdinal;
    });
    if (oldest == entries.end() || ordinal <= oldest->dayOrdinal) return false;
    *oldest = {};
    strncpy(oldest->bookId, bookId, sizeof(oldest->bookId) - 1);
    oldest->dayOrdinal = ordinal;
    oldest->seconds = seconds;
    sortEntries();
    return true;
  }
  if (entries.capacity() < ReadingStatsStore::MAX_BOOK_DAY_ENTRIES) entries.reserve(ReadingStatsStore::MAX_BOOK_DAY_ENTRIES);
  ReadingStatsBookDay entry;
  strncpy(entry.bookId, bookId, sizeof(entry.bookId) - 1);
  entry.dayOrdinal = ordinal;
  entry.seconds = seconds;
  entries.push_back(entry);
  sortEntries();
  return true;
}

bool parseDate(const char* date, uint32_t& ordinal) {
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  char tail = '\0';
  if (!date || strlen(date) != 10 || date[4] != '-' || date[7] != '-' ||
      sscanf(date, "%4d-%2u-%2u%c", &year, &month, &day, &tail) != 3 || year < 1970 || month == 0 ||
      month > 12 || day == 0 || day > 31) {
    return false;
  }
  constexpr unsigned daysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leapYear = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  const unsigned maxDay = month == 2 && leapYear ? 29 : daysPerMonth[month - 1];
  if (day > maxDay) return false;
  ordinal = dayOrdinal(year, month, day);
  return true;
}

// "YYYY-MM-DD" for a trusted epoch, in the device's configured timezone (the
// TZ rule pushed by timezones::applyToClock() feeds localtime_r).
void formatDate(const int64_t epoch, char (&out)[11]) {
  const time_t t = static_cast<time_t>(epoch);
  std::tm tmBuf;
  localtime_r(&t, &tmBuf);
  snprintf(out, sizeof(out), "%04d-%02d-%02d", tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday);
}

std::string clampString(const char* src, size_t maxLen) {
  if (!src) return "";
  std::string out = src;
  if (out.size() > maxLen) out.resize(maxLen);
  return out;
}

}  // namespace

void ReadingStatsStore::toJson(JsonDocument& doc) const {
  doc["totalSeconds"] = totalSeconds;
  doc["totalPages"] = totalPages;
  JsonArray dayArr = doc["days"].to<JsonArray>();
  for (const auto& day : days) {
    JsonObject obj = dayArr.add<JsonObject>();
    obj["d"] = day.date;
    obj["s"] = day.seconds;
    obj["p"] = day.pages;
    obj["n"] = day.sessions;
  }
  JsonArray bookArr = doc["books"].to<JsonArray>();
  for (const auto& book : books) {
    JsonObject obj = bookArr.add<JsonObject>();
    obj["id"] = book.bookId;
    obj["path"] = book.path;
    obj["t"] = book.title;
    obj["a"] = book.author;
    obj["s"] = book.seconds;
    obj["p"] = book.pages;
    obj["n"] = book.sessions;
    obj["last"] = static_cast<long long>(book.lastReadAt);
    obj["prog"] = book.lastProgressBp;
  }
  JsonArray bookDayArr = doc["bookDays"].to<JsonArray>();
  for (const auto& day : bookDays) {
    JsonObject obj = bookDayArr.add<JsonObject>();
    obj["id"] = day.bookId;
    obj["d"] = day.dayOrdinal;
    obj["s"] = day.seconds;
  }
}

bool ReadingStatsStore::fromJson(JsonVariantConst doc) {
  days.clear();
  books.clear();
  bookDays.clear();
  totalSeconds = 0;
  totalPages = 0;

  JsonArrayConst dayArr = doc["days"].as<JsonArrayConst>();
  days.reserve(std::min(dayArr.size(), MAX_DAYS));
  for (JsonObjectConst obj : dayArr) {
    if (days.size() >= MAX_DAYS) break;
    ReadingStatsDay day;
    day.date = obj["d"] | "";
    if (day.date.size() != 10) continue;  // skip malformed rows
    day.seconds = obj["s"] | 0;
    day.pages = obj["p"] | 0;
    day.sessions = obj["n"] | 0;
    days.push_back(day);
  }
  std::sort(days.begin(), days.end(),
            [](const ReadingStatsDay& a, const ReadingStatsDay& b) { return a.date < b.date; });

  JsonArrayConst bookArr = doc["books"].as<JsonArrayConst>();
  books.reserve(std::min(bookArr.size(), MAX_BOOKS));
  for (JsonObjectConst obj : bookArr) {
    if (books.size() >= MAX_BOOKS) break;
    ReadingStatsBook book;
    book.bookId = obj["id"] | "";
    book.path = obj["path"] | "";
    book.title = obj["t"] | "";
    book.author = obj["a"] | "";
    book.seconds = obj["s"] | 0;
    book.pages = obj["p"] | 0;
    book.sessions = obj["n"] | 0;
    book.lastReadAt = obj["last"] | 0LL;
    book.lastProgressBp = obj["prog"] | 0;
    books.push_back(book);
  }
  JsonArrayConst bookDayArr = doc["bookDays"].as<JsonArrayConst>();
  bookDays.reserve(std::min(bookDayArr.size(), MAX_BOOK_DAY_ENTRIES));
  for (JsonObjectConst obj : bookDayArr) {
    const char* id = obj["id"] | "";
    const uint32_t ordinal = obj["d"] | 0;
    const uint32_t seconds = obj["s"] | 0;
    if (id[0] == '\0') continue;
    char date[11];
    const bool retainedDay = formatDayOrdinal(ordinal, date, sizeof(date)) &&
                             std::any_of(days.begin(), days.end(), [&](const ReadingStatsDay& day) {
                               return day.date == date;
                             });
    if (retainedDay &&
        std::any_of(books.begin(), books.end(), [&](const ReadingStatsBook& book) { return book.bookId == id; })) {
      addBookDay(bookDays, id, ordinal, seconds);
    }
  }
  std::sort(books.begin(), books.end(),
            [](const ReadingStatsBook& a, const ReadingStatsBook& b) { return a.lastReadAt > b.lastReadAt; });
  uint64_t dailySeconds = 0;
  uint64_t dailyPages = 0;
  for (const auto& day : days) {
    dailySeconds += day.seconds;
    dailyPages += day.pages;
  }
  uint64_t bookSeconds = 0;
  uint64_t bookPages = 0;
  for (const auto& book : books) {
    bookSeconds += book.seconds;
    bookPages += book.pages;
  }
  totalSeconds = doc["totalSeconds"] | std::max(dailySeconds, bookSeconds);
  totalPages = doc["totalPages"] | std::max(dailyPages, bookPages);
  return true;
}

void ReadingStatsStore::recordSession(const int64_t sessionEndEpoch, const uint32_t durationSeconds,
                                      const uint32_t pages, const char* bookId, const char* bookPath,
                                      const char* title, const char* author, const uint16_t endProgressBp) {
  if (sessionEndEpoch <= 0) return;

  char date[11];
  formatDate(sessionEndEpoch, date);
  const uint32_t ordinal = dayOrdinal(sessionEndEpoch);

  // --- per-day aggregation ---
  auto dayIt = std::find_if(days.begin(), days.end(), [&](const ReadingStatsDay& d) { return d.date == date; });
  if (dayIt == days.end()) {
    ReadingStatsDay day;
    day.date = date;
    day.seconds = 0;
    day.pages = 0;
    day.sessions = 0;
    days.push_back(day);
    dayIt = days.end() - 1;
  }
  ReadingStatsDay& day = *dayIt;
  day.seconds += durationSeconds;
  day.pages += pages;
  totalSeconds += durationSeconds;
  totalPages += pages;
  if (day.sessions < UINT16_MAX) day.sessions += 1;

  std::sort(days.begin(), days.end(),
            [](const ReadingStatsDay& a, const ReadingStatsDay& b) { return a.date < b.date; });
  if (days.size() > MAX_DAYS) {
    days.erase(days.begin(), days.end() - MAX_DAYS);
    discardBookDaysBefore(days, bookDays);
  }

  // --- per-book aggregation (keyed by document id; empty id skips the book row) ---
  if (bookId && bookId[0] != '\0') {
    auto bookIt =
        std::find_if(books.begin(), books.end(), [&](const ReadingStatsBook& b) { return b.bookId == bookId; });
    if (bookIt == books.end()) {
      ReadingStatsBook book;
      book.bookId = clampString(bookId, MAX_BOOK_ID);
      book.path = clampString(bookPath, MAX_BOOK_PATH);
      book.title = clampString(title, MAX_BOOK_TITLE);
      book.author = clampString(author, MAX_BOOK_AUTHOR);
      book.seconds = 0;
      book.pages = 0;
      book.sessions = 0;
      book.lastReadAt = 0;
      book.lastProgressBp = 0;
      books.push_back(book);
      bookIt = books.end() - 1;
    }
    ReadingStatsBook& book = *bookIt;
    book.seconds += durationSeconds;
    book.pages += pages;
    if (book.sessions < UINT16_MAX) book.sessions += 1;
    book.lastReadAt = sessionEndEpoch;
    book.lastProgressBp = endProgressBp;
    if (bookPath && bookPath[0] != '\0') book.path = clampString(bookPath, MAX_BOOK_PATH);
    if (ordinal != 0) addBookDay(bookDays, book.bookId.c_str(), ordinal, durationSeconds);

    std::sort(books.begin(), books.end(),
              [](const ReadingStatsBook& a, const ReadingStatsBook& b) { return a.lastReadAt > b.lastReadAt; });
    if (books.size() > MAX_BOOKS) {
      const auto firstRemovedId = books[MAX_BOOKS].bookId;
      bookDays.erase(std::remove_if(bookDays.begin(), bookDays.end(), [&](const ReadingStatsBookDay& entry) {
                       return entry.bookId == firstRemovedId;
                     }),
                     bookDays.end());
      books.erase(books.begin() + MAX_BOOKS, books.end());
    }
  }

  if (!saveToFile()) {
    LOG_ERR("STATS", "Failed to persist reading stats");
  }
}

bool ReadingStatsStore::adjustBookReadingTime(const char* bookId, const char* date, const int32_t deltaSeconds) {
  if (!bookId || bookId[0] == '\0' || deltaSeconds == 0 || deltaSeconds == INT32_MIN) return false;
  uint32_t ordinal = 0;
  if (!parseDate(date, ordinal)) return false;

  auto bookIt = std::find_if(books.begin(), books.end(), [&](const ReadingStatsBook& book) {
    return book.bookId == bookId;
  });
  if (bookIt == books.end()) return false;

  auto bookDay = std::find_if(bookDays.begin(), bookDays.end(), [&](const ReadingStatsBookDay& entry) {
    return entry.dayOrdinal == ordinal && bookIt->bookId == entry.bookId;
  });
  const bool hasBookDay = bookDay != bookDays.end();
  if (deltaSeconds < 0 &&
      (!hasBookDay || bookDay->seconds < static_cast<uint32_t>(-deltaSeconds))) {
    return false;
  }
  if (deltaSeconds > 0 && !hasBookDay && bookDays.size() >= MAX_BOOK_DAY_ENTRIES) {
    const auto oldest = std::min_element(bookDays.begin(), bookDays.end(), [](const auto& left, const auto& right) {
      return left.dayOrdinal < right.dayOrdinal;
    });
    if (oldest == bookDays.end() || ordinal <= oldest->dayOrdinal) return false;
  }

  char dateCopy[11];
  snprintf(dateCopy, sizeof(dateCopy), "%s", date);
  auto globalDay = std::find_if(days.begin(), days.end(), [&](const ReadingStatsDay& day) {
    return day.date == dateCopy;
  });
  if (globalDay == days.end() && days.size() >= MAX_DAYS) return false;
  if (deltaSeconds < 0 &&
      (globalDay == days.end() || globalDay->seconds < static_cast<uint32_t>(-deltaSeconds) ||
       bookIt->seconds < static_cast<uint32_t>(-deltaSeconds) || totalSeconds < static_cast<uint32_t>(-deltaSeconds))) {
    return false;
  }

  const uint32_t magnitude = static_cast<uint32_t>(deltaSeconds > 0 ? deltaSeconds : -deltaSeconds);
  const uint32_t previousBookSeconds = bookIt->seconds;
  const uint64_t previousTotalSeconds = totalSeconds;
  const bool hadGlobalDay = globalDay != days.end();
  const uint32_t previousGlobalSeconds = hadGlobalDay ? globalDay->seconds : 0;
  const uint32_t previousBookDaySeconds = hasBookDay ? bookDay->seconds : 0;
  ReadingStatsBookDay evictedBookDay{};
  bool evictedBookDayForAdd = false;
  if (deltaSeconds > 0 && !hasBookDay && bookDays.size() >= MAX_BOOK_DAY_ENTRIES) {
    const auto oldest = std::min_element(bookDays.begin(), bookDays.end(), [](const auto& left, const auto& right) {
      return left.dayOrdinal < right.dayOrdinal;
    });
    evictedBookDay = *oldest;
    evictedBookDayForAdd = true;
  }
  if (deltaSeconds > 0) {
    if (!addBookDay(bookDays, bookIt->bookId.c_str(), ordinal, magnitude)) return false;
  } else {
    bookDay->seconds -= magnitude;
    if (bookDay->seconds == 0) bookDays.erase(bookDay);
  }
  if (deltaSeconds > 0) {
    bookIt->seconds += magnitude;
    if (globalDay == days.end()) {
      ReadingStatsDay day{dateCopy, 0, 0, 0};
      days.push_back(std::move(day));
      globalDay = days.end() - 1;
    }
    globalDay->seconds += magnitude;
    totalSeconds += magnitude;
  } else {
    bookIt->seconds -= magnitude;
    globalDay->seconds -= magnitude;
    totalSeconds -= magnitude;
    if (globalDay->seconds == 0 && globalDay->pages == 0 && globalDay->sessions == 0) days.erase(globalDay);
  }
  std::sort(days.begin(), days.end(),
            [](const ReadingStatsDay& a, const ReadingStatsDay& b) { return a.date < b.date; });
  if (!saveToFile()) {
    LOG_ERR("STATS", "Failed to persist reading-time correction");
    bookIt->seconds = previousBookSeconds;
    totalSeconds = previousTotalSeconds;
    if (deltaSeconds > 0) {
      auto addedDay = std::find_if(bookDays.begin(), bookDays.end(), [&](const ReadingStatsBookDay& entry) {
        return entry.dayOrdinal == ordinal && bookIt->bookId == entry.bookId;
      });
      if (hasBookDay && addedDay != bookDays.end()) {
        addedDay->seconds = previousBookDaySeconds;
      } else if (evictedBookDayForAdd) {
        *addedDay = evictedBookDay;
        std::sort(bookDays.begin(), bookDays.end(), [](const auto& left, const auto& right) {
          if (left.dayOrdinal != right.dayOrdinal) return left.dayOrdinal < right.dayOrdinal;
          return strcmp(left.bookId, right.bookId) < 0;
        });
      } else if (addedDay != bookDays.end()) {
        bookDays.erase(addedDay);
      }
    } else {
      // The subtract path mutated the entry in place or erased it; restore by
      // assignment — addBookDay() accumulates into a surviving entry.
      auto rollbackBookDay = std::find_if(bookDays.begin(), bookDays.end(), [&](const ReadingStatsBookDay& entry) {
        return entry.dayOrdinal == ordinal && bookIt->bookId == entry.bookId;
      });
      if (rollbackBookDay != bookDays.end()) {
        rollbackBookDay->seconds = previousBookDaySeconds;
      } else {
        addBookDay(bookDays, bookIt->bookId.c_str(), ordinal, previousBookDaySeconds);
      }
    }
    auto rollbackGlobalDay = std::find_if(days.begin(), days.end(), [&](const ReadingStatsDay& day) {
      return day.date == dateCopy;
    });
    if (hadGlobalDay) {
      if (rollbackGlobalDay != days.end()) {
        rollbackGlobalDay->seconds = previousGlobalSeconds;
      } else {
        // The subtract path erased the day row; rebuild it.
        days.push_back(ReadingStatsDay{dateCopy, previousGlobalSeconds, 0, 0});
        std::sort(days.begin(), days.end(),
                  [](const ReadingStatsDay& a, const ReadingStatsDay& b) { return a.date < b.date; });
      }
    } else if (rollbackGlobalDay != days.end()) {
      days.erase(rollbackGlobalDay);
    }
    return false;
  }
  return true;
}

const ReadingStatsBook* ReadingStatsStore::findBookById(const char* bookId) const {
  if (!bookId || bookId[0] == '\0') return nullptr;
  const auto it = std::find_if(books.begin(), books.end(), [&](const ReadingStatsBook& book) {
    return book.bookId == bookId;
  });
  return it == books.end() ? nullptr : &*it;
}

bool ReadingStatsStore::getDayOrdinal(const char* date, uint32_t& ordinal) { return parseDate(date, ordinal); }

bool ReadingStatsStore::formatDayOrdinal(const uint32_t ordinal, char* out, const size_t outSize) {
  if (!out || outSize < 11) return false;

  int days = static_cast<int>(ordinal) + 719468;
  const int era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned dayOfEra = static_cast<unsigned>(days - era * 146097);
  const unsigned yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
  int year = static_cast<int>(yearOfEra) + era * 400;
  const unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
  const unsigned monthPrime = (5 * dayOfYear + 2) / 153;
  const unsigned day = dayOfYear - (153 * monthPrime + 2) / 5 + 1;
  const unsigned month = monthPrime + (monthPrime < 10 ? 3 : -9);
  year += month <= 2;
  snprintf(out, outSize, "%04d-%02u-%02u", year, month, day);
  return true;
}

const ReadingStatsDay* ReadingStatsStore::findDay(const char* date) const {
  const auto it = std::find_if(days.begin(), days.end(), [&](const ReadingStatsDay& d) { return d.date == date; });
  return it == days.end() ? nullptr : &*it;
}

void ReadingStatsStore::sumRecent(const size_t dayCount, uint32_t& seconds, uint32_t& pages) const {
  seconds = 0;
  pages = 0;
  if (dayCount == 0) return;

  // Day rows are date-sorted ascending; walk the tail backwards over the
  // requested number of calendar days ending today.
  const int64_t now = trustedtime::trustedNow();
  if (now <= 0) return;
  const uint32_t todayOrdinal = dayOrdinal(now);
  for (size_t i = 0; i < dayCount; i++) {
    if (i > todayOrdinal) break;
    char date[11];
    if (!formatDayOrdinal(todayOrdinal - static_cast<uint32_t>(i), date, sizeof(date))) continue;
    if (const ReadingStatsDay* d = findDay(date)) {
      seconds += d->seconds;
      pages += d->pages;
    }
  }
}

void ReadingStatsStore::sumAll(uint32_t& seconds, uint32_t& pages) const {
  seconds = static_cast<uint32_t>(std::min<uint64_t>(totalSeconds, UINT32_MAX));
  pages = static_cast<uint32_t>(std::min<uint64_t>(totalPages, UINT32_MAX));
}

int ReadingStatsStore::currentStreak() const {
  if (days.empty()) return 0;

  const int64_t now = trustedtime::trustedNow();
  if (now <= 0) return 0;

  // Start from today; when today has no reading yet, start from yesterday so a
  // streak isn't reported dead at midnight.
  int64_t cursor = static_cast<int64_t>(dayOrdinal(now));
  char date[11];
  if (!formatDayOrdinal(static_cast<uint32_t>(cursor), date, sizeof(date))) return 0;
  const ReadingStatsDay* entry = findDay(date);
  if (!entry || entry->seconds == 0) cursor--;

  int streak = 0;
  while (cursor >= 0) {
    if (!formatDayOrdinal(static_cast<uint32_t>(cursor), date, sizeof(date))) break;
    entry = findDay(date);
    if (!entry || entry->seconds == 0) break;
    streak++;
    cursor--;
  }
  return streak;
}

int ReadingStatsStore::currentGoalStreak(const uint32_t dailyGoalSeconds) const {
  if (days.empty() || dailyGoalSeconds == 0) return 0;
  const int64_t now = trustedtime::trustedNow();
  if (now <= 0) return 0;

  int ordinal = static_cast<int>(dayOrdinal(now));
  char date[11];
  if (!formatDayOrdinal(static_cast<uint32_t>(ordinal), date, sizeof(date))) return 0;
  const ReadingStatsDay* entry = findDay(date);
  if (!entry || entry->seconds < dailyGoalSeconds) ordinal--;

  int streak = 0;
  while (ordinal >= 0) {
    if (!formatDayOrdinal(static_cast<uint32_t>(ordinal), date, sizeof(date))) break;
    entry = findDay(date);
    if (!entry || entry->seconds < dailyGoalSeconds) break;
    streak++;
    ordinal--;
  }
  return streak;
}

int ReadingStatsStore::maxGoalStreak(const uint32_t dailyGoalSeconds) const {
  if (dailyGoalSeconds == 0) return 0;
  int current = 0;
  int maximum = 0;
  uint32_t previousOrdinal = 0;
  for (const auto& day : days) {
    uint32_t ordinal = 0;
    if (!parseDate(day.date.c_str(), ordinal) || day.seconds < dailyGoalSeconds) {
      current = 0;
      previousOrdinal = 0;
      continue;
    }
    current = previousOrdinal != 0 && ordinal == previousOrdinal + 1 ? current + 1 : 1;
    previousOrdinal = ordinal;
    maximum = std::max(maximum, current);
  }
  return maximum;
}
