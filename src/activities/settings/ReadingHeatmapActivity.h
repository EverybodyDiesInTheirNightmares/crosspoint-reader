#pragma once

#include <deque>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

class ReadingHeatmapActivity final : public UiListActivity {
 public:
  ReadingHeatmapActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string month = "");
  void onEnter() override;

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  void rebuildRows();

  std::string month_;
  std::deque<std::string> valueStorage_;
  std::vector<freeink::ui::ListItem> rowItems_;
  std::vector<std::string> months_;
  std::vector<std::string> dates_;
};