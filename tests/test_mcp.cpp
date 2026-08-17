//
// the MCP protocol layer, driven end to end through the server.
//
// the checks that matter most are the ones about what must *not* be
// sent: a notification must never be answered, and a malformed message
// must never end the session.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <memory>
#include <string>
#include <vector>

#include "json/parser.h"
#include "json/writer.h"
#include "mcp/server.h"
#include "mcp/tool_registry.h"
#include "mcp/transport.h"
#include "spectrum/machine.h"
#include "test_support.h"
#include "tools/registration.h"

namespace {

//
// a server wired to a real machine, as main() builds it.
//
struct harness {
    spectrum::machine emulated;
    mcp::tool_registry registry;
    std::unique_ptr<mcp::server> server;

    harness()
    {
        tools::register_all_tools(registry, emulated);
        server = std::make_unique<mcp::server>(
            mcp::server_info{"test-server", "0.0.1"}, registry);
    }

    //
    // send one message and parse whatever comes back.
    //
    json::value send(const json::value &message)
    {
        const auto reply = server->handle_message(json::write(message));
        if (!reply)
            return json::value();
        const auto parsed = json::parse(*reply);
        return parsed.ok ? parsed.document : json::value();
    }

    bool answered(const json::value &message)
    {
        return server->handle_message(json::write(message)).has_value();
    }

    json::value request(int id, const std::string &method,
                        json::value params = json::value())
    {
        json::value message = json::value::make_object();
        message.set("jsonrpc", json::value("2.0"));
        message.set("id", json::value(id));
        message.set("method", json::value(method));
        if (!params.is_null())
            message.set("params", std::move(params));
        return message;
    }

