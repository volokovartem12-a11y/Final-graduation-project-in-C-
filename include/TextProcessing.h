#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace text_processing {
std::vector<std::string> split_words(const std::string& text);
// The input format permits lowercase ASCII words separated by whitespace.
void validate_text(const std::string& text, std::size_t max_words,
                   bool allow_empty = true);
}  // namespace text_processing
