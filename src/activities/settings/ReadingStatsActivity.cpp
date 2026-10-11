#include "ReadingStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {

// CrossMux-style duration formatting: "Xh Ym" past the hour, bare minutes
// below it, "<1 min" for sub-minute readings.
void formatDuration(uint32_t seconds, char (&out)[16]) {
  if (seconds >= 3600) {
    snprintf(out, sizeof(out), "%luh %lum", static_cast<unsigned long>(seconds / 3600),
             static_cast<unsigned long>((seconds % 3600) / 60));
  } else if (seconds >= 60) {
    snprintf(out, sizeof(out), "%lu min", static_cast<unsigned long>(seconds / 60));
  } else {
    snprintf(out, sizeof(out), "<1 min");
  }
}

void formatDayValue(uint32_t seconds, uint32_t pages, std::string& out) {
  char dur[16];
  formatDuration(seconds, dur);
  char buf[40];
  snprintf(buf, sizeof(buf), "%s / %lu pages", dur, static_cast<unsigned long>(pages));
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

  valueStorage_.clear();
  rowItems_.clear();

  // Rows hold const char* pointers into valueStorage_; deque growth never
  // moves existing elements, so every pointer stays valid.

  // --- summary metrics (CrossMux's summary cards, as rows) ---
  uint32_t seconds = 0;
  uint32_t pages = 0;

  READING_STATS.sumRecent(1, seconds, pages);
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

  // --- recent days section (rolling window, newest first) ---
  valueStorage_.emplace_back(tr(STR_STATS_RECENT_DAYS));
  rowItems_.push_back(makeRow(valueStorage_.back().c_str(), nullptr));
  rowItems_.back().isHeader = true;

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
  }

  // --- books section (most recently read first) ---
  valueStorage_.emplace_back(tr(STR_STATS_BOOKS));
  rowItems_.push_back(makeRow(valueStorage_.back().c_str(), nullptr));
  rowItems_.back().isHeader = true;

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
    bookRows++;
  }

  if (rowItems_.empty()) {
    rowItems_.push_back(makeRow(tr(STR_STATS_NO_DATA), nullptr));
  }

  nav.reset(0);
}

int ReadingStatsActivity::listCount() const { return static_cast<int>(rowItems_.size()); }

const char* ReadingStatsActivity::headerTitle() const { return tr(STR_READING_STATS); }

void ReadingStatsActivity::activateIndex(int index) {
  // Read-only page; tapping a row just moves the selection.
  (void)index;
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
