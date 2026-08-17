//
// implementation of the stdio and in-memory transports.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "mcp/transport.h"

#include <cstdio>
#include <iostream>

namespace mcp {

stdio_transport::stdio_transport()
{
    // the streams are used as a message channel, not as a console, so
    // untie them from each other and from the C streams. without this,
    // every read would flush stdout and the process would spend its
    // time in the standard library rather than in the emulator.
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
}

bool stdio_transport::read_message(std::string &out)
{
    while (std::getline(std::cin, out)) {
        // tolerate a CRLF peer, and skip the blank lines that appear
        // when a client pads its output.
        if (!out.empty() && out.back() == '\r')
            out.pop_back();

        if (!out.empty())
            return true;
    }

    return false;
}

void stdio_transport::write_message(std::string_view text)
{
    std::cout << text << '\n';

    // a client is usually blocked waiting for this, so it has to leave
    // our buffers now rather than when they happen to fill.
    std::cout.flush();
}

memory_transport::memory_transport(std::vector<std::string> incoming)
    : incoming_(std::move(incoming))
{
}

bool memory_transport::read_message(std::string &out)
{
    if (position_ >= incoming_.size())
        return false;

    out = incoming_[position_++];
    return true;
}

void memory_transport::write_message(std::string_view text)
{
    outgoing_.emplace_back(text);
}

const std::vector<std::string> &memory_transport::sent() const
{
    return outgoing_;
}

} // namespace mcp
