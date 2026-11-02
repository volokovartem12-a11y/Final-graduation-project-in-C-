#include "ConverterJSON.h"
#include "TextProcessing.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {
using nlohmann::json;

std::string read_file(const std::filesystem::path& path, const std::string& missing_error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(missing_error);
    }
    std::string contents{std::istreambuf_iterator<char>(input), {}};
    if (input.bad()) {
        throw std::runtime_error("could not read file: " + path.u8string());
    }
    return contents;
}

json parse_json(const std::string& contents, const std::string& filename) {
    try {
        return json::parse(contents);
    } catch (const json::exception& error) {
        throw std::runtime_error(filename + " contains invalid JSON: " + error.what());
    }
}

int positive_integer(const json& value, const std::string& field) {
    if (!value.is_number_integer() || value < 1 ||
        value > std::numeric_limits<int>::max()) {
        throw std::runtime_error(field + " must be a positive integer");
    }
    return value.get<int>();
}

bool same_path(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::error_code error;
    if (std::filesystem::equivalent(left, right, error)) {
        return true;
    }
    return std::filesystem::weakly_canonical(left) == std::filesystem::weakly_canonical(right);
}
}  // namespace

ConverterJSON::ConverterJSON(std::filesystem::path config_path,
                             std::filesystem::path requests_path,
                             std::filesystem::path answers_path)
    : config_path(std::move(config_path)), requests_path(std::move(requests_path)),
      answers_path(std::move(answers_path)) {}

void ConverterJSON::load_config() {
    if (config_loaded) {
        return;
    }
    const auto contents = read_file(config_path, "config file is missing");
    if (contents.find_first_not_of(" \t\r\n") == std::string::npos) {
        throw std::runtime_error("config file is empty");
    }
    const auto root = parse_json(contents, "config.json");
    if (!root.is_object() || !root.contains("config") ||
        !root["config"].is_object() || root["config"].empty()) {
        throw std::runtime_error("config file is empty");
    }
    const auto& config = root["config"];
    if (!config.contains("version") || !config["version"].is_string() ||
        config["version"].get<std::string>() != "0.1") {
        throw std::runtime_error("config.json has incorrect file version");
    }
    if (!config.contains("name") || !config["name"].is_string() ||
        config["name"].get<std::string>().empty()) {
        throw std::runtime_error("config.name must be a non-empty string");
    }
    const auto next_limit = config.contains("max_responses")
        ? positive_integer(config["max_responses"], "config.max_responses") : 5;
    const auto next_interval = config.contains("update_interval")
        ? positive_integer(config["update_interval"], "config.update_interval") : 60;
    if (!root.contains("files") || !root["files"].is_array() ||
        root["files"].size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("files must be an array of file paths");
    }
    std::vector<std::filesystem::path> next_paths;
    for (const auto& file : root["files"]) {
        if (!file.is_string() || file.get<std::string>().empty()) {
            throw std::runtime_error("files must contain non-empty string paths");
        }
        auto path = std::filesystem::u8path(file.get<std::string>());
        next_paths.push_back(path.is_absolute() ? path : config_path.parent_path() / path);
    }
    engine_name = config["name"].get<std::string>();
    response_limit = next_limit;
    update_interval = next_interval;
    file_paths = std::move(next_paths);
    config_loaded = true;
}

std::vector<std::string> ConverterJSON::GetTextDocuments() {
    load_config();
    std::vector<std::string> documents;
    documents.reserve(file_paths.size());
    for (const auto& path : file_paths) {
        try {
            auto text = read_file(path, "document is missing or unreadable: " + path.u8string());
            text_processing::validate_text(text, 1000);
            documents.push_back(std::move(text));
        } catch (const std::runtime_error& error) {
            std::cerr << "Warning: " << path.u8string() << ": " << error.what() << '\n';
            documents.emplace_back();
        }
    }
    return documents;
}

int ConverterJSON::GetResponsesLimit() {
    load_config();
    return response_limit;
}

std::string ConverterJSON::GetEngineName() {
    load_config();
    return engine_name;
}

int ConverterJSON::GetUpdateInterval() {
    load_config();
    return update_interval;
}

std::vector<std::string> ConverterJSON::GetRequests() {
    const auto root = parse_json(read_file(requests_path, "requests file is missing"), "requests.json");
    if (!root.is_object() || !root.contains("requests") ||
        !root["requests"].is_array() || root["requests"].size() > 1000) {
        throw std::runtime_error("requests must be an array of at most 1000 queries");
    }
    std::vector<std::string> requests;
    for (const auto& request : root["requests"]) {
        if (!request.is_string()) {
            throw std::runtime_error("every request must be a string");
        }
        auto text = request.get<std::string>();
        text_processing::validate_text(text, 10, false);
        requests.push_back(std::move(text));
    }
    return requests;
}

void ConverterJSON::putAnswers(std::vector<std::vector<std::pair<int, float>>> answers) {
    if (same_path(answers_path, config_path) || same_path(answers_path, requests_path)) {
        throw std::runtime_error("answers path must differ from input paths");
    }
    for (const auto& path : file_paths) {
        if (same_path(answers_path, path)) {
            throw std::runtime_error("answers path must differ from document paths");
        }
    }
    nlohmann::ordered_json root;
    root["answers"] = nlohmann::ordered_json::object();
    for (std::size_t i = 0; i < answers.size(); ++i) {
        std::ostringstream request_id;
        request_id << "request" << std::setw(3) << std::setfill('0') << i + 1;
        auto& answer = root["answers"][request_id.str()];
        answer["result"] = answers[i].empty() ? "false" : "true";
        if (!answers[i].empty()) {
            answer["relevance"] = nlohmann::ordered_json::array();
            for (const auto& [doc_id, rank] : answers[i]) {
                if (doc_id < 0 || !std::isfinite(rank) || rank < 0 || rank > 1) {
                    throw std::runtime_error("invalid search result");
                }
                answer["relevance"].push_back({{"docid", doc_id}, {"rank", rank}});
            }
        }
    }
    std::ofstream output(answers_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("could not open answers file: " + answers_path.u8string());
    }
    output << root.dump(4) << '\n';
    output.close();
    if (!output) {
        throw std::runtime_error("could not write answers file: " + answers_path.u8string());
    }
}
