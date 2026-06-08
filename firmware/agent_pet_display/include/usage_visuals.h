#pragma once

#include <string>

struct QuotaUsage {
  int fiveHourPercent{-1};
  int weekPercent{-1};
};

QuotaUsage parseQuotaUsage(const std::string& text);
