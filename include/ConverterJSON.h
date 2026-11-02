#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

class ConverterJSON {
public:
    ConverterJSON() = default;
    explicit ConverterJSON(std::filesystem::path config_path,
                           std::filesystem::path requests_path = "requests.json",
                           std::filesystem::path answers_path = "answers.json");
    std::vector<std::string> GetTextDocuments();
    int GetResponsesLimit();
    std::vector<std::string> GetRequests();
    void putAnswers(std::vector<std::vector<std::pair<int, float>>> answers);
    std::string GetEngineName();
    int GetUpdateInterval();

private:
    void load_config();
    std::filesystem::path config_path = "config.json";
    std::filesystem::path requests_path = "requests.json";
    std::filesystem::path answers_path = "answers.json";
    bool config_loaded = false;
    std::string engine_name;
    int response_limit = 5;
    int update_interval = 60;
    std::vector<std::filesystem::path> file_paths;
};
