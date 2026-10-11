#pragma once
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Read-only reading-stats page (CrossMux-style summary, list rendition):
// summary metric rows (today / 7d / 30d / all-time / streak), a recent-days
// section, and a most-recently-read books section.
class ReadingStatsActivity final : public UiListActivity {
 public:
  explicit ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  std::vector<std::string> valueStorage_;  // backs ListItem value pointers
  std::vector<freeink::ui::ListItem> rowItems_;
};
