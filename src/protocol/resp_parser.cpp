#include "resp_parser.h"
#include <sstream>
#include <cstdlib>

namespace miniredis {

static bool find_crlf(const std::string& buf, size_t start, size_t& crlf_pos) {
    size_t pos = buf.find("\r\n", start);
    if (pos == std::string::npos) {
        return false;
    }
    crlf_pos = pos;
    return true;
}

bool RespParser::parse_inline_command(std::string_view line, Command& cmd) {
    cmd.clear();
    std::string current;
    bool in_quote = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') {
            in_quote = !in_quote;
        } else if (c == ' ' && !in_quote) {
            if (!current.empty()) {
                cmd.push_back(std::move(current));
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        cmd.push_back(std::move(current));
    }
    return !cmd.empty();
}

bool RespParser::parse_resp_array(const std::string& buf, size_t& offset, Command& cmd) {
    size_t curr = offset;
    if (curr >= buf.size() || buf[curr] != '*') {
        return false;
    }
    curr++; // skip '*'

    size_t crlf_pos = 0;
    if (!find_crlf(buf, curr, crlf_pos)) {
        return false; // incomplete
    }

    std::string count_str = buf.substr(curr, crlf_pos - curr);
    char* endptr = nullptr;
    long num_args = std::strtol(count_str.c_str(), &endptr, 10);
    if (*endptr != '\0' || num_args < 0 || static_cast<size_t>(num_args) > MAX_ARGS) {
        return false;
    }
    curr = crlf_pos + 2;

    cmd.clear();
    cmd.reserve(num_args);

    for (long i = 0; i < num_args; ++i) {
        if (curr >= buf.size() || buf[curr] != '$') {
            return false;
        }
        curr++; // skip '$'

        if (!find_crlf(buf, curr, crlf_pos)) {
            return false; // incomplete
        }

        std::string len_str = buf.substr(curr, crlf_pos - curr);
        long arg_len = std::strtol(len_str.c_str(), &endptr, 10);
        if (*endptr != '\0' || arg_len < 0 || static_cast<size_t>(arg_len) > MAX_ARG_LEN) {
            return false;
        }
        curr = crlf_pos + 2;

        // Check if full argument + CRLF is present
        if (curr + arg_len + 2 > buf.size()) {
            return false; // incomplete data
        }

        if (buf[curr + arg_len] != '\r' || buf[curr + arg_len + 1] != '\n') {
            return false; // malformed trailing CRLF
        }

        cmd.push_back(buf.substr(curr, arg_len));
        curr += arg_len + 2;
    }

    offset = curr;
    return true;
}

ParseStatus RespParser::parse(std::string& in_buf, std::vector<Command>& out_cmds) {
    size_t offset = 0;
    bool parsed_any = false;

    while (offset < in_buf.size()) {
        char first = in_buf[offset];

        if (first == '*') {
            // Standard RESP array
            size_t next_offset = offset;
            Command cmd;
            if (parse_resp_array(in_buf, next_offset, cmd)) {
                out_cmds.push_back(std::move(cmd));
                offset = next_offset;
                parsed_any = true;
            } else {
                // Incomplete or malformed.
                // If the array header had an invalid count or exceeded limit, check
                size_t crlf = 0;
                if (find_crlf(in_buf, offset, crlf)) {
                    std::string count_str = in_buf.substr(offset + 1, crlf - offset - 1);
                    char* endptr = nullptr;
                    long num = std::strtol(count_str.c_str(), &endptr, 10);
                    if (*endptr != '\0' || num > static_cast<long>(MAX_ARGS)) {
                        return ParseStatus::ERROR;
                    }
                }
                break;
            }
        } else if (first == '\r' || first == '\n') {
            offset++;
        } else {
            // Inline command (e.g. "PING\r\n")
            size_t crlf_pos = 0;
            if (!find_crlf(in_buf, offset, crlf_pos)) {
                break; // Wait for full line
            }
            std::string_view line(&in_buf[offset], crlf_pos - offset);
            Command cmd;
            if (parse_inline_command(line, cmd)) {
                out_cmds.push_back(std::move(cmd));
                parsed_any = true;
            }
            offset = crlf_pos + 2;
        }
    }

    if (offset > 0) {
        in_buf.erase(0, offset);
    }

    if (parsed_any) {
        return ParseStatus::OK;
    }
    return ParseStatus::INCOMPLETE;
}

} // namespace miniredis
