#include "InvertedIndex.h"
#include "TextProcessing.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

void InvertedIndex::UpdateDocumentBase(std::vector<std::string> input_docs) {
    // Each worker writes only to its own document's map. The shared index is
    // replaced after joining, so scheduling cannot change postings order.
    std::vector<std::map<std::string, std::size_t>> frequencies(input_docs.size());
    std::atomic<std::size_t> next_document{0};
    std::atomic<bool> failed{false};
    std::exception_ptr failure;
    std::mutex failure_mutex;
    const auto worker = [&] {
        try {
            while (!failed.load()) {
                const auto doc_id = next_document.fetch_add(1);
                if (doc_id >= input_docs.size()) {
                    break;
                }
                for (const auto& word : text_processing::split_words(input_docs[doc_id])) {
                    ++frequencies[doc_id][word];
                }
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(failure_mutex);
            if (!failure) {
                failure = std::current_exception();
            }
            failed.store(true);
        }
    };

    const auto worker_count = std::min<std::size_t>(
        input_docs.size(), std::max(1u, std::thread::hardware_concurrency()));
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    try {
        for (std::size_t i = 0; i < worker_count; ++i) {
            workers.emplace_back(worker);
        }
    } catch (...) {
        failed.store(true);
        for (auto& thread : workers) {
            thread.join();
        }
        throw;
    }
    for (auto& thread : workers) {
        thread.join();
    }
    if (failure) {
        std::rethrow_exception(failure);
    }

    std::map<std::string, std::vector<Entry>> next_dictionary;
    for (std::size_t doc_id = 0; doc_id < frequencies.size(); ++doc_id) {
        for (const auto& [word, count] : frequencies[doc_id]) {
            next_dictionary[word].push_back({doc_id, count});
        }
    }
    docs = std::move(input_docs);
    freq_dictionary = std::move(next_dictionary);
}

std::vector<Entry> InvertedIndex::GetWordCount(const std::string& word) const {
    const auto found = freq_dictionary.find(word);
    return found == freq_dictionary.end() ? std::vector<Entry>{} : found->second;
}
