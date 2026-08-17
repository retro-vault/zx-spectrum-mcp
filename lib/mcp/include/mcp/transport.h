//
// how messages get in and out.
//
// MCP's stdio transport is newline delimited JSON: one complete message
// per line, no framing headers, no embedded newlines. the writer takes
// care of the last of those by only ever emitting compact JSON, in
// which every control character is escaped.
//
// stdout belongs to the protocol and nothing else. any diagnostic the
// server wants to emit goes to stderr, because a stray line on stdout
// would be read as a message and break the session.
//
// the interface is abstract so tests can drive a session from a vector
// of strings without touching the process's real streams.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef MCP_TRANSPORT_H
#define MCP_TRANSPORT_H

#include <string>
#include <string_view>
#include <vector>

namespace mcp {

//
// a bidirectional message channel.
//
class transport {
public:
    virtual ~transport() = default;

    //
    // Wait for the next message.
    //
    // Parameters:
    //      out         - receives the message text.
    //
    // Returns:
    //      false once the peer has closed the channel.
    //
    virtual bool read_message(std::string &out) = 0;

    //
    // Send a message.
    //
    // Parameters:
    //      text        - one complete message, containing no newline.
    //
    virtual void write_message(std::string_view text) = 0;
};

//
// the real transport: stdin and stdout.
//
class stdio_transport final : public transport {
public:
    stdio_transport();

    bool read_message(std::string &out) override;
    void write_message(std::string_view text) override;
};

//
// an in-memory transport for tests.
//
// reads from a scripted list of messages and collects everything
// written, so a whole session can be exercised without a subprocess.
//
class memory_transport final : public transport {
public:
    explicit memory_transport(std::vector<std::string> incoming);

    bool read_message(std::string &out) override;
    void write_message(std::string_view text) override;

    //
    // Returns: every message the server sent, in order.
    //
    const std::vector<std::string> &sent() const;

private:
    std::vector<std::string> incoming_;
    std::vector<std::string> outgoing_;
    std::size_t position_ = 0;
};

} // namespace mcp

#endif // MCP_TRANSPORT_H
