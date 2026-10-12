#include "ReadingStatsDetailActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {

void formatDuration(const uint32_t seconds, char (&out)[16]) {
  if (seconds >= 3600) {
    snprintf(out, sizeof(out), tr(STR_STATS_DURATION_HOURS_FORMAT), static_cast<unsigned long>(seconds / 3600),
             static_cast<unsigned long>((seconds % 3600) / 60));
  } else if (seconds >= 60) {
    snprintf(out, sizeof(out), tr(STR_STATS_DURATION_MINUTES_FORMAT), static_cast<unsigned long>(seconds / 60));
  } else {
    snprintf(out, sizeof(out), "%s", tr(STR_STATS_DURATION_LESS_THAN_MINUTE));
  }
}

fui::ListItem makeRow(const char* label, const char* value) {
  fui::ListItem item;
  item.label = label;
  item.value = value;
  return item;
}

}  // namespace

ReadingStatsDetailActivity::ReadingStatsDetailActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                       std::string bookId, std::string date)
    : UiListActivity("ReadingStatsDetail", renderer, mappedInput),
      bookId_(std::move(bookId)),
      date_(std::move(date)) {}

void ReadingStatsDetailActivity::onEnter() {
  UiListActivity::onEnter();
  rebuildRows();
  nav.reset(0);
}

void ReadingStatsDetailActivity::rebuildRows() {
  valueStorage_.clear();
  rowItems_.clear();
  dayDates_.clear();
  dayBookIds_.clear();
  dayRowsStart_ = -1;
  addTimeIndex_ = -1;
  subtractTimeIndex_ = -1;

  if (!bookId_.empty()) {
    const ReadingStatsBook* book = READING_STATS.findBookById(bookId_.c_str());
    if (!book) {
      rowItems_.push_back(makeRow(tr(STR_STATS_NO_DATA), nullptr));
      return;
    }

    char value[40];
    char duration[16];
    formatDuration(book->seconds, duration);
    snprintf(value, sizeof(value), tr(STR_STATS_PAGES_FORMAT), duration, static_cast<unsigned long>(book->pages));
    valueStorage_.emplace_back(value);
    rowItems_.push_back(makeRow(book->title.c_str(), valueStorage_.back().c_str()));

    snprintf(value, sizeof(value), "%u", book->sessions);
    valueStorage_.emplace_back(value);
    rowItems_.push_back(makeRow(tr(STR_STATS_SESSIONS), valueStorage_.back().c_str()));

    snprintf(value, sizeof(value), "%u%%", book->lastProgressBp / 100);
    valueStorage_.emplace_back(value);
    rowItems_.push_back(makeRow(tr(STR_STATS_LAST_PROGRESS), valueStorage_.back().c_str()));

    valueStorage_.emplace_back(tr(STR_STATS_RECENT_DAYS));
    rowItems_.push_back(makeRow(valueStorage_.back().c_str(), nullptr));
    rowItems_.back().isHeader = true;
    dayRowsStart_ = static_cast<int>(rowItems_.size());

    const auto& bookDays = READING_STATS.getBookDays();
    for (auto it = bookDays.rbegin(); it != bookDays.rend(); ++it) {
      if (it->bookId != bookId_) continue;
      char date[11];
      if (!ReadingStatsStore::formatDayOrdinal(it->dayOrdinal, date, sizeof(date))) continue;
      char durationValue[16];
      formatDuration(it->seconds, durationValue);
      valueStorage_.emplace_back(date);
      valueStorage_.emplace_back(durationValue);
      rowItems_.push_back(makeRow(valueStorage_[valueStorage_.size() - 2].c_str(), valueStorage_.back().c_str()));
      dayDates_.emplace_back(date);
    }

    if (dayDates_.empty()) {
      rowItems_.push_back(makeRow(tr(STR_STATS_NO_BOOK_DETAIL), nullptr));
      return;
    }
    // Keep a correction date that is still listed across rebuilds; otherwise
    // fall back to the entered date (book mode) or the newest listed day.
    if (std::find(dayDates_.begin(), dayDates_.end(), correctionDate_) == dayDates_.end()) {
      correctionDate_ = date_;
      if (correctionDate_.empty() && !dayDates_.empty()) correctionDate_ = dayDates_.front();
    }
    addTimeIndex_ = static_cast<int>(rowItems_.size());
    valueStorage_.emplace_back(correctionDate_);
    rowItems_.push_back(makeRow(tr(STR_STATS_ADD_TIME), valueStorage_.back().c_str()));
    subtractTimeIndex_ = static_cast<int>(rowItems_.size());
    valueStorage_.emplace_back(correctionDate_);
    rowItems_.push_back(makeRow(tr(STR_STATS_SUBTRACT_TIME), valueStorage_.back().c_str()));
    return;
  }

  const ReadingStatsDay* day = READING_STATS.findDay(date_.c_str());
  if (!day) {
    rowItems_.push_back(makeRow(tr(STR_STATS_NO_DATA), nullptr));
    return;
  }

  char dayValue[40];
  char duration[16];
  formatDuration(day->seconds, duration);
  snprintf(dayValue, sizeof(dayValue), tr(STR_STATS_PAGES_FORMAT), duration, static_cast<unsigned long>(day->pages));
  valueStorage_.emplace_back(dayValue);
  rowItems_.push_back(makeRow(date_.c_str(), valueStorage_.back().c_str()));

  valueStorage_.emplace_back(tr(STR_STATS_BOOKS));
  rowItems_.push_back(makeRow(valueStorage_.back().c_str(), nullptr));
  rowItems_.back().isHeader = true;
  dayRowsStart_ = static_cast<int>(rowItems_.size());

  uint32_t ordinal = 0;
  if (!ReadingStatsStore::getDayOrdinal(date_.c_str(), ordinal)) return;
  for (const auto& book : READING_STATS.getBooks()) {
    for (const auto& bookDay : READING_STATS.getBookDays()) {
      if (bookDay.dayOrdinal != ordinal || bookDay.bookId != book.bookId) continue;
      char durationValue[16];
      formatDuration(bookDay.seconds, durationValue);
      valueStorage_.emplace_back(book.title);
      valueStorage_.emplace_back(book.author);
      valueStorage_.emplace_back(durationValue);
      fui::ListItem item;
      item.label = (valueStorage_.end() - 3)->c_str();
      item.subtitle = (valueStorage_.end() - 2)->c_str();
      item.value = valueStorage_.back().c_str();
      rowItems_.push_back(item);
      dayBookIds_.push_back(book.bookId);
      dayDates_.push_back(date_);
      break;
    }
  }
  if (dayBookIds_.empty()) rowItems_.push_back(makeRow(tr(STR_STATS_NO_BOOK_DETAIL), nullptr));
}

