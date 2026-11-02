#include "SearchServer.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::vector<std::string> CapitalDocuments() {
    return {"london is the capital of great britain",
            "paris is the capital of france",
            "berlin is the capital of germany",
            "rome is the capital of italy",
            "madrid is the capital of spain",
            "lisboa is the capital of portugal",
            "bern is the capital of switzerland",
            "moscow is the capital of russia",
            "kiev is the capital of ukraine",
            "minsk is the capital of belarus",
            "astana is the capital of kazakhstan",
            "beijing is the capital of china",
            "tokyo is the capital of japan",
            "bangkok is the capital of thailand",
            "welcome to moscow the capital of russia the third rome",
            "amsterdam is the capital of netherlands",
            "helsinki is the capital of finland",
            "oslo is the capital of norway",
            "stockholm is the capital of sweden",
            "riga is the capital of latvia",
            "tallinn is the capital of estonia",
            "warsaw is the capital of poland"};
}

}  // namespace

TEST(SearchServerTests, CalculatesReferenceRelevanceAndMissingWord) {
    InvertedIndex index;
    index.UpdateDocumentBase({"milk milk milk milk water water water",
                              "milk water water",
                              "milk milk milk milk milk water water water water water",
                              "americano cappuccino"});
    SearchServer server(index);
    const auto results = server.search({"milk water", "sugar"});

    ASSERT_EQ(results.size(), 2U);
    ASSERT_EQ(results[0].size(), 3U);
    EXPECT_EQ(results[0][0].doc_id, 2U);
    EXPECT_FLOAT_EQ(results[0][0].rank, 1.0F);
    EXPECT_EQ(results[0][1].doc_id, 0U);
    EXPECT_FLOAT_EQ(results[0][1].rank, 0.7F);
    EXPECT_EQ(results[0][2].doc_id, 1U);
    EXPECT_FLOAT_EQ(results[0][2].rank, 0.3F);
    EXPECT_TRUE(results[1].empty());
    EXPECT_EQ(server.GetResultCounts(), (std::vector<std::size_t>{3, 0}));
}

TEST(SearchServerTests, RequiresEveryUniqueQueryWordByDefault) {
    InvertedIndex index;
    index.UpdateDocumentBase({"milk milk milk", "water water", "milk water", "milk water water"});
    SearchServer server(index);
    const auto results = server.search({"milk water", "milk sugar"});

    ASSERT_EQ(results.size(), 2U);
    ASSERT_EQ(results[0].size(), 2U);
    EXPECT_EQ(results[0][0].doc_id, 3U);
    EXPECT_FLOAT_EQ(results[0][0].rank, 1.0F);
    EXPECT_EQ(results[0][1].doc_id, 2U);
    EXPECT_FLOAT_EQ(results[0][1].rank, 2.0F / 3.0F);
    EXPECT_TRUE(results[1].empty());
}

TEST(SearchServerTests, RepeatedQueryWordsDoNotIncreaseTheirWeight) {
    InvertedIndex index;
    index.UpdateDocumentBase({"milk milk water", "milk water water water"});
    SearchServer server(index);
    const auto results = server.search({"milk water", "milk milk water milk"});

    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[0], results[1]);
}

TEST(SearchServerTests, QueryWordOrderDoesNotChangeTheResult) {
    InvertedIndex index;
    index.UpdateDocumentBase({"common common rare", "common", "rare common common common"});
    SearchServer server(index);
    const auto results = server.search({"common rare", "rare common"});

    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[0], results[1]);
}

TEST(SearchServerTests, TiesUseAscendingDocumentIdAndCountBeforeLimit) {
    InvertedIndex index;
    index.UpdateDocumentBase({"milk", "milk", "milk", "milk", "milk", "milk", "milk"});
    SearchServer server(index, 3);
    const auto results = server.search({"milk"});

    ASSERT_EQ(results.size(), 1U);
    ASSERT_EQ(results[0].size(), 3U);
    EXPECT_EQ(results[0][0].doc_id, 0U);
    EXPECT_EQ(results[0][1].doc_id, 1U);
    EXPECT_EQ(results[0][2].doc_id, 2U);
    EXPECT_EQ(server.GetResultCounts(), (std::vector<std::size_t>{7}));
}

