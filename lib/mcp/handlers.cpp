//
// implementation of the MCP method handlers.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "mcp/handlers.h"

#include <array>

namespace mcp {

namespace {

//
// protocol revisions this server can speak.
//
// the differences between them do not touch anything a tools-only
// server does, so the client's choice is honoured when it names one of
// these and ours is offered otherwise.
//
constexpr std::array<const char *, 3> supported_versions = {
    "2025-06-18",
    "2025-03-26",
    "2024-11-05",
};

bool is_supported_version(const std::string &version)
{
    for (const char *candidate : supported_versions) {
        if (version == candidate)
            return true;
    }
    return false;
}

} // namespace

initialize_handler::initialize_handler(server_info info,
                                       session_state &session)
    : info_(std::move(info)), session_(session)
{
}

const char *initialize_handler::name() const { return "initialize"; }

bool initialize_handler::can_handle(const request &message) const
{
    return message.method == "initialize";
}

std::optional<json::value> initialize_handler::process(
    const request &message)
{
    const json::value &params = message.params;

    // honour the client's revision when we know it, otherwise answer
    // with ours and let the client decide whether to continue.
    const std::string requested =
        params["protocolVersion"].as_string(protocol_version);
    session_.protocol_version =
        is_supported_version(requested) ? requested : protocol_version;

    const json::value &client = params["clientInfo"];
    session_.client_name = client["name"].as_string("unknown");
    session_.client_version = client["version"].as_string("unknown");
    session_.initialized = true;

    // only the tools capability is advertised. the tool list is fixed
    // for the life of the process, so listChanged is false and no
    // notification will ever be sent.
    json::value tools = json::value::make_object();
    tools.set("listChanged", json::value(false));

    json::value capabilities = json::value::make_object();
    capabilities.set("tools", std::move(tools));

    json::value server = json::value::make_object();
    server.set("name", json::value(info_.name));
    server.set("version", json::value(info_.version));

    json::value result = json::value::make_object();
    result.set("protocolVersion", json::value(session_.protocol_version));
    result.set("capabilities", std::move(capabilities));
    result.set("serverInfo", std::move(server));

    return make_response(message.id, std::move(result));
}

initialized_handler::initialized_handler(session_state &session)
    : session_(session)
{
}

const char *initialized_handler::name() const
{
    return "notifications/initialized";
}

bool initialized_handler::can_handle(const request &message) const
{
    return message.method == "notifications/initialized";
}

std::optional<json::value> initialized_handler::process(const request &)
{
    session_.ready = true;

    // a notification is never answered.
    return std::nullopt;
}

const char *ping_handler::name() const { return "ping"; }

bool ping_handler::can_handle(const request &message) const
{
    return message.method == "ping";
}

std::optional<json::value> ping_handler::process(const request &message)
{
    if (message.is_notification())
        return std::nullopt;

    return make_response(message.id, json::value::make_object());
}

tools_list_handler::tools_list_handler(const tool_registry &tools)
    : tools_(tools)
{
}

const char *tools_list_handler::name() const { return "tools/list"; }

bool tools_list_handler::can_handle(const request &message) const
{
    return message.method == "tools/list";
}

std::optional<json::value> tools_list_handler::process(
    const request &message)
{
    if (message.is_notification())
        return std::nullopt;

    json::value result = json::value::make_object();
    result.set("tools", tools_.list());

    // the whole list is returned in one go, so there is no cursor. the
    // field is omitted entirely rather than sent as null, which is what
    // clients expect when pagination has finished.
    return make_response(message.id, std::move(result));
}

tools_call_handler::tools_call_handler(tool_registry &tools)
    : tools_(tools)
{
}

const char *tools_call_handler::name() const { return "tools/call"; }

bool tools_call_handler::can_handle(const request &message) const
{
    return message.method == "tools/call";
}

std::optional<json::value> tools_call_handler::process(
    const request &message)
{
    if (message.is_notification())
        return std::nullopt;

    const json::value &params = message.params;
    const json::value &name_field = params["name"];

    if (!name_field.is_string() || name_field.as_string().empty()) {
        return make_error(message.id, error_code::invalid_params,
                          "tools/call requires a \"name\" string");
    }

    const std::string requested = name_field.as_string();
    tool *target = tools_.find(requested);

    if (target == nullptr) {
        return make_error(message.id, error_code::invalid_params,
                          "no tool named '" + requested +
                              "'; call tools/list for the available "
                              "tools");
    }

    const json::value &arguments = params["arguments"];

    if (!arguments.is_null() && !arguments.is_object()) {
        return make_error(message.id, error_code::invalid_params,
                          "\"arguments\" must be an object when "
                          "present");
    }

    // a tool that cannot do what was asked answers with isError set,
    // not with a JSON-RPC error: the model is meant to read the reason
    // and try something else.
    const tool_result result = target->invoke(arguments);
    return make_response(message.id, result.to_json());
}

const char *fallback_handler::name() const { return "fallback"; }

bool fallback_handler::can_handle(const request &) const { return true; }

std::optional<json::value> fallback_handler::process(
    const request &message)
{
    // an unknown notification is dropped in silence, as the spec
    // requires; an unknown request gets a proper error.
    if (message.is_notification())
        return std::nullopt;

    return make_error(message.id, error_code::method_not_found,
                      "unsupported method '" + message.method +
                          "'; this server implements initialize, "
                          "tools/list, tools/call and ping");
}

} // namespace mcp
