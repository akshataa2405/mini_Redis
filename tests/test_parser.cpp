#include <gtest/gtest.h>
#include "../src/protocol/resp_parser.h"

using namespace miniredis;

TEST(RespParserTest, CompleteCommand) {
    RespParser parser;
    std::string buf = "*3\r\n$3\r\nSET\r\n$5\r\nmykey\r\n$7\r\nmyvalue\r\n";
    std::vector<Command> cmds;

    ParseStatus status = parser.parse(buf, cmds);
    EXPECT_EQ(status, ParseStatus::OK);
    ASSERT_EQ(cmds.size(), 1);
    EXPECT_EQ(cmds[0].size(), 3);
    EXPECT_EQ(cmds[0][0], "SET");
    EXPECT_EQ(cmds[0][1], "mykey");
    EXPECT_EQ(cmds[0][2], "myvalue");
    EXPECT_TRUE(buf.empty());
}

TEST(RespParserTest, SplitReadsByteByByte) {
    RespParser parser;
    std::string full_packet = "*2\r\n$4\r\nECHO\r\n$5\r\nhello\r\n";
    std::string stream_buf;
    std::vector<Command> cmds;

    for (size_t i = 0; i < full_packet.size() - 1; ++i) {
        stream_buf.push_back(full_packet[i]);
        ParseStatus st = parser.parse(stream_buf, cmds);
        EXPECT_EQ(st, ParseStatus::INCOMPLETE);
        EXPECT_EQ(cmds.size(), 0);
    }

    // Push the final byte
    stream_buf.push_back(full_packet.back());
    ParseStatus st = parser.parse(stream_buf, cmds);
    EXPECT_EQ(st, ParseStatus::OK);
    ASSERT_EQ(cmds.size(), 1);
    EXPECT_EQ(cmds[0][0], "ECHO");
    EXPECT_EQ(cmds[0][1], "hello");
    EXPECT_TRUE(stream_buf.empty());
}

TEST(RespParserTest, PipelinedCommands) {
    RespParser parser;
    std::string buf = "*1\r\n$4\r\nPING\r\n*2\r\n$3\r\nGET\r\n$1\r\na\r\n*1\r\n$4\r\nPING\r\n";
    std::vector<Command> cmds;

    ParseStatus status = parser.parse(buf, cmds);
    EXPECT_EQ(status, ParseStatus::OK);
    ASSERT_EQ(cmds.size(), 3);
    EXPECT_EQ(cmds[0][0], "PING");
    EXPECT_EQ(cmds[1][0], "GET");
    EXPECT_EQ(cmds[1][1], "a");
    EXPECT_EQ(cmds[2][0], "PING");
    EXPECT_TRUE(buf.empty());
}

TEST(RespParserTest, InlineCommand) {
    RespParser parser;
    std::string buf = "PING\r\n";
    std::vector<Command> cmds;

    ParseStatus status = parser.parse(buf, cmds);
    EXPECT_EQ(status, ParseStatus::OK);
    ASSERT_EQ(cmds.size(), 1);
    EXPECT_EQ(cmds[0][0], "PING");
}
