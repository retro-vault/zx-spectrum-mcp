//
// entry point: assemble a machine, a tool set and a server, then serve.
//
// the process is an MCP server speaking JSON-RPC over stdin and stdout.
// that has one hard consequence which shapes everything here: stdout
// carries the protocol and nothing else. every diagnostic, banner and
// error message goes to stderr, because a single stray line on stdout
// would be parsed as a message and break the session.
//
// one machine is created and lives for the life of the process. the
// tools hold a reference to it, so state accumulates across calls: a
// program loaded by one call is still there for the next, which is what
// makes a conversation with the emulator possible.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <charconv>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "json/writer.h"
#include "mcp/server.h"
#include "mcp/tool_registry.h"
#include "mcp/transport.h"
#include "spectrum/machine.h"
#include "spectrum/snapshot.h"
#include "tools/registration.h"

namespace {

constexpr const char *server_name = "zx-spectrum-mcp";
constexpr const char *server_version = "1.0.0";

//
// what the command line asked for.
//
struct options {
    std::string rom_path;
    std::string interface1_rom_path;
    std::string load_path;
    bool serial = false;
    int serial_data_port = 6601;
    int serial_control_port = 6602;
    bool verbose = false;
    bool show_help = false;
    bool show_version = false;
    bool list_tools = false;
    bool failed = false;
    std::string error;
};

void print_usage(std::ostream &out)
{
    out << "usage: " << server_name << " [options]\n"
        << "\n"
        << "A cycle-accurate ZX Spectrum 48K emulator that speaks the\n"
        << "Model Context Protocol over stdin and stdout. With no\n"
        << "options it serves the protocol; the machine has no display\n"
        << "and is driven entirely through tool calls.\n"
        << "\n"
        << "options:\n"
        << "  --rom PATH      load a 16K ROM image before serving.\n"
        << "                  without one the machine boots a small\n"
        << "                  built-in stub instead of BASIC.\n"
        << "  --interface1-rom PATH\n"
        << "                  load an 8K Interface 1 shadow ROM and\n"
        << "                  attach its serial hardware.\n"
        << "  --load PATH     load a program or snapshot at startup.\n"
        << "                  the format is taken from the extension:\n"
        << "                  .sna, .z80, .scr, .tap, .tzx; anything else is raw\n"
        << "                  binary placed at 0x8000.\n"
        << "  --serial        expose Interface 1 RS-232 over TCP ports\n"
        << "                  6601 (raw data) and 6602 (control).\n"
        << "  --serial-data-port PORT\n"
        << "                  select the raw data port and enable serial.\n"
        << "  --serial-control-port PORT\n"
        << "                  select the control port and enable serial.\n"
        << "  --verbose       log protocol activity to stderr.\n"
        << "  --list-tools    print the tool names and exit.\n"
        << "  --version       print the version and exit.\n"
        << "  --help          print this message and exit.\n"
        << "\n"
        << "See docs/manuals/USER-GUIDE.md for the tool reference.\n";
}

//
// Parse a decimal TCP port without accepting signs or trailing text.
//
std::optional<int> parse_tcp_port(std::string_view text)
{
    int value = 0;
    const char *begin = text.data();
    const char *end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc() || parsed.ptr != end || value < 1 ||
        value > 65535) {
        return std::nullopt;
    }
    return value;
}

options parse_options(int argc, char **argv)
{
    options result;

    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];

        const auto take_value = [&](const char *name) -> std::string {
            if (i + 1 >= argc) {
                result.failed = true;
                result.error = std::string(name) + " needs a value";
                return {};
            }
            return argv[++i];
        };

        if (argument == "--help" || argument == "-h") {
            result.show_help = true;
        } else if (argument == "--version") {
            result.show_version = true;
        } else if (argument == "--verbose" || argument == "-v") {
            result.verbose = true;
        } else if (argument == "--list-tools") {
            result.list_tools = true;
        } else if (argument == "--rom") {
            result.rom_path = take_value("--rom");
        } else if (argument == "--interface1-rom") {
            result.interface1_rom_path = take_value("--interface1-rom");
        } else if (argument == "--load") {
            result.load_path = take_value("--load");
        } else if (argument == "--serial") {
            result.serial = true;
        } else if (argument == "--serial-data-port") {
            const std::string value = take_value("--serial-data-port");
            if (!result.failed) {
                const auto port = parse_tcp_port(value);
                if (!port) {
                    result.failed = true;
                    result.error = "--serial-data-port must be between "
                                   "1 and 65535";
                } else {
                    result.serial = true;
                    result.serial_data_port = *port;
                }
            }
        } else if (argument == "--serial-control-port") {
            const std::string value = take_value("--serial-control-port");
            if (!result.failed) {
                const auto port = parse_tcp_port(value);
                if (!port) {
                    result.failed = true;
                    result.error = "--serial-control-port must be between "
                                   "1 and 65535";
                } else {
                    result.serial = true;
                    result.serial_control_port = *port;
                }
            }
        } else {
            result.failed = true;
            result.error = "unknown option '" + std::string(argument) +
                           "'";
        }

        if (result.failed)
            break;
    }

    return result;
}

