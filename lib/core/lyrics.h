#pragma once

#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <string>

#include "text_fold.h"

// Line-synced lyrics in LRC format ("[01:23.45] text"), as LRCLIB returns them in syncedLyrics.
namespace lyrics {

struct Line {
  uint32_t timeMs;
  uint16_t text;  // offset of the NUL-terminated text in Synced::text; "" marks a pause
};

inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Reads up to maxDigits decimal digits at p; digits gets how many were read.
inline const char *readNumber(const char *p, const char *end, int maxDigits, uint32_t &value, int &digits) {
  value = 0;
  digits = 0;
  while (p < end && digits < maxDigits && isDigit(*p)) {
    value = value * 10 + (*p - '0');
    ++p;
    ++digits;
  }
  return p;
}

// Parses a time tag such as [01:23.45] (also [1:23], [01:23.4], [01:23.456] and [01:23:45]) at p.
// Returns the position after ']' and sets ms, or nullptr when p isn't a time tag (e.g. [ar:...]).
inline const char *parseTimeTag(const char *p, const char *end, uint32_t &ms) {
  if (p >= end || *p != '[') return nullptr;
  uint32_t minutes = 0;
  uint32_t seconds = 0;
  uint32_t fraction = 0;
  int digits = 0;
  p = readNumber(p + 1, end, 3, minutes, digits);
  if (!digits || p >= end || *p != ':') return nullptr;
  p = readNumber(p + 1, end, 2, seconds, digits);
  if (!digits || seconds > 59) return nullptr;
  if (p < end && (*p == '.' || *p == ':')) {
    p = readNumber(p + 1, end, 3, fraction, digits);
    if (!digits) return nullptr;
    for (; digits < 3; ++digits) fraction *= 10;  // tenths or hundredths -> ms
  }
  if (p >= end || *p != ']') return nullptr;
  ms = (minutes * 60 + seconds) * 1000 + fraction;
  return p + 1;
}

// Parsed lyrics, sorted by time. Plain data without constructors so it can live in a raw PSRAM
// allocation; parse() initializes it.
template <int MaxLines, int MaxText>
struct Synced {
  static_assert(MaxText > 1 && MaxText <= 65536, "line offsets are 16-bit");

  int count;
  Line lines[MaxLines];
  char text[MaxText];  // text[0] is the "" shared by every pause

  // Lines that don't start with a time tag (metadata, blank or malformed lines) are skipped; a line
  // with several tags is repeated at each time. The text is folded to ASCII for the bundled fonts; a
  // line left empty (an instrumental gap, a non-Latin script) becomes a pause. Lines beyond MaxLines
  // or MaxText are dropped. Returns true when at least one line has text.
  bool parse(const char *lrc) {
    count = 0;
    text[0] = '\0';
    int used = 1;
    while (*lrc) {
      const char *end = lrc;
      while (*end && *end != '\n') ++end;
      addLine(lrc, end, used);
      lrc = *end ? end + 1 : end;
    }
    std::stable_sort(lines, lines + count, [](const Line &a, const Line &b) { return a.timeMs < b.timeMs; });
    return neighbor(-1, 1) >= 0;
  }

  // Index of the line being sung at positionMs, or -1 before the first one.
  int lineAt(long positionMs) const {
    int lo = 0;
    int hi = count;  // binary search for the first line that starts after positionMs
    while (lo < hi) {
      const int mid = (lo + hi) / 2;
      if (static_cast<long>(lines[mid].timeMs) <= positionMs) lo = mid + 1;
      else hi = mid;
    }
    return lo - 1;
  }

  const char *textOf(int index) const { return text + lines[index].text; }
  bool isPause(int index) const { return !text[lines[index].text]; }

  // The nearest line with text before (step -1) or after (step 1) index, or -1 when there is none.
  int neighbor(int index, int step) const {
    for (int i = index + step; i >= 0 && i < count; i += step) {
      if (!isPause(i)) return i;
    }
    return -1;
  }

 private:
  void addLine(const char *p, const char *end, int &used) {
    constexpr int maxTags = 8;
    uint32_t times[maxTags];
    int tags = 0;
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    uint32_t ms = 0;
    for (const char *next; (next = parseTimeTag(p, end, ms)) != nullptr; p = next) {
      if (tags < maxTags) times[tags++] = ms;
    }
    if (!tags || count >= MaxLines) return;
    const int offset = store(p, end, used);
    if (offset < 0) return;
    for (int t = 0; t < tags && count < MaxLines; ++t) lines[count++] = Line{times[t], static_cast<uint16_t>(offset)};
  }

  // Appends the text folded to ASCII, trimmed and with runs of spaces collapsed. Returns its offset
  // (0 for an empty line), or -1 when it doesn't fit.
  int store(const char *p, const char *end, int &used) {
    const std::string folded = asciiFold(std::string(p, end));
    const int start = used;
    for (char c : folded) {
      if (static_cast<uint8_t>(c) < 0x20 || c == 0x7F) c = ' ';
      if (c == ' ' && (used == start || text[used - 1] == ' ')) continue;
      if (used >= MaxText - 1) {  // keep room for the NUL
        used = start;
        return -1;
      }
      text[used++] = c;
    }
    if (used > start && text[used - 1] == ' ') --used;
    if (used == start) return 0;
    text[used++] = '\0';
    return start;
  }
};

struct Span {
  int start, end;  // [start, end) offsets into the wrapped text
};

// Greedy word wrap of ASCII text into the smallest block of rows (up to maxRows) that fits.
// widthOf(row, rows) is the pixel width of row `row` in a block of `rows` rows (on a round screen,
// rows far from the center are narrower); measure(text, length) is the pixel width of a run of text.
// Since the widths depend on the block size, a text can need a block of n rows yet fill only the
// first n - 1; the remaining rows are then empty (start == end) and the caller should still lay out
// the whole block. When even maxRows rows don't fit, the last row holds the rest for the caller to
// truncate. A word wider than its row gets a row of its own. Returns the block size, 0 for blank text.
template <typename WidthOf, typename Measure>
int wrapText(const char *text, int maxRows, WidthOf widthOf, Measure measure, Span *rows) {
  const int length = static_cast<int>(strlen(text));
  int count = 0;
  for (int total = 1; total <= maxRows; ++total) {
    count = 0;
    int next = 0;
    while (next < length && text[next] == ' ') ++next;
    if (next >= length) return 0;
    while (next < length && count < total) {
      const int start = next;
      int end = start;
      while (next < length) {
        int wordEnd = next;
        while (wordEnd < length && text[wordEnd] != ' ') ++wordEnd;
        if (end > start && measure(text + start, wordEnd - start) > widthOf(count, total)) break;
        end = wordEnd;
        next = wordEnd;
        while (next < length && text[next] == ' ') ++next;
      }
      rows[count++] = Span{start, end};
    }
    if (next >= length) {
      for (; count < total; ++count) rows[count] = Span{length, length};
      return total;
    }
  }
  if (count) {
    int last = length;
    while (last > 0 && text[last - 1] == ' ') --last;
    rows[count - 1].end = last;
  }
  return count;
}

}  // namespace lyrics
