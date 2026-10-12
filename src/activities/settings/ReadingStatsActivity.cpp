#include "ReadingStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "ReadingHeatmapActivity.h"
#include "ReadingStatsDetailActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {

// CrossMux-style duration formatting: "Xh Ym" past the hour, bare minutes
// below it, "<1 min" for sub-minute readings.
void formatDuration(uint32_t seconds, char (&out)[16]) {
  if (seconds >= 3600) {
    snprintf(out, sizeof(out), tr(STR_STATS_DURATION_HOURS_FORMAT), static_cast<unsigned long>(seconds / 3600),
             static_cast<unsigned long>((seconds % 3600) / 60));
  } else if (seconds >= 60) {
    snprintf(out, sizeof(out), tr(STR_STATS_DURATION_MINUTES_FORMAT), static_cast<unsigned long>(seconds / 60));
  } else {
    snprintf(out, sizeof(out), "%s", tr(STR_STATS_DURATION_LESS_THAN_MINUTE));
  }
}

void formatDayValue(uint32_t seconds, uint32_t pages, std::string& out) {
  char dur[16];
  formatDuration(seconds, dur);
  char buf[40];
  snprintf(buf, sizeof(buf), tr(STR_STATS_PAGES_FORMAT), dur, static_cast<unsigned long>(pages));
  out = buf;
}

fui::ListItem makeRow(const char* label, const char* value) {
  fui::ListItem item;
  item.label = label;
  item.value = value;
  return item;
}

}  // namespace

ReadingStatsActivity::ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("ReadingStats", renderer, mappedInput) {}

void ReadingStatsActivity::onEnter() {
  UiListActivity::onEnter();
  rebuildRows();
  nav.reset(0);
}

void ReadingStatsActivity::rebuildRows() {
  valueStorage_.clear();
  rowItems_.clear();
  recentDayDates_.clear();
  visibleBookIds_.clear();
  heatmapIndex_ = -1;
  recentDayRowsStart_ = -1;
  bookRowsStart_ = -1;

  // Rows hold const char* pointers into valueStorage_; deque growth never
  // moves existing elements, so every pointer stays valid.

  // --- summary metrics (CrossMux's summary cards, as rows) ---
  uint32_t seconds = 0;
  uint32_t pages = 0;

  READING_STATS.sumRecent(1, seconds, pages);
  char goalValue[40];
  char goalDuration[16];
  char todayDuration[16];
  formatDuration(static_cast<uint32_t>(SETTINGS.dailyReadingGoalMinutes) * 60, goalDuration);
  formatDuration(seconds, todayDuration);
  snprintf(goalValue, sizeof(goalValue), "%s / %s", todayDuration, goalDuration);
  valueStorage_.emplace_back(goalValue);
  rowItems_.push_back(makeRow(tr(STR_STATS_DAILY_GOAL), valueStorage_.back().c_str()));
  heatmapIndex_ = static_cast<int>(rowItems_.size());
  rowItems_.push_back(makeRow(tr(STR_STATS_HEATMAP), nullptr));

  valueStorage_.emplace_back();
  formatDayValue(seconds, pages, valueStorage_.back());
  rowItems_.push_back(makeRow(tr(STR_STATS_TODAY), valueStorage_.back().c_str()));

  READING_STATS.sumRecent(7, seconds, pages);
  valueStorage_.emplace_back();
  formatDayValue(seconds, pages, valueStorage_.back());
  rowItems_.push_back(makeRow(tr(STR_STATS_LAST_7_DAYS), valueStorage_.back().c_str()));

  READING_STATS.sumRecent(30, seconds, pages);
  valueStorage_.emplace_back();
  formatDayValue(seconds, pages, valueStorage_.back());
  rowItems_.push_back(makeRow(tr(STR_STATS_LAST_30_DAYS), valueStorage_.back().c_str()));

  READING_STATS.sumAll(seconds, pages);
  valueStorage_.emplace_back();
  formatDayValue(seconds, pages, valueStorage_.back());
  rowItems_.push_back(makeRow(tr(STR_STATS_ALL_TIME), valueStorage_.back().c_str()));

  {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", READING_STATS.currentStreak());
    valueStorage_.emplace_back(buf);
    rowItems_.push_back(makeRow(tr(STR_STATS_DAY_STREAK), valueStorage_.back().c_str()));
  }
  {
    const uint32_t goalSeconds = static_cast<uint32_t>(SETTINGS.dailyReadingGoalMinutes) * 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d / %d", READING_STATS.currentGoalStreak(goalSeconds),
             READING_STATS.maxGoalStreak(goalSeconds));
    valueStorage_.emplace_back(buf);
    rowItems_.push_back(makeRow(tr(STR_STATS_GOAL_STREAK), valueStorage_.back().c_str()));
  }

  // --- recent days section (rolling window, newest first) ---
  valueStorage_.emplace_back(tr(STR_STATS_RECENT_DAYS));
  rowItems_.push_back(makeRow(valueStorage_.back().c_str(), nullptr));
  rowItems_.back().isHeader = true;
  recentDayRowsStart_ = static_cast<int>(rowItems_.size());

  const auto& days = READING_STATS.getDays();
  constexpr size_t MAX_RECENT_DAY_ROWS = 14;
  size_t shown = 0;
  for (auto it = days.rbegin(); it != days.rend() && shown < MAX_RECENT_DAY_ROWS; ++it, ++shown) {
    char dateLabel[8];
    snprintf(dateLabel, sizeof(dateLabel), "%.5s", it->date.c_str() + 5);  // "MM-DD"
    valueStorage_.emplace_back(dateLabel);
    valueStorage_.emplace_back();
    formatDayValue(it->seconds, it->pages, valueStorage_.back());
    rowItems_.push_back(makeRow((valueStorage_.end() - 2)->c_str(), valueStorage_.back().c_str()));
    recentDayDates_.push_back(it->date);
  }

  // --- books section (most recently read first) ---
  valueStorage_.emplace_back(tr(STR_STATS_BOOKS));
  rowItems_.push_back(makeRow(valueStorage_.back().c_str(), nullptr));
  rowItems_.back().isHeader = true;
  bookRowsStart_ = static_cast<int>(rowItems_.size());

  const auto& books = READING_STATS.getBooks();
  constexpr size_t MAX_BOOK_ROWS = 20;
  size_t bookRows = 0;
  for (const auto& book : books) {
    if (bookRows >= MAX_BOOK_ROWS) break;
    char dur[16];
    formatDuration(book.seconds, dur);
    char value[32];
    snprintf(value, sizeof(value), "%s / %u%%", dur, book.lastProgressBp / 100);
    valueStorage_.emplace_back(book.title);
    valueStorage_.emplace_back(book.author);
    valueStorage_.emplace_back(value);
    fui::ListItem item;
    item.label = (valueStorage_.end() - 3)->c_str();
    item.subtitle = (valueStorage_.end() - 2)->c_str();
    item.value = valueStorage_.back().c_str();
    rowItems_.push_back(item);
    visibleBookIds_.push_back(book.bookId);
    bookRows++;
  }

  if (days.empty() && books.empty()) rowItems_.push_back(makeRow(tr(STR_STATS_NO_DATA), nullptr));

  if (rowItems_.empty()) {
    rowItems_.push_back(makeRow(tr(STR_STATS_NO_DATA), nullptr));
  }
}

