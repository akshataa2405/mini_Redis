#include <gtest/gtest.h>
#include "../src/store/hash_table.h"
#include <string>

using namespace miniredis;

TEST(HashTableTest, BasicInsertAndFind) {
    HashTable<std::string> ht(4);
    EXPECT_TRUE(ht.empty());
    EXPECT_EQ(ht.size(), 0);

    EXPECT_TRUE(ht.insert("hello", "world"));
    EXPECT_EQ(ht.size(), 1);

    std::string* val = ht.find("hello");
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, "world");

    // Update existing key
    EXPECT_FALSE(ht.insert("hello", "redis"));
    EXPECT_EQ(ht.size(), 1);
    val = ht.find("hello");
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, "redis");
}

TEST(HashTableTest, EraseKey) {
    HashTable<int> ht(4);
    ht.insert("k1", 100);
    ht.insert("k2", 200);
    EXPECT_EQ(ht.size(), 2);

    EXPECT_TRUE(ht.erase("k1"));
    EXPECT_EQ(ht.size(), 1);
    EXPECT_EQ(ht.find("k1"), nullptr);
    EXPECT_NE(ht.find("k2"), nullptr);

    // Erasing non-existent key returns false
    EXPECT_FALSE(ht.erase("non_existent"));
}

TEST(HashTableTest, IncrementalRehashingLargeVolume) {
    HashTable<int> ht(16);
    const int N = 5000;

    for (int i = 0; i < N; ++i) {
        ht.insert("key_" + std::to_string(i), i * 10);
    }
    EXPECT_EQ(ht.size(), N);

    // Verify all keys are present
    for (int i = 0; i < N; ++i) {
        int* v = ht.find("key_" + std::to_string(i));
        ASSERT_NE(v, nullptr) << "Failed to find key_" << i;
        EXPECT_EQ(*v, i * 10);
    }

    // Erase half
    for (int i = 0; i < N; i += 2) {
        EXPECT_TRUE(ht.erase("key_" + std::to_string(i)));
    }
    EXPECT_EQ(ht.size(), N / 2);

    for (int i = 0; i < N; ++i) {
        int* v = ht.find("key_" + std::to_string(i));
        if (i % 2 == 0) {
            EXPECT_EQ(v, nullptr);
        } else {
            ASSERT_NE(v, nullptr);
            EXPECT_EQ(*v, i * 10);
        }
    }
}