    json::value call(int id, const std::string &tool,
                     json::value arguments)
    {
        json::value params = json::value::make_object();
        params.set("name", json::value(tool));
        params.set("arguments", std::move(arguments));
        return send(request(id, "tools/call", std::move(params)));
    }
};

void test_handshake()
{
    test::section("initialize handshake");

    harness h;

    json::value params = json::value::make_object();
    params.set("protocolVersion", json::value("2025-06-18"));
    params.set("capabilities", json::value::make_object());

    json::value client = json::value::make_object();
    client.set("name", json::value("probe"));
    client.set("version", json::value("1.0"));
    params.set("clientInfo", std::move(client));

    const json::value reply =
        h.send(h.request(1, "initialize", std::move(params)));

    test::check_eq_str(reply["jsonrpc"].as_string(), "2.0",
                       "the reply names the JSON-RPC version");
    test::check_eq(reply["id"].as_int(), 1, "and echoes the id");
    test::check_eq_str(reply["result"]["protocolVersion"].as_string(),
                       "2025-06-18",
                       "the client's protocol revision is honoured");
    test::check(reply["result"]["capabilities"]["tools"].is_object(),
                "the tools capability is advertised");
    test::check_eq_str(reply["result"]["serverInfo"]["name"].as_string(),
                       "test-server", "the server names itself");
    test::check_eq_str(h.server->session().client_name, "probe",
                       "and records who the client is");

    // an older revision the server also speaks is echoed back.
    json::value older = json::value::make_object();
    older.set("protocolVersion", json::value("2024-11-05"));
    const json::value negotiated =
        h.send(h.request(2, "initialize", std::move(older)));
    test::check_eq_str(
        negotiated["result"]["protocolVersion"].as_string(),
        "2024-11-05", "a supported older revision is accepted");

    // one we do not speak falls back to ours.
    json::value unknown = json::value::make_object();
    unknown.set("protocolVersion", json::value("1999-01-01"));
    const json::value fallback =
        h.send(h.request(3, "initialize", std::move(unknown)));
    test::check_eq_str(fallback["result"]["protocolVersion"].as_string(),
                       "2025-06-18",
                       "an unknown revision falls back to ours");
}

void test_notifications_are_never_answered()
{
    test::section("notifications");

    harness h;

    json::value note = json::value::make_object();
    note.set("jsonrpc", json::value("2.0"));
    note.set("method", json::value("notifications/initialized"));

    test::check(!h.answered(note),
                "the initialized notification gets no reply");
    test::check(h.server->session().ready,
                "but it is recorded as completing the handshake");

    json::value unknown = json::value::make_object();
    unknown.set("jsonrpc", json::value("2.0"));
    unknown.set("method", json::value("notifications/nonsense"));

    test::check(!h.answered(unknown),
                "an unknown notification is dropped in silence");
}

void test_tools_list()
{
    test::section("tools/list");

    harness h;
    const json::value reply = h.send(h.request(1, "tools/list"));
    const json::value &tools = reply["result"]["tools"];

    test::check(tools.is_array(), "an array of tools comes back");
    test::check(tools.size() >= 18,
                "every tool is listed, found " +
                    std::to_string(tools.size()));

    bool all_described = true;
    bool found_screen = false;
    bool found_screenshot = false;
    bool found_video_start = false;
    bool found_video_stop = false;
    bool found_tape = false;

    for (const json::value &entry : tools.elements()) {
        if (!entry["name"].is_string() ||
            !entry["description"].is_string() ||
            !entry["inputSchema"].is_object()) {
            all_described = false;
            break;
        }
        // MCP requires the schema itself to be an object schema.
        if (entry["inputSchema"]["type"].as_string() != "object") {
            all_described = false;
            break;
        }
        if (entry["name"].as_string() == "screen")
            found_screen = true;
        if (entry["name"].as_string() == "screenshot")
            found_screenshot = true;
        if (entry["name"].as_string() == "video_start")
            found_video_start = true;
        if (entry["name"].as_string() == "video_stop")
            found_video_stop = true;
        if (entry["name"].as_string() == "tape")
            found_tape = true;
    }

    test::check(all_described,
                "every tool has a name, a description and an object "
                "schema");
    test::check(found_screen, "the screen tool is among them");
    test::check(found_screenshot && found_video_start && found_video_stop &&
                    found_tape,
                "the capture and cassette tools are among them");
}

void test_tool_invocation()
{
    test::section("tools/call");

    harness h;

    // a tool that reports machine state.
    const json::value status =
        h.call(1, "status", json::value::make_object());
    test::check(status["result"]["content"].at(0)["type"].as_string() ==
                    "text",
                "status returns a text block");
    test::check(status["result"]["structuredContent"].is_object(),
                "and structured content alongside it");
    test::check_eq(
        status["result"]["structuredContent"]["tstates_per_frame"]
            .as_int(),
        69888, "reporting the 48K frame length");

    // write then read memory back.
    json::value write_args = json::value::make_object();
    write_args.set("address", json::value(0x8000));
    write_args.set("data", json::value("deadbeef"));
    const json::value written = h.call(2, "write_memory",
                                       std::move(write_args));
    test::check(!written["result"]["isError"].as_bool(false),
                "write_memory succeeds");
    test::check_eq(written["result"]["structuredContent"]["written"]
                       .as_int(),
                   4, "four bytes were written");

    json::value read_args = json::value::make_object();
    read_args.set("address", json::value("0x8000"));
    read_args.set("length", json::value(4));
    const json::value read_back = h.call(3, "read_memory",
                                         std::move(read_args));
    const json::value &bytes =
        read_back["result"]["structuredContent"]["bytes"];
    test::check_eq(bytes.at(0).as_int(), 0xde, "the first byte");
    test::check_eq(bytes.at(3).as_int(), 0xef, "the last byte");
    test::check_contains(
        read_back["result"]["content"].at(0)["text"].as_string(),
        "DE AD BE EF", "and the hex dump shows them");

    // the screen tool returns an image block.
    const json::value screen =
        h.call(4, "screen", json::value::make_object());
    const json::value &blocks = screen["result"]["content"];
    bool has_image = false;
    for (const json::value &block : blocks.elements()) {
        if (block["type"].as_string() == "image") {
            has_image = true;
            test::check_eq_str(block["mimeType"].as_string(), "image/png",
                               "the image is a PNG");
            test::check(block["data"].as_string().size() > 100,
                        "and carries base64 data");
        }
    }
    test::check(has_image, "the screen tool returns an image block");
}

void test_tool_errors()
{
    test::section("tool failures");

    harness h;

    // a tool that cannot do what was asked reports isError, not a
    // JSON-RPC error: the model is meant to read it and try again.
    json::value bad_args = json::value::make_object();
    bad_args.set("address", json::value(999999));
    const json::value out_of_range =
        h.call(1, "read_memory", std::move(bad_args));

    test::check(!out_of_range.contains("error"),
                "an out of range argument is not a protocol error");
    test::check(out_of_range["result"]["isError"].as_bool(false),
                "it is reported as a tool error instead");

    // an unknown tool is a protocol error, because the caller used the
    // protocol wrongly.
    json::value params = json::value::make_object();
    params.set("name", json::value("no_such_tool"));
    params.set("arguments", json::value::make_object());
    const json::value unknown =
        h.send(h.request(2, "tools/call", std::move(params)));

    test::check(unknown.contains("error"), "an unknown tool is an error");
    test::check_eq(unknown["error"]["code"].as_int(), -32602,
                   "reported as invalid params");
    test::check_contains(unknown["error"]["message"].as_string(),
                         "no_such_tool", "naming the tool");

    // a missing name field likewise.
    const json::value nameless = h.send(
        h.request(3, "tools/call", json::value::make_object()));
    test::check_eq(nameless["error"]["code"].as_int(), -32602,
                   "a call with no tool name is invalid params");
}

void test_malformed_input()
{
    test::section("malformed input");

    harness h;

    const auto broken = h.server->handle_message("{not json");
    test::check(broken.has_value(), "bad JSON still gets an answer");

    const auto parsed = json::parse(*broken);
    test::check_eq(parsed.document["error"]["code"].as_int(), -32700,
                   "reported as a parse error");
    test::check(parsed.document["id"].is_null(),
                "with a null id, since none could be read");

    const auto not_an_object = h.server->handle_message("42");
    test::check(not_an_object.has_value(), "a bare number gets an answer");

    const auto no_version = h.server->handle_message(
        R"({"id":1,"method":"ping"})");
    test::check(json::parse(*no_version)
                    .document["error"]["code"]
                    .as_int() == -32600,
                "a message without a jsonrpc version is an invalid "
                "request");

    const auto batch = h.server->handle_message(R"([{"id":1}])");
    test::check(batch.has_value(), "a batch is answered rather than "
                                   "ignored");
    test::check_contains(*batch, "batched", "and says batches are not "
                                            "supported");

    // the session survives all of it.
    const json::value ping = h.send(h.request(9, "ping"));
    test::check(ping["result"].is_object(),
                "the server still answers after every kind of bad "
                "input");
}

void test_unknown_method()
{
    test::section("unknown methods");

    harness h;
    const json::value reply = h.send(h.request(1, "resources/list"));

    test::check_eq(reply["error"]["code"].as_int(), -32601,
                   "method not found");
    test::check_contains(reply["error"]["message"].as_string(),
                         "tools/list",
                         "and the message says what is supported");
}

void test_full_session_over_a_transport()
{
    test::section("a whole session over a transport");

    spectrum::machine emulated;
    mcp::tool_registry registry;
    tools::register_all_tools(registry, emulated);
    mcp::server server({"test-server", "0.0.1"}, registry);

    mcp::memory_transport io({
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{)"
        R"("protocolVersion":"2025-06-18","clientInfo":{"name":"t",)"
        R"("version":"1"}}})",
        R"({"jsonrpc":"2.0","method":"notifications/initialized"})",
        R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})",
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{)"
        R"("name":"run","arguments":{"frames":1}}})",
        R"({"jsonrpc":"2.0","id":4,"method":"ping"})",
    });

    server.run(io);

    // five messages in, but one was a notification, so four replies.
    test::check_eq(static_cast<long long>(io.sent().size()), 4,
                   "one reply per request and none for the notification");

    for (const std::string &line : io.sent()) {
        test::check(line.find('\n') == std::string::npos,
                    "no reply contains a newline");
    }

    const auto last = json::parse(io.sent().back());
    test::check_eq(last.document["id"].as_int(), 4,
                   "replies come back in order");

    test::check(emulated.frame_number() >= 1,
                "the run tool really advanced the machine");
}

} // namespace

int main()
{
    test_handshake();
    test_notifications_are_never_answered();
    test_tools_list();
    test_tool_invocation();
    test_tool_errors();
    test_malformed_input();
    test_unknown_method();
    test_full_session_over_a_transport();
    return test::summary("mcp");
}