void ReadingStatsDetailActivity::launchCorrection(const bool add) {
  if (correctionDate_.empty()) return;
  auto activity = makeUniqueNoThrow<IntervalSelectionActivity>(
      renderer, mappedInput, "ReadingStatsAdjustment", add ? StrId::STR_STATS_ADD_TIME : StrId::STR_STATS_SUBTRACT_TIME,
      15, 5, 180, 5, 15, StrId::STR_SLEEP_TIMER_VALUE_FORMAT);
  if (!activity) {
    LOG_ERR("STATS", "OOM: reading-time adjustment");
    return;
  }
  const std::string selectedDate = correctionDate_;
  startActivityForResult(std::move(activity), [this, add, selectedDate](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto* interval = std::get_if<IntervalResult>(&result.data);
    if (!interval) return;
    const int32_t seconds = static_cast<int32_t>(interval->value * 60);
    const bool saved = READING_STATS.adjustBookReadingTime(bookId_.c_str(), selectedDate.c_str(), add ? seconds : -seconds);
    if (!saved) {
      LOG_ERR("STATS", "Reading-time adjustment was rejected or failed to save");
      return;
    }
    {
      RenderLock lock(*this);  // Handler runs unlocked; rebuild must not race the render task.
      rebuildRows();
    }
    requestUpdate();
  });
}

int ReadingStatsDetailActivity::listCount() const { return static_cast<int>(rowItems_.size()); }

const char* ReadingStatsDetailActivity::headerTitle() const {
  return bookId_.empty() ? tr(STR_STATS_DAY_DETAIL) : tr(STR_STATS_BOOK_DETAIL);
}

void ReadingStatsDetailActivity::activateIndex(const int index) {
  if (bookId_.empty()) {
    const int detailIndex = index - dayRowsStart_;
    if (detailIndex < 0 || static_cast<size_t>(detailIndex) >= dayBookIds_.size()) return;
    auto activity =
      makeUniqueNoThrow<ReadingStatsDetailActivity>(renderer, mappedInput, dayBookIds_[detailIndex], date_);
    if (!activity) {
      LOG_ERR("STATS", "OOM: book reading stats detail");
      return;
    }
    app.clearTapFlash();
    startActivityForResult(std::move(activity), [](const ActivityResult&) {});
    return;
  }

  const int dayIndex = index - dayRowsStart_;
  if (dayIndex >= 0 && static_cast<size_t>(dayIndex) < dayDates_.size()) {
    correctionDate_ = dayDates_[dayIndex];
    {
      RenderLock lock(*this);  // loop() runs unlocked; rebuild must not race the render task.
      rebuildRows();
    }
    requestUpdate();
  } else if (index == addTimeIndex_) {
    launchCorrection(true);
  } else if (index == subtractTimeIndex_) {
    launchCorrection(false);
  }
}

void ReadingStatsDetailActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 1;
  syncListViewport(screen, props);
  screen.list(props);
}