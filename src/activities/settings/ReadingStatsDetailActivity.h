#pragma once

#include <deque>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

class ReadingStatsDetailActivity final : public UiListActivity {
 public:
  ReadingStatsDetailActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookId,
                             std::string date);
  void onEnter() override;

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  void rebuildRows();
  void launchCorrection(bool add);

  std::string bookId_;
  std::string date_;
  std::string correctionDate_;
  std::deque<std::string> valueStorage_;
  std::vector<freeink::ui::ListItem> rowItems_;
  std::vector<std::string> dayDates_;
  std::vector<std::string> dayBookIds_;
  int dayRowsStart_ = -1;
  int addTimeIndex_ = -1;
  int subtractTimeIndex_ = -1;
};