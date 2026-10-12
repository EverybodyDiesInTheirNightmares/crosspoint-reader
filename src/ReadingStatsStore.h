#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>
#include <vector>

// Reading statistics, persisted to /.crosspoint/reading-stats.json.
//
// Schema follows CrossMux's two-level aggregation (per-day + per-book) with
// CrossPoint's own capture pipeline: ReaderSession (anchor-based dwell timing,
// already unit-tested) folds finished sessions in at flush time — one data
// source, no duplicate session accounting.
struct ReadingStatsDay {
  std::string date;  // "YYYY-MM-DD" (device local time)
  uint32_t seconds;
  uint32_t pages;
  uint16_t sessions;

  bool operator==(const ReadingStatsDay& other) const { return date == other.date; }
};

struct ReadingStatsBookDay {
  char bookId[41] = {};
  uint32_t dayOrdinal = 0;
  uint32_t seconds = 0;
};

struct ReadingStatsBook {
  std::string bookId;    // KOReader document id (32 hex chars), stable across moves
  std::string path;      // most recent known path (display fallback only)
  std::string title;
  std::string author;
  uint32_t seconds;
  uint32_t pages;
  uint16_t sessions;
  int64_t lastReadAt;    // trusted epoch of the most recent session end
  uint16_t lastProgressBp;
};

class ReadingStatsStore : public PersistableStore<ReadingStatsStore> {
 public:
  // Keep a year of daily totals, 128 books, and 128 per-book/day detail rows.
  // The detail table's fixed payload is about 6.5 KB; its independent cap also
  // bounds the persisted JSON snapshot on low-memory boards.
  static constexpr size_t MAX_DAYS = 365;
  static constexpr size_t MAX_BOOKS = 128;
  static constexpr size_t MAX_BOOK_DAY_ENTRIES = 128;

 private:
  std::vector<ReadingStatsDay> days;  // sorted by date ascending (newest last)
  std::vector<ReadingStatsBook> books;  // sorted by lastReadAt descending
  std::vector<ReadingStatsBookDay> bookDays;  // most recent book/day pairs, capped independently
  uint64_t totalSeconds = 0;
  uint64_t totalPages = 0;

  ReadingStatsStore() = default;
  ~ReadingStatsStore() = default;

  friend class PersistableStore<ReadingStatsStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/reading-stats.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Fold one finished reading session in. sessionEndEpoch is the trusted epoch
  // of the session's last render; the local-timezone date of that instant owns
  // the whole session (CrossMux convention). bookId may be empty when the
  // document hash is unavailable — the session still counts toward days.
  // Persists on success (flushes are infrequent — reader exit / sleep only).
  void recordSession(int64_t sessionEndEpoch, uint32_t durationSeconds, uint32_t pages, const char* bookId,
                     const char* bookPath, const char* title, const char* author, uint16_t endProgressBp);
  bool adjustBookReadingTime(const char* bookId, const char* date, int32_t deltaSeconds);

  // Chronological, oldest first, at most MAX_DAYS entries.
  const std::vector<ReadingStatsDay>& getDays() const { return days; }
  // Most recently read first, at most MAX_BOOKS entries.
  const std::vector<ReadingStatsBook>& getBooks() const { return books; }
  const std::vector<ReadingStatsBookDay>& getBookDays() const { return bookDays; }
  const ReadingStatsBook* findBookById(const char* bookId) const;
  static bool getDayOrdinal(const char* date, uint32_t& ordinal);
  static bool formatDayOrdinal(uint32_t ordinal, char* out, size_t outSize);

  const ReadingStatsDay* findDay(const char* date) const;

  // Aggregates over the trailing window of calendar days (zeroes when absent).
  void sumRecent(size_t dayCount, uint32_t& seconds, uint32_t& pages) const;
  void sumAll(uint32_t& seconds, uint32_t& pages) const;

  // Consecutive days with >0 seconds ending today (or yesterday, so a morning
  // reader doesn't see the streak die at 00:00); 0 when no data.
  int currentStreak() const;
  int currentGoalStreak(uint32_t dailyGoalSeconds) const;
  int maxGoalStreak(uint32_t dailyGoalSeconds) const;
};

// Helper macro to access reading stats store
#define READING_STATS ReadingStatsStore::getInstance()