TEST(SearchServerTests, LimitIsAppliedAfterRanking) {
    InvertedIndex index;
    index.UpdateDocumentBase({"milk", "milk milk", "milk milk milk"});
    SearchServer server(index, 1);
    const auto results = server.search({"milk"});

    ASSERT_EQ(results[0].size(), 1U);
    EXPECT_EQ(results[0][0].doc_id, 2U);
    EXPECT_FLOAT_EQ(results[0][0].rank, 1.0F);
    EXPECT_EQ(server.GetResultCounts(), (std::vector<std::size_t>{3}));
}

TEST(SearchServerTests, DefaultLimitIsFive) {
    InvertedIndex index;
    index.UpdateDocumentBase(std::vector<std::string>(8, "milk"));
    SearchServer server(index);

    EXPECT_EQ(server.search({"milk"})[0].size(), 5U);
    EXPECT_EQ(server.GetResultCounts(), (std::vector<std::size_t>{8}));
}

TEST(SearchServerTests, ExplicitAnyModeReproducesTheInconsistentTopFiveExample) {
    InvertedIndex index;
    index.UpdateDocumentBase(CapitalDocuments());
    SearchServer server(index, 5, MatchMode::Any);
    const auto results = server.search({"moscow is the capital of russia"});

    ASSERT_EQ(results[0].size(), 5U);
    EXPECT_EQ(results[0][0].doc_id, 7U);
    EXPECT_FLOAT_EQ(results[0][0].rank, 1.0F);
    EXPECT_EQ(results[0][1].doc_id, 14U);
    EXPECT_FLOAT_EQ(results[0][1].rank, 1.0F);
    for (std::size_t position = 2; position < 5; ++position) {
        EXPECT_EQ(results[0][position].doc_id, position - 2);
        EXPECT_FLOAT_EQ(results[0][position].rank, 4.0F / 6.0F);
    }
    EXPECT_EQ(server.GetResultCounts(), (std::vector<std::size_t>{22}));
}

TEST(SearchServerTests, AllModeOnTheTopFiveDatasetExcludesPartialMatches) {
    InvertedIndex index;
    index.UpdateDocumentBase(CapitalDocuments());
    SearchServer server(index);
    const auto results = server.search({"moscow is the capital of russia"});

    ASSERT_EQ(results[0].size(), 1U);
    EXPECT_EQ(results[0][0].doc_id, 7U);
    EXPECT_FLOAT_EQ(results[0][0].rank, 1.0F);
}

TEST(SearchServerTests, AnyModeCanIgnoreUnknownWordsWhileAllModeCannot) {
    InvertedIndex index;
    index.UpdateDocumentBase({"milk", "water"});
    SearchServer all_server(index);
    SearchServer any_server(index, 5, MatchMode::Any);

    EXPECT_TRUE(all_server.search({"milk unknown"})[0].empty());
    const auto results = any_server.search({"milk unknown"});
    ASSERT_EQ(results[0].size(), 1U);
    EXPECT_EQ(results[0][0].doc_id, 0U);
    EXPECT_FLOAT_EQ(results[0][0].rank, 1.0F);
}

TEST(SearchServerTests, HandlesEmptyBaseEmptyQueriesAndRefreshesCounts) {
    InvertedIndex index;
    SearchServer server(index);
    const auto results = server.search({"milk", "", "   "});

    ASSERT_EQ(results.size(), 3U);
    for (const auto& result : results) {
        EXPECT_TRUE(result.empty());
    }
    EXPECT_EQ(server.GetResultCounts(), (std::vector<std::size_t>{0, 0, 0}));
    EXPECT_TRUE(server.search({}).empty());
    EXPECT_TRUE(server.GetResultCounts().empty());
}

TEST(SearchServerTests, ObservesTheReferencedIndexAfterRebuilding) {
    InvertedIndex index;
    index.UpdateDocumentBase({"milk"});
    SearchServer server(index);
    ASSERT_FALSE(server.search({"milk"})[0].empty());

    index.UpdateDocumentBase({"water", "milk milk"});
    const auto results = server.search({"milk"});
    ASSERT_EQ(results[0].size(), 1U);
    EXPECT_EQ(results[0][0].doc_id, 1U);
    EXPECT_FLOAT_EQ(results[0][0].rank, 1.0F);
}

TEST(SearchServerTests, RejectsAZeroResponseLimit) {
    InvertedIndex index;
    EXPECT_THROW(SearchServer(index, 0), std::invalid_argument);
}
