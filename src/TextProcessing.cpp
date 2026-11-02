#include "TextProcessing.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

std::vector<std::string> text_processing::split_words(const std::string& text) {
    std::istringstream input(text);
    std::vector<std::string> words;
    for (std::string word; input >> word;) {
        words.push_back(std::move(word));
    }
    return words;
}

void text_processing::validate_text(const std::string& text, std::size_t max_words,
                                    bool allow_empty) {
    const auto words = split_words(text);
    if ((!allow_empty && words.empty()) || words.size() > max_words) {
        throw std::runtime_error("invalid word count");
    }
    for (const auto& word : words) {
        if (word.size() > 100 ||
            !std::all_of(word.begin(), word.end(), [](unsigned char letter) {
                return letter >= 'a' && letter <= 'z';
            })) {
            throw std::runtime_error("words must contain 1 to 100 lowercase Latin letters");
        }
    }
}
