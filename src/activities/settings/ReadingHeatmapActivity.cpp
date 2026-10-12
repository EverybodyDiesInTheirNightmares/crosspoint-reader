#include "ReadingHeatmapActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "ReadingStatsDetailActivity.h"
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

ReadingHeatmapActivity::ReadingHeatmapActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               std::string month)
    : UiListActivity("ReadingHeatmap", renderer, mappedInput), month_(std::move(month)) {}

void ReadingHeatmapActivity::onEnter() {
  UiListActivity::onEnter();
  rebuildRows();
  nav.reset(0);
}

void ReadingHeatmapActivity::rebuildRows() {
  valueStorage_.clear();
  rowItems_.clear();
  months_.clear();
  dates_.clear();

  const auto& days = READING_STATS.getDays();
  if (days.empty()) {
    rowItems_.push_back(makeRow(tr(STR_STATS_NO_DATA), nullptr));
    return;
  }

  if (!month_.empty()) {
    for (auto it = days.rbegin(); it != days.rend(); ++it) {
      if (it->date.compare(0, 7, month_) != 0) continue;
      char duration[16];
      char value[40];
      formatDuration(it->seconds, duration);
      snprintf(value, sizeof(value), tr(STR_STATS_PAGES_FORMAT), duration, static_cast<unsigned long>(it->pages));
      uint32_t ordinal = 0;
      char dateLabel[16];
      if (!ReadingStatsStore::getDayOrdinal(it->date.c_str(), ordinal) ||
          !ReadingStatsStore::formatDayOrdinal(ordinal, dateLabel, sizeof(dateLabel))) {
        continue;
      }
      valueStorage_.emplace_back(dateLabel);
      valueStorage_.emplace_back(value);
      rowItems_.push_back(makeRow((valueStorage_.end() - 2)->c_str(), valueStorage_.back().c_str()));
      dates_.push_back(it->date);
    }
    if (rowItems_.empty()) rowItems_.push_back(makeRow(tr(STR_STATS_NO_DATA), nullptr));
    return;
  }

  const uint32_t goalSeconds = static_cast<uint32_t>(SETTINGS.dailyReadingGoalMinutes) * 60;
  for (auto it = days.rbegin(); it != days.rend();) {
    const std::string monthKey = it->date.substr(0, 7);
    char heat[32];
    std::fill(std::begin(heat), std::end(heat), '.');
    uint32_t daysRead = 0;
    uint32_t monthSeconds = 0;
    while (it != days.rend() && it->date.compare(0, 7, monthKey) == 0) {
      unsigned dayNumber = 0;
      if (sscanf(it->date.c_str() + 8, "%2u", &dayNumber) == 1 && dayNumber >= 1 && dayNumber <= 31) {
        static constexpr char INTENSITY[] = ".:-=+#";
        const uint32_t level = it->seconds == 0
                   ? 0
                   : goalSeconds == 0
                     ? 5
                     : std::max<uint32_t>(1, std::min<uint32_t>(5, (it->seconds * 5ULL) / goalSeconds));
        heat[dayNumber - 1] = INTENSITY[level];
      }
      if (it->seconds > 0) daysRead++;
      monthSeconds += it->seconds;
      ++it;
    }
    heat[31] = '\0';
    char summary[40];
    char duration[16];
    formatDuration(monthSeconds, duration);
    snprintf(summary, sizeof(summary), tr(STR_STATS_DAYS_FORMAT), static_cast<unsigned long>(daysRead), duration);
    valueStorage_.emplace_back(monthKey);
    valueStorage_.emplace_back(summary);
    valueStorage_.emplace_back(heat);
    fui::ListItem item = makeRow(valueStorage_[valueStorage_.size() - 3].c_str(), valueStorage_.back().c_str());
    item.subtitle = (valueStorage_.end() - 2)->c_str();
    rowItems_.push_back(item);
    months_.push_back(monthKey);
  }
}

int ReadingHeatmapActivity::listCount() const { return static_cast<int>(rowItems_.size()); }

const char* ReadingHeatmapActivity::headerTitle() const { return tr(STR_STATS_HEATMAP); }

void ReadingHeatmapActivity::activateIndex(const int index) {
  if (!month_.empty()) {
    if (index < 0 || static_cast<size_t>(index) >= dates_.size()) return;
    auto activity = makeUniqueNoThrow<ReadingStatsDetailActivity>(renderer, mappedInput, "", dates_[index]);
    if (!activity) {
      LOG_ERR("STATS", "OOM: reading-day detail");
      return;
    }
    app.clearTapFlash();
    startActivityForResult(std::move(activity), [this](const ActivityResult&) { RenderLock lock(*this); rebuildRows(); });
    return;
  }
  if (index < 0 || static_cast<size_t>(index) >= months_.size()) return;
  auto activity = makeUniqueNoThrow<ReadingHeatmapActivity>(renderer, mappedInput, months_[index]);
  if (!activity) {
    LOG_ERR("STATS", "OOM: reading-month detail");
    return;
  }
  app.clearTapFlash();
  startActivityForResult(std::move(activity), [this](const ActivityResult&) { RenderLock lock(*this); rebuildRows(); });
}

void ReadingHeatmapActivity::buildScreen(UiScreen& screen) {
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