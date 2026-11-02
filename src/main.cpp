#include "ConverterJSON.h"
#include "InvertedIndex.h"
#include "SearchServer.h"

#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
volatile std::sig_atomic_t stop_requested = 0;
void request_stop(int) { stop_requested = 1; }

struct Options {
    std::filesystem::path config_path = "config.json";
    std::filesystem::path requests_path = "requests.json";
    std::filesystem::path answers_path = "answers.json";
    MatchMode match_mode = MatchMode::All;
    bool watch = false;
    bool help = false;
};

std::vector<std::string> get_arguments(int argc, char* argv[]) {
#ifdef _WIN32
    (void)argc;
    (void)argv;
    int argument_count = 0;
    std::unique_ptr<wchar_t*, decltype(&LocalFree)> native_arguments(
        CommandLineToArgvW(GetCommandLineW(), &argument_count), &LocalFree);
    if (!native_arguments) {
        throw std::runtime_error("could not read command line");
    }
    std::vector<std::string> arguments;
    for (int i = 0; i < argument_count; ++i) {
        arguments.push_back(std::filesystem::path(native_arguments.get()[i]).u8string());
    }
    return arguments;
#else
    return {argv, argv + argc};
#endif
}

Options parse_options(const std::vector<std::string>& arguments) {
    Options options;
    for (std::size_t i = 1; i < arguments.size(); ++i) {
        const auto& argument = arguments[i];
        if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else if (argument == "--watch") {
            options.watch = true;
        } else if (argument == "--config" || argument == "--requests" ||
                   argument == "--answers" || argument == "--match") {
            if (++i >= arguments.size() || arguments[i].empty()) {
                throw std::runtime_error("missing value for " + argument);
            }
            const auto& value = arguments[i];
            if (argument == "--config") {
                options.config_path = std::filesystem::u8path(value);
            } else if (argument == "--requests") {
                options.requests_path = std::filesystem::u8path(value);
            } else if (argument == "--answers") {
                options.answers_path = std::filesystem::u8path(value);
            } else if (value == "all" || value == "any") {
                options.match_mode = value == "all" ? MatchMode::All : MatchMode::Any;
            } else {
                throw std::runtime_error("--match must be all or any");
            }
        } else {
            throw std::runtime_error("unknown option: " + argument);
        }
    }
    return options;
}

void show_help() {
    std::cout << "Local document search engine 0.1\n"
              << "Usage: search_engine [options]\n"
              << "  --config PATH    Configuration (default: config.json)\n"
              << "  --requests PATH  Queries (default: requests.json)\n"
              << "  --answers PATH   Output (default: answers.json)\n"
              << "  --match all|any  Match all words (default) or any word\n"
              << "  --watch          Reload inputs at config.update_interval seconds\n"
              << "  --help           Show this help\n";
}
}  // namespace

int main(int argc, char* argv[]) {
    try {
        const auto options = parse_options(get_arguments(argc, argv));
        if (options.help) {
            show_help();
            return 0;
        }
        std::signal(SIGINT, request_stop);
        std::signal(SIGTERM, request_stop);
        InvertedIndex index;
        bool first_run = true;
        do {
            // Reconstructing the converter reloads configuration in watch mode.
            ConverterJSON converter(options.config_path, options.requests_path, options.answers_path);
            const auto response_limit = converter.GetResponsesLimit();
            if (first_run) {
                std::cout << "Starting " << converter.GetEngineName() << " 0.1\n";
                first_run = false;
            }
            const auto requests = converter.GetRequests();
            index.UpdateDocumentBase(converter.GetTextDocuments());
            SearchServer server(index, static_cast<std::size_t>(response_limit), options.match_mode);
            const auto results = server.search(requests);
            std::vector<std::vector<std::pair<int, float>>> answers;
            for (std::size_t i = 0; i < results.size(); ++i) {
                std::vector<std::pair<int, float>> answer;
                for (const auto& entry : results[i]) {
                    answer.emplace_back(static_cast<int>(entry.doc_id), entry.rank);
                }
                answers.push_back(std::move(answer));
                std::cout << "Request " << i + 1 << ": found " << server.GetResultCounts()[i]
                          << ", returned " << results[i].size() << '\n';
            }
            converter.putAnswers(std::move(answers));
            std::cout << "Answers written to " << options.answers_path.u8string() << std::endl;
            if (!options.watch) {
                break;
            }
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::seconds(converter.GetUpdateInterval());
            while (!stop_requested && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        } while (!stop_requested);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
