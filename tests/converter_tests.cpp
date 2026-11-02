#include "ConverterJSON.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace filesystem = std::filesystem;
using Json = nlohmann::json;

class ConverterTests : public ::testing::Test {
protected:
    void SetUp() override {
        static std::atomic<unsigned long> sequence{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory = filesystem::temp_directory_path() /
                    ("search_engine_converter_" + std::to_string(stamp) + "_" +
                     std::to_string(sequence.fetch_add(1)));
        filesystem::create_directories(directory / "resources");
        config_path = directory / "config.json";
        requests_path = directory / "requests.json";
        answers_path = directory / "answers.json";
    }

    void TearDown() override {
        std::error_code error;
        filesystem::remove_all(directory, error);
    }

    static void WriteText(const filesystem::path& path, const std::string& text) {
        std::ofstream output(path, std::ios::binary);
        ASSERT_TRUE(output.is_open());
        output << text;
        ASSERT_TRUE(output.good());
    }

    static void WriteJson(const filesystem::path& path, const Json& value) {
        WriteText(path, value.dump(2));
    }

    Json Configuration() const {
        return {{"config", {{"name", "TestSearchEngine"}, {"version", "0.1"}}},
                {"files", Json::array()}};
    }

    ConverterJSON CreateConverter() const {
        return ConverterJSON(config_path, requests_path, answers_path);
    }

    Json ReadAnswers() const {
        std::ifstream input(answers_path);
        return Json::parse(input);
    }

    filesystem::path directory;
    filesystem::path config_path;
    filesystem::path requests_path;
    filesystem::path answers_path;
};

TEST_F(ConverterTests, ReportsTheRequiredMissingConfigMessage) {
    auto converter = CreateConverter();
    try {
        converter.GetResponsesLimit();
        FAIL() << "Missing config must throw";
    } catch (const std::runtime_error& error) {
        EXPECT_STREQ(error.what(), "config file is missing");
    }
}

TEST_F(ConverterTests, ReportsTheRequiredEmptyConfigMessageForMissingObject) {
    WriteJson(config_path, {{"files", Json::array()}});
    auto converter = CreateConverter();
    try {
        converter.GetTextDocuments();
        FAIL() << "Missing config object must throw";
    } catch (const std::runtime_error& error) {
        EXPECT_STREQ(error.what(), "config file is empty");
    }
}

TEST_F(ConverterTests, ReportsTheRequiredVersionMessage) {
    auto config = Configuration();
    config["config"]["version"] = "9.9";
    WriteJson(config_path, config);
    auto converter = CreateConverter();
    try {
        converter.GetResponsesLimit();
        FAIL() << "Unsupported version must throw";
    } catch (const std::runtime_error& error) {
        EXPECT_STREQ(error.what(), "config.json has incorrect file version");
    }
}

TEST_F(ConverterTests, SuppliesDefaultsAndEngineName) {
    WriteJson(config_path, Configuration());
    auto converter = CreateConverter();

    EXPECT_EQ(converter.GetResponsesLimit(), 5);
    EXPECT_EQ(converter.GetEngineName(), "TestSearchEngine");
    EXPECT_EQ(converter.GetUpdateInterval(), 60);
    EXPECT_TRUE(converter.GetTextDocuments().empty());
}

TEST_F(ConverterTests, ReadsConfiguredLimitsAndUpdateInterval) {
    auto config = Configuration();
    config["config"]["max_responses"] = 2;
    config["config"]["update_interval"] = 3;
    WriteJson(config_path, config);
    auto converter = CreateConverter();

    EXPECT_EQ(converter.GetResponsesLimit(), 2);
    EXPECT_EQ(converter.GetUpdateInterval(), 3);
}

TEST_F(ConverterTests, RejectsMalformedConfigJson) {
    WriteText(config_path, "{\"config\":");
    auto converter = CreateConverter();
    EXPECT_THROW(converter.GetTextDocuments(), std::runtime_error);
}

TEST_F(ConverterTests, RejectsInvalidResponseLimitTypesAndValues) {
    for (const Json& value : std::vector<Json>{0, -1, 1.5, "five", true, nullptr}) {
        auto config = Configuration();
        config["config"]["max_responses"] = value;
        WriteJson(config_path, config);
        auto converter = CreateConverter();
        EXPECT_THROW(converter.GetResponsesLimit(), std::runtime_error) << value;
    }
}

TEST_F(ConverterTests, RejectsInvalidConfigStructure) {
    auto config = Configuration();
    config["files"] = "file.txt";
    WriteJson(config_path, config);
    auto converter = CreateConverter();
    EXPECT_THROW(converter.GetTextDocuments(), std::runtime_error);

    config = Configuration();
    config["config"]["name"] = "";
    WriteJson(config_path, config);
    auto empty_name_converter = CreateConverter();
    EXPECT_THROW(empty_name_converter.GetEngineName(), std::runtime_error);
}

TEST_F(ConverterTests, ResolvesDocumentPathsFromTheConfigDirectoryAndPreservesIds) {
    WriteText(directory / "resources" / "first.txt", "milk sugar salt");
    WriteText(directory / "resources" / "third.txt", "milk milk");
    auto config = Configuration();
    config["files"] = {"resources/first.txt", "resources/missing.txt", "resources/third.txt"};
    WriteJson(config_path, config);
    auto converter = CreateConverter();

    testing::internal::CaptureStderr();
    const auto documents = converter.GetTextDocuments();
    const auto warning = testing::internal::GetCapturedStderr();
    EXPECT_EQ(documents, (std::vector<std::string>{"milk sugar salt", "", "milk milk"}));
    EXPECT_FALSE(warning.empty());
}

TEST_F(ConverterTests, SkipsInvalidDocumentsWithoutShiftingFollowingIds) {
    WriteText(directory / "invalid.txt", "Milk punctuation!");
    WriteText(directory / "valid.txt", "milk water");
    auto config = Configuration();
    config["files"] = {"invalid.txt", "valid.txt"};
    WriteJson(config_path, config);
    auto converter = CreateConverter();

    testing::internal::CaptureStderr();
    const auto documents = converter.GetTextDocuments();
    testing::internal::GetCapturedStderr();
    EXPECT_EQ(documents, (std::vector<std::string>{"", "milk water"}));
}

TEST_F(ConverterTests, AcceptsDocumentLimitsAndSkipsDocumentsOverTheLimits) {
    const std::string longest_word(100, 'a');
    std::string thousand_words;
    for (int word = 0; word < 1000; ++word) {
        thousand_words += "milk ";
    }
    WriteText(directory / "maxword.txt", longest_word);
    WriteText(directory / "maxwords.txt", thousand_words);
    WriteText(directory / "longword.txt", longest_word + "a");
    WriteText(directory / "manywords.txt", thousand_words + "milk");
    auto config = Configuration();
    config["files"] = {"maxword.txt", "maxwords.txt", "longword.txt", "manywords.txt"};
    WriteJson(config_path, config);
    auto converter = CreateConverter();

    testing::internal::CaptureStderr();
    const auto documents = converter.GetTextDocuments();
    testing::internal::GetCapturedStderr();
    ASSERT_EQ(documents.size(), 4U);
    EXPECT_EQ(documents[0], longest_word);
    EXPECT_EQ(documents[1], thousand_words);
    EXPECT_TRUE(documents[2].empty());
    EXPECT_TRUE(documents[3].empty());
}

TEST_F(ConverterTests, ReadsRequestsAgainOnEachCall) {
    WriteJson(requests_path, {{"requests", {"milk water", "salt"}}});
    auto converter = CreateConverter();
    EXPECT_EQ(converter.GetRequests(), (std::vector<std::string>{"milk water", "salt"}));

    WriteJson(requests_path, {{"requests", {"coffee"}}});
    EXPECT_EQ(converter.GetRequests(), (std::vector<std::string>{"coffee"}));
}

TEST_F(ConverterTests, AcceptsAnEmptyRequestBatchAndMaximumValidQuery) {
    WriteJson(requests_path, {{"requests", Json::array()}});
    auto converter = CreateConverter();
    EXPECT_TRUE(converter.GetRequests().empty());

    const std::string query = "a b c d e f g h i j";
    WriteJson(requests_path, {{"requests", {query}}});
    EXPECT_EQ(converter.GetRequests(), (std::vector<std::string>{query}));
}

TEST_F(ConverterTests, RejectsMissingMalformedAndIncorrectRequestStructure) {
    auto converter = CreateConverter();
    EXPECT_THROW(converter.GetRequests(), std::runtime_error);
    WriteText(requests_path, "not json");
    EXPECT_THROW(converter.GetRequests(), std::runtime_error);
    WriteJson(requests_path, {{"requests", "milk"}});
    EXPECT_THROW(converter.GetRequests(), std::runtime_error);
    WriteJson(requests_path, {{"requests", {42}}});
    EXPECT_THROW(converter.GetRequests(), std::runtime_error);
}

TEST_F(ConverterTests, RejectsInvalidQueryContentAndTooManyWords) {
    auto converter = CreateConverter();
    for (const auto& query : std::vector<std::string>{"", "   ", "Milk", "milk!",
                                                      "a b c d e f g h i j k",
                                                      std::string(101, 'a')}) {
        WriteJson(requests_path, {{"requests", {query}}});
        EXPECT_THROW(converter.GetRequests(), std::runtime_error) << query;
    }
}

TEST_F(ConverterTests, EnforcesTheMaximumNumberOfRequests) {
    auto converter = CreateConverter();
    std::vector<std::string> queries(1000, "milk");
    WriteJson(requests_path, {{"requests", queries}});
    EXPECT_EQ(converter.GetRequests().size(), 1000U);
    queries.push_back("water");
    WriteJson(requests_path, {{"requests", queries}});
    EXPECT_THROW(converter.GetRequests(), std::runtime_error);
}

TEST_F(ConverterTests, WritesTheSpecifiedAnswerSchemaIncludingSingleAndEmptyResults) {
    auto converter = CreateConverter();
    converter.putAnswers({{{2, 1.0F}, {0, 0.5F}}, {{7, 1.0F}}, {}});
    const auto result = ReadAnswers();

    ASSERT_EQ(result.size(), 1U);
    ASSERT_TRUE(result.contains("answers"));
    const auto& answers = result["answers"];
    EXPECT_EQ(answers.size(), 3U);
    EXPECT_EQ(answers["request001"]["result"], "true");
    EXPECT_EQ(answers["request001"]["relevance"][0]["docid"], 2);
    EXPECT_DOUBLE_EQ(answers["request001"]["relevance"][1]["rank"].get<double>(), 0.5);
    EXPECT_EQ(answers["request002"]["relevance"].size(), 1U);
    EXPECT_EQ(answers["request003"], Json({{"result", "false"}}));
}

TEST_F(ConverterTests, ReplacesExistingAnswersInsteadOfAppending) {
    WriteText(answers_path, "old contents that are not valid json");
    auto converter = CreateConverter();
    converter.putAnswers({{{0, 1.0F}}});
    ASSERT_EQ(ReadAnswers()["answers"].size(), 1U);

    converter.putAnswers({});
    EXPECT_EQ(ReadAnswers(), Json({{"answers", Json::object()}}));
}

TEST_F(ConverterTests, FormatsRequestIdentifiersBeyondThreeDigits) {
    auto converter = CreateConverter();
    converter.putAnswers(std::vector<std::vector<std::pair<int, float>>>(1000));
    const auto answers = ReadAnswers()["answers"];

    EXPECT_EQ(answers.size(), 1000U);
    EXPECT_TRUE(answers.contains("request001"));
    EXPECT_TRUE(answers.contains("request999"));
    EXPECT_TRUE(answers.contains("request1000"));
}

TEST_F(ConverterTests, ReportsFailureWhenTheOutputCannotBeOpened) {
    ConverterJSON converter(config_path, requests_path, directory / "missing" / "answers.json");
    EXPECT_THROW(converter.putAnswers({}), std::runtime_error);
}

}  // namespace
