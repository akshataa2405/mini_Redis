#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <string_view>

namespace miniredis {

using Command = std::vector<std::string>;

enum class ParseStatus {
    OK,         // Parsed one or more complete commands
    INCOMPLETE, // Need more data from socket
    ERROR       // Malformed protocol or exceeds size limits
};

class RespParser {
public:
    static constexpr size_t MAX_ARGS = 1024;
    static constexpr size_t MAX_ARG_LEN = 1024 * 1024; // 1 MB limit

    // Parse commands from in_buf. Consumes bytes processed from in_buf.
    ParseStatus parse(std::string& in_buf, std::vector<Command>& out_cmds);

private:
    bool parse_inline_command(std::string_view line, Command& cmd);
    bool parse_resp_array(const std::string& buf, size_t& offset, Command& cmd);
};

} // namespace miniredis