int ReadingStatsActivity::listCount() const { return static_cast<int>(rowItems_.size()); }

const char* ReadingStatsActivity::headerTitle() const { return tr(STR_READING_STATS); }

void ReadingStatsActivity::activateIndex(int index) {
  if (index == heatmapIndex_) {
    auto activity = makeUniqueNoThrow<ReadingHeatmapActivity>(renderer, mappedInput);
    if (!activity) {
      LOG_ERR("STATS", "OOM: reading heatmap");
      return;
    }
    app.clearTapFlash();
    startActivityForResult(std::move(activity), [this](const ActivityResult&) { RenderLock lock(*this); rebuildRows(); });
    return;
  }
  const int dayIndex = index - recentDayRowsStart_;
  if (dayIndex >= 0 && static_cast<size_t>(dayIndex) < recentDayDates_.size()) {
    auto activity = makeUniqueNoThrow<ReadingStatsDetailActivity>(renderer, mappedInput, "", recentDayDates_[dayIndex]);
    if (!activity) {
      LOG_ERR("STATS", "OOM: reading-day detail");
      return;
    }
    app.clearTapFlash();
    startActivityForResult(std::move(activity), [this](const ActivityResult&) { RenderLock lock(*this); rebuildRows(); });
    return;
  }
  const int bookIndex = index - bookRowsStart_;
  if (bookIndex >= 0 && static_cast<size_t>(bookIndex) < visibleBookIds_.size()) {
    auto activity = makeUniqueNoThrow<ReadingStatsDetailActivity>(renderer, mappedInput, visibleBookIds_[bookIndex], "");
    if (!activity) {
      LOG_ERR("STATS", "OOM: book reading stats detail");
      return;
    }
    app.clearTapFlash();
    startActivityForResult(std::move(activity), [this](const ActivityResult&) { RenderLock lock(*this); rebuildRows(); });
  }
}

void ReadingStatsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 1;
  syncListViewport(screen, props);
  screen.list(props);
}