#pragma once

#include "InvertedIndex.h"

#include <cstddef>
#include <string>
#include <vector>

struct RelativeIndex {
    std::size_t doc_id;
    float rank;

    bool operator==(const RelativeIndex& other) const {
        return doc_id == other.doc_id && rank == other.rank;
    }
};

enum class MatchMode { All, Any };

class SearchServer {
public:
    explicit SearchServer(InvertedIndex& index, std::size_t response_limit = 5,
                          MatchMode match_mode = MatchMode::All);
    std::vector<std::vector<RelativeIndex>> search(
        const std::vector<std::string>& queries_input);
    const std::vector<std::size_t>& GetResultCounts() const;

private:
    InvertedIndex& index;
    std::size_t response_limit;
    MatchMode match_mode;
    std::vector<std::size_t> result_counts;
};
