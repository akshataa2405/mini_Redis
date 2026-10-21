#include <gtest/gtest.h>
#include "../src/store/store.h"
#include <thread>
#include <chrono>

using namespace miniredis;

TEST(StoreTest, BasicSetGetDel) {
    Store store;
    EXPECT_EQ(store.set("foo", Value("bar")), SetResult::OK);

    Value* val = store.get("foo");
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(val->str_val, "bar");

    EXPECT_TRUE(store.exists("foo"));
    EXPECT_EQ(store.del({"foo"}), 1);
    EXPECT_FALSE(store.exists("foo"));
    EXPECT_EQ(store.get("foo"), nullptr);
}

TEST(StoreTest, LazyExpiry) {
    Store store;
    // Set with 50 ms TTL
    int64_t expire_at = now_ms() + 50;
    store.set("temp_key", Value("val", expire_at));

    // Immediately available
    EXPECT_NE(store.get("temp_key"), nullptr);

    // Sleep for 70 ms
    std::this_thread::sleep_for(std::chrono::milliseconds(70));

    // Lazy expiry should purge the key
    EXPECT_EQ(store.get("temp_key"), nullptr);
    EXPECT_FALSE(store.exists("temp_key"));
}

TEST(StoreTest, ListOperations) {
    Store store;
    EXPECT_EQ(store.rpush("mylist", "a"), 1);
    EXPECT_EQ(store.rpush("mylist", "b"), 2);
    EXPECT_EQ(store.lpush("mylist", "z"), 3);

    // List is now: z, a, b
    auto range = store.lrange("mylist", 0, -1);
    ASSERT_EQ(range.size(), 3);
    EXPECT_EQ(range[0], "z");
    EXPECT_EQ(range[1], "a");
    EXPECT_EQ(range[2], "b");

    auto popped = store.lpop("mylist");
    ASSERT_TRUE(popped.has_value());
    EXPECT_EQ(*popped, "z");

    EXPECT_EQ(store.llen("mylist"), 2);
}
