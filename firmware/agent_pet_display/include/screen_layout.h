#pragma once

struct ScreenRect {
  int x;
  int y;
  int w;
  int h;
};

struct QuotaBarLayout {
  ScreenRect fiveHourBar;
  ScreenRect weekBar;
};

ScreenRect overviewPetPanelRect();
ScreenRect overviewRightInfoCardRect(int index);
ScreenRect overviewTimeCardRect();
ScreenRect overviewStatusCardRect();
ScreenRect overviewMetricCardRect(int index);
QuotaBarLayout overviewQuotaBarLayout();
