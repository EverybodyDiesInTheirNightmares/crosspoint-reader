#include "ReadingStatsStore.h"

#include <Logging.h>
#include <TrustedTime.h>

#include <algorithm>
#include <ctime>

namespace {

constexpr size_t MAX_BOOK_ID = 40;
constexpr size_t MAX_BOOK_PATH = 160;
constexpr size_t MAX_BOOK_TITLE = 80;
constexpr size_t MAX_BOOK_AUTHOR = 60;

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
}

bool ReadingStatsStore::fromJson(JsonVariantConst doc) {
  days.clear();
  books.clear();

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
  std::sort(books.begin(), books.end(),
            [](const ReadingStatsBook& a, const ReadingStatsBook& b) { return a.lastReadAt > b.lastReadAt; });
  return true;
}

void ReadingStatsStore::recordSession(const int64_t sessionEndEpoch, const uint32_t durationSeconds,
                                      const uint32_t pages, const char* bookId, const char* bookPath,
                                      const char* title, const char* author, const uint16_t endProgressBp) {
  if (sessionEndEpoch <= 0) return;

  char date[11];
  formatDate(sessionEndEpoch, date);

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
  if (day.sessions < UINT16_MAX) day.sessions += 1;

  std::sort(days.begin(), days.end(),
            [](const ReadingStatsDay& a, const ReadingStatsDay& b) { return a.date < b.date; });
  if (days.size() > MAX_DAYS) {
    days.erase(days.begin(), days.end() - MAX_DAYS);
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

    std::sort(books.begin(), books.end(),
              [](const ReadingStatsBook& a, const ReadingStatsBook& b) { return a.lastReadAt > b.lastReadAt; });
    if (books.size() > MAX_BOOKS) {
      books.erase(books.begin() + MAX_BOOKS, books.end());
    }
  }

  if (!saveToFile()) {
    LOG_ERR("STATS", "Failed to persist reading stats");
  }
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
  for (size_t i = 0; i < dayCount; i++) {
    char date[11];
    formatDate(now - static_cast<int64_t>(i) * 86400, date);
    if (const ReadingStatsDay* d = findDay(date)) {
      seconds += d->seconds;
      pages += d->pages;
    }
  }
}

void ReadingStatsStore::sumAll(uint32_t& seconds, uint32_t& pages) const {
  seconds = 0;
  pages = 0;
  for (const auto& d : days) {
    seconds += d.seconds;
    pages += d.pages;
  }
}

int ReadingStatsStore::currentStreak() const {
  if (days.empty()) return 0;

  const int64_t now = trustedtime::trustedNow();
  if (now <= 0) return 0;

  // Start from today; when today has no reading yet, start from yesterday so a
  // streak isn't reported dead at midnight.
  int64_t cursor = now;
  char date[11];
  formatDate(cursor, date);
  const ReadingStatsDay* entry = findDay(date);
  if (!entry || entry->seconds == 0) cursor -= 86400;

  int streak = 0;
  for (;;) {
    formatDate(cursor, date);
    entry = findDay(date);
    if (!entry || entry->seconds == 0) break;
    streak++;
    cursor -= 86400;
  }
  return streak;
}
