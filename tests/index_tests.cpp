#include "InvertedIndex.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

TEST(InvertedIndexTests, CountsWordsInEveryDocument) {
    InvertedIndex index;
    index.UpdateDocumentBase({"london is the capital of great britain",
                              "big ben is the nickname for the great bell of the striking clock"});

    EXPECT_EQ(index.GetWordCount("london"), (std::vector<Entry>{{0, 1}}));
    EXPECT_EQ(index.GetWordCount("the"), (std::vector<Entry>{{0, 1}, {1, 3}}));
}

TEST(InvertedIndexTests, CountsRepeatedWordsAndKeepsDocumentOrder) {
    InvertedIndex index;
    index.UpdateDocumentBase({"milk milk milk milk water water water",
                              "milk water water",
                              "milk milk milk milk milk water water water water water",
                              "americano cappuccino"});

    EXPECT_EQ(index.GetWordCount("milk"), (std::vector<Entry>{{0, 4}, {1, 1}, {2, 5}}));
    EXPECT_EQ(index.GetWordCount("water"), (std::vector<Entry>{{0, 3}, {1, 2}, {2, 5}}));
    EXPECT_EQ(index.GetWordCount("cappuccino"), (std::vector<Entry>{{3, 1}}));
    EXPECT_TRUE(index.GetWordCount("cappuchino").empty());
}

TEST(InvertedIndexTests, FindsWholeWordsOnly) {
    InvertedIndex index;
    index.UpdateDocumentBase({"abcdefghijkl", "statement"});

    EXPECT_TRUE(index.GetWordCount("m").empty());
    EXPECT_EQ(index.GetWordCount("statement"), (std::vector<Entry>{{1, 1}}));
    EXPECT_TRUE(index.GetWordCount("").empty());
}

TEST(InvertedIndexTests, HandlesWhitespaceAndEmptyDocumentSlots) {
    InvertedIndex index;
    index.UpdateDocumentBase({"", "  milk\twater\n milk\r\n", "", "water"});

    EXPECT_EQ(index.GetWordCount("milk"), (std::vector<Entry>{{1, 2}}));
    EXPECT_EQ(index.GetWordCount("water"), (std::vector<Entry>{{1, 1}, {3, 1}}));
}

TEST(InvertedIndexTests, ReplacesThePreviousBaseCompletely) {
    InvertedIndex index;
    index.UpdateDocumentBase({"milk milk", "water"});
    index.UpdateDocumentBase({"coffee", "milk"});

    EXPECT_TRUE(index.GetWordCount("water").empty());
    EXPECT_EQ(index.GetWordCount("milk"), (std::vector<Entry>{{1, 1}}));
    EXPECT_EQ(index.GetWordCount("coffee"), (std::vector<Entry>{{0, 1}}));

    index.UpdateDocumentBase({});
    EXPECT_TRUE(index.GetWordCount("coffee").empty());
    EXPECT_TRUE(index.GetWordCount("milk").empty());
}

TEST(InvertedIndexTests, RepeatedParallelIndexingIsDeterministic) {
    std::vector<std::string> documents(128);
    std::vector<Entry> expected;
    for (std::size_t document_id = 0; document_id < documents.size(); ++document_id) {
        const auto occurrences = document_id % 7 + 1;
        for (std::size_t count = 0; count < occurrences; ++count) {
            documents[document_id] += "shared ";
        }
        documents[document_id] += document_id % 2 == 0 ? "even" : "odd";
        expected.push_back({document_id, occurrences});
    }

    InvertedIndex index;
    for (int repetition = 0; repetition < 4; ++repetition) {
        index.UpdateDocumentBase(documents);
        EXPECT_EQ(index.GetWordCount("shared"), expected);
        EXPECT_EQ(index.GetWordCount("even").size(), 64U);
        EXPECT_EQ(index.GetWordCount("odd").size(), 64U);
    }
}
