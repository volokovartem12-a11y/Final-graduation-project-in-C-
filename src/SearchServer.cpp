#include "SearchServer.h"
#include "TextProcessing.h"

#include <algorithm>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <utility>

SearchServer::SearchServer(InvertedIndex& index, std::size_t response_limit,
                           MatchMode match_mode)
    : index(index), response_limit(response_limit), match_mode(match_mode) {
    if (response_limit == 0) {
        throw std::invalid_argument("response limit must be positive");
    }
}

std::vector<std::vector<RelativeIndex>> SearchServer::search(
    const std::vector<std::string>& queries_input) {
    struct WordEntries {
        std::vector<Entry> entries;
        std::size_t frequency;
    };
    std::vector<std::vector<RelativeIndex>> answers;
    result_counts.clear();
    for (const auto& query : queries_input) {
        const auto words = text_processing::split_words(query);
        const std::set<std::string> unique_words(words.begin(), words.end());
        std::vector<WordEntries> word_entries;
        for (const auto& word : unique_words) {
            auto entries = index.GetWordCount(word);
            const auto frequency = std::accumulate(
                entries.begin(), entries.end(), std::size_t{0},
                [](std::size_t sum, const Entry& entry) { return sum + entry.count; });
            word_entries.push_back({std::move(entries), frequency});
        }
        std::stable_sort(word_entries.begin(), word_entries.end(),
                         [](const auto& left, const auto& right) {
                             return left.frequency < right.frequency;
                         });

        std::map<std::size_t, std::size_t> scores;
        for (std::size_t i = 0; i < word_entries.size(); ++i) {
            std::map<std::size_t, std::size_t> next_scores;
            for (const auto& entry : word_entries[i].entries) {
                const auto found = scores.find(entry.doc_id);
                if (match_mode == MatchMode::Any) {
                    scores[entry.doc_id] += entry.count;
                } else if (i == 0 || found != scores.end()) {
                    next_scores[entry.doc_id] = entry.count +
                        (found == scores.end() ? 0 : found->second);
                }
            }
            if (match_mode == MatchMode::All) {
                scores = std::move(next_scores);
                if (scores.empty()) {
                    break;
                }
            }
        }

        result_counts.push_back(scores.size());
        std::vector<std::pair<std::size_t, std::size_t>> ranked(scores.begin(), scores.end());
        std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
            return left.second != right.second ? left.second > right.second
                                               : left.first < right.first;
        });
        std::vector<RelativeIndex> result;
        if (!ranked.empty()) {
            const auto maximum = static_cast<float>(ranked.front().second);
            for (std::size_t i = 0; i < std::min(response_limit, ranked.size()); ++i) {
                result.push_back({ranked[i].first, static_cast<float>(ranked[i].second) / maximum});
            }
        }
        answers.push_back(std::move(result));
    }
    return answers;
}

const std::vector<std::size_t>& SearchServer::GetResultCounts() const {
    return result_counts;
}
