#include "screen_layout.h"

namespace {

constexpr ScreenRect kOverviewPetPanel{278, 178, 110, 90};
constexpr ScreenRect kOverviewRightInfoCards[2]{
    {278, 32, 110, 56},
    {278, 98, 110, 68},
};
constexpr ScreenRect kOverviewTimeCard{12, 32, 256, 54};
constexpr ScreenRect kOverviewStatusCard{12, 94, 256, 72};
constexpr ScreenRect kOverviewMetricCards[3]{
    {12, 178, 256, 26},
    {12, 211, 256, 26},
    {12, 242, 256, 28},
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
      {card.x + 98, card.y + 13, card.w - 106, 4},
      {card.x + 98, card.y + 22, card.w - 106, 4},
  };
}
