// Fuzzy matching for the quick-jump palette: a query of one or more words against a few fields of text (a window's
// title, its application, where it lives). Every word must match, in order, somewhere in some field; a field scores
// higher for consecutive letters and for letters that start a word. Pure, so the ranking is unit-tested.
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace krellis {

namespace fuzzy_detail {

/// UTF-8 to code points, lower-cased for the scripts a window title is likely to use (ASCII, Latin-1, Greek, Cyrillic).
/// Bytes that are not valid UTF-8 pass through as themselves.
inline std::u32string fold(const std::string &text)
{
    std::u32string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const auto b = static_cast<unsigned char>(text[i]);
        char32_t c = b;
        int extra = 0;
        if (b >= 0xF0 && b < 0xF8) { c = b & 0x07; extra = 3; }
        else if (b >= 0xE0) { c = b & 0x0F; extra = 2; }
        else if (b >= 0xC0) { c = b & 0x1F; extra = 1; }
        if (extra && i + size_t(extra) < text.size()) {
            bool ok = true;
            for (int k = 1; k <= extra; ++k) {
                const auto cb = static_cast<unsigned char>(text[i + size_t(k)]);
                if ((cb & 0xC0) != 0x80) { ok = false; break; }
                c = (c << 6) | (cb & 0x3F);
            }
            if (ok) i += size_t(extra) + 1;
            else { c = b; ++i; }
        } else {
            c = b;
            ++i;
        }
        if (c >= 'A' && c <= 'Z') c += 32;
        else if ((c >= 0xC0 && c <= 0xDE && c != 0xD7) || (c >= 0x391 && c <= 0x3A9) || (c >= 0x410 && c <= 0x42F)) c += 0x20;
        else if (c >= 0x400 && c <= 0x40F) c += 0x50;
        out += c;
    }
    return out;
}

inline bool separator(char32_t c)
{
    return c == ' ' || c == '-' || c == '_' || c == '/' || c == '.' || c == ':' || c == '|' || c == '\t' || c == '(' || c == '[' || c == 0x2013 || c == 0x2014;
}

/// One word against one field: the best way to lay the word's letters, in order, over the field's. nullopt if it
/// cannot be done.
inline std::optional<int> wordScore(const std::u32string &word, const std::u32string &field)
{
    constexpr int base = 16, boundary = 22, consecutive = 18, gap = 1, none = -1000000;
    const size_t m = word.size(), n = field.size();
    if (m == 0) return 0;
    if (m > n) return std::nullopt;
    // best[j]: the best score with the current letter of the word placed at field[j]; run[j]: the best score of the
    // previous letter anywhere at or before j, less the gap penalty for the distance to j.
    std::vector<int> best(n, none), prev(n, none), run(n, none);
    for (size_t i = 0; i < m; ++i) {
        std::fill(best.begin(), best.end(), none);
        for (size_t j = i; j < n; ++j) {
            if (field[j] != word[i]) continue;
            int score = base;
            if (j == 0 || separator(field[j - 1])) score += boundary;
            if (i == 0) {
                best[j] = score - int(std::min<size_t>(j, 12)); // a late start costs a little
                continue;
            }
            int from = none;
            if (j >= 1 && prev[j - 1] > none) from = prev[j - 1] + consecutive;
            if (j >= 2 && run[j - 2] > none) from = std::max(from, run[j - 2] - gap);
            if (from > none) best[j] = score + from;
        }
        if (i + 1 < m) {
            prev = best;
            for (size_t j = 0; j < n; ++j) {
                run[j] = std::max(prev[j], j ? run[j - 1] - gap : none);
            }
        }
    }
    const int top = *std::max_element(best.begin(), best.end());
    if (top <= none / 2) return std::nullopt;
    return top;
}

} // namespace fuzzy_detail

struct FuzzyField {
    std::string text;
    double weight = 1; // a match in a title counts for more than one in the application name
};

/// The query split into words and matched against the fields. Every word must match some field; its score is the best
/// weighted one. An empty query matches everything with score 0.
inline std::optional<int> fuzzyScore(const std::string &query, const std::vector<FuzzyField> &fields)
{
    const std::u32string q = fuzzy_detail::fold(query);
    std::vector<std::u32string> words;
    std::u32string current;
    for (char32_t c : q) {
        if (c == ' ' || c == '\t') {
            if (!current.empty()) words.push_back(std::move(current));
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) words.push_back(std::move(current));
    if (words.empty()) return 0;
    std::vector<std::u32string> folded;
    for (const auto &f : fields) folded.push_back(fuzzy_detail::fold(f.text));
    double total = 0;
    for (const auto &word : words) {
        std::optional<double> best;
        for (size_t i = 0; i < folded.size(); ++i)
            if (const auto s = fuzzy_detail::wordScore(word, folded[i]))
                best = std::max(best.value_or(-1e9), double(*s) * fields[i].weight);
        if (!best) return std::nullopt;
        total += *best;
    }
    return int(total);
}

/// Indices of the candidates that match, best first; ties keep their given order (so the caller's recency order
/// decides among equals). An empty query keeps every candidate in order.
inline std::vector<size_t> fuzzyRank(const std::string &query, const std::vector<std::vector<FuzzyField>> &candidates)
{
    std::vector<std::pair<int, size_t>> scored;
    for (size_t i = 0; i < candidates.size(); ++i)
        if (const auto s = fuzzyScore(query, candidates[i])) scored.emplace_back(*s, i);
    std::stable_sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    std::vector<size_t> out;
    out.reserve(scored.size());
    for (const auto &[score, index] : scored) out.push_back(index);
    return out;
}

} // namespace krellis
