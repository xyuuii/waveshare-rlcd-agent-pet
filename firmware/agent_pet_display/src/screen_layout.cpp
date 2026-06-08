#include "screen_layout.h"

namespace {

constexpr ScreenRect kOverviewPetPanel{278, 176, 112, 96};
constexpr ScreenRect kOverviewRightInfoCards[2]{
    {278, 30, 112, 68},
    {278, 106, 112, 62},
};
constexpr ScreenRect kOverviewTimeCard{12, 30, 258, 70};
constexpr ScreenRect kOverviewStatusCard{12, 108, 258, 68};
constexpr ScreenRect kOverviewMetricCards[3]{
    {12, 184, 250, 24},
    {12, 212, 250, 24},
    {12, 240, 250, 28},
};

}  // namespace

ScreenRect overviewPetPanelRect() {
  return kOverviewPetPanel;
}

ScreenRect overviewRightInfoCardRect(int index) {
  if (index < 0) {
    return kOverviewRightInfoCards[0];
  }
  if (index > 1) {
    return kOverviewRightInfoCards[1];
  }
  return kOverviewRightInfoCards[index];
}

ScreenRect overviewTimeCardRect() {
  return kOverviewTimeCard;
}

ScreenRect overviewStatusCardRect() {
  return kOverviewStatusCard;
}

ScreenRect overviewMetricCardRect(int index) {
  if (index < 0) {
    return kOverviewMetricCards[0];
  }
  if (index > 2) {
    return kOverviewMetricCards[2];
  }
  return kOverviewMetricCards[index];
}

QuotaBarLayout overviewQuotaBarLayout() {
  const ScreenRect card = overviewMetricCardRect(2);
  return QuotaBarLayout{
      {card.x + 58, card.y + 14, card.w - 66, 4},
      {card.x + 58, card.y + 21, card.w - 66, 4},
  };
}
