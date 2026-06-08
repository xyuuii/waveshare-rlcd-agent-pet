#include "usage_visuals.h"

#include <ctype.h>
#include <string.h>

namespace {

int parsePercentAfterToken(const std::string& text, const char* token) {
  const size_t tokenPos = text.find(token);
  if (tokenPos == std::string::npos) {
    return -1;
  }

  size_t pos = tokenPos + strlen(token);
  while (pos < text.size() && text[pos] == ' ') {
    ++pos;
  }

  int value = 0;
  bool foundDigit = false;
  while (pos < text.size() && isdigit(static_cast<unsigned char>(text[pos]))) {
    foundDigit = true;
    value = (value * 10) + (text[pos] - '0');
    ++pos;
  }

  return foundDigit ? value : -1;
}

}  // namespace

QuotaUsage parseQuotaUsage(const std::string& text) {
  QuotaUsage usage;
  usage.fiveHourPercent = parsePercentAfterToken(text, "5H");
  usage.weekPercent = parsePercentAfterToken(text, "WK");
  return usage;
}