//
// load a ROM image, reporting to stderr.
//
// a missing ROM is not fatal. the machine still runs loaded machine
// code against the stub, and saying so is more useful than refusing to
// start.
//
bool install_rom(spectrum::machine &target, const std::string &path)
{
    std::string error;
    const auto image = spectrum::read_file(path, error);

    if (!image) {
        std::cerr << server_name << ": " << error << '\n';
        return false;
    }

    if (image->size() > spectrum::memory::rom_size) {
        std::cerr << server_name << ": '" << path << "' is "
                  << image->size()
                  << " bytes, larger than the 16384 byte ROM area;"
                     " it will be truncated\n";
    }

    target.mem().load_rom(*image);
    target.reset(true);

    std::cerr << server_name << ": loaded ROM '" << path << "' ("
              << image->size() << " bytes)\n";
    return true;
}

//
// Load the Interface 1 shadow ROM and attach its port hardware.
//
bool install_interface1_rom(spectrum::machine &target,
                            const std::string &path)
{
    std::string error;
    const auto image = spectrum::read_file(path, error);

    if (!image) {
        std::cerr << server_name << ": " << error << '\n';
        return false;
    }

    if (!target.load_interface1_rom(*image, error)) {
        std::cerr << server_name << ": '" << path << "': " << error << '\n';
        return false;
    }

    std::cerr << server_name << ": loaded Interface 1 ROM '" << path
              << "' (" << image->size() << " bytes)\n";
    return true;
}

//
// load a startup image, choosing the format from the extension.
//
bool install_image(spectrum::machine &target, const std::string &path)
{
    std::string error;
    const auto image = spectrum::read_file(path, error);

    if (!image) {
        std::cerr << server_name << ": " << error << '\n';
        return false;
    }

    spectrum::load_options load;
    load.format = spectrum::snapshot_format_from_path(path).value_or(
        spectrum::snapshot_format::binary);
    load.address = 0x8000;

    const spectrum::load_result result =
        spectrum::load_into(target, *image, load);

    if (!result.ok) {
        std::cerr << server_name << ": " << result.error << '\n';
        return false;
    }

    std::cerr << server_name << ": " << result.description << '\n';
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    const options settings = parse_options(argc, argv);

    if (settings.failed) {
        std::cerr << server_name << ": " << settings.error << "\n\n";
        print_usage(std::cerr);
        return 2;
    }

    if (settings.show_help) {
        print_usage(std::cout);
        return 0;
    }

    if (settings.show_version) {
        std::cout << server_name << ' ' << server_version << '\n';
        return 0;
    }

    spectrum::machine emulated;
    mcp::tool_registry registry;
    tools::register_all_tools(registry, emulated);

    if (settings.list_tools) {
        std::cout << json::write_pretty(registry.list()) << '\n';
        return 0;
    }

    if (!settings.rom_path.empty())
        install_rom(emulated, settings.rom_path);

    if (!settings.interface1_rom_path.empty() &&
        !install_interface1_rom(emulated, settings.interface1_rom_path)) {
        return 2;
    }

    if (!settings.load_path.empty())
        install_image(emulated, settings.load_path);

    if (settings.serial) {
        spectrum::serial_bridge_config config;
        config.data_port = settings.serial_data_port;
        config.control_port = settings.serial_control_port;

        std::string error;
        if (!emulated.enable_serial_bridge(config, error)) {
            std::cerr << server_name << ": cannot start serial bridge: "
                      << error << '\n';
            return 2;
        }

        const spectrum::interface1_serial_status serial =
            emulated.serial().status();
        std::cerr << server_name << ": Interface 1 serial data on TCP "
                  << serial.data_port << ", control on TCP "
                  << serial.control_port << '\n';
    }

    mcp::server server({server_name, server_version}, registry);

    if (settings.verbose) {
        server.set_logger([](std::string_view line) {
            std::cerr << server_name << ": " << line << '\n';
        });
        std::cerr << server_name << ": serving " << registry.size()
                  << " tools on stdio\n";
    }

    mcp::stdio_transport io;
    server.run(io);

    return 0;
}
