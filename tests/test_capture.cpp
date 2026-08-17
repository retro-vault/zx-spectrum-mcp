//
// persistent PNG screenshot and YUV4MPEG2 video commands.
//
// This is an ordinary standalone C++ suite like the rest of the project. It
// invokes the real registered MCP tools, inspects the files they write, and
// verifies that video frame count follows emulated rather than wall time.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <vector>

#include "mcp/tool_registry.h"
#include "spectrum/machine.h"
#include "test_support.h"
#include "tools/registration.h"

namespace {

std::vector<unsigned char> read_file(const std::string &path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

std::string output_path(const char *extension)
{
    char executable[4096];
    const ssize_t length =
        ::readlink("/proc/self/exe", executable, sizeof executable - 1);
    std::string directory = ".";
    if (length > 0) {
        executable[length] = '\0';
        const std::string path(executable);
        const std::size_t slash = path.find_last_of('/');
        if (slash != std::string::npos)
            directory = path.substr(0, slash);
    }

    return directory + "/capture-" +
           std::to_string(static_cast<long long>(::getpid())) + extension;
}

void test_screenshot_command()
{
    test::section("screenshot command");

    spectrum::machine target;
    mcp::tool_registry registry;
    tools::register_capture_tools(registry, target);
    mcp::tool *screenshot = registry.find("screenshot");
    test::check(screenshot != nullptr, "the screenshot tool is registered");
    if (!screenshot)
        return;

    const std::string path = output_path(".png");
    json::value arguments = json::value::make_object();
    arguments.set("path", json::value(path));
    arguments.set("include_border", json::value(false));
    arguments.set("scale", json::value(2));
    const mcp::tool_result result = screenshot->invoke(arguments);

    test::check(!result.is_error, "a PNG screenshot is written");
    test::check_eq(result.structured["width"].as_int(), 512,
                   "scale applies to the cropped display width");
    test::check_eq(result.structured["height"].as_int(), 384,
                   "scale applies to the cropped display height");
    test::check_eq_str(result.structured["path"].as_string(), path,
                       "the result returns the destination path");

    const std::vector<unsigned char> file = read_file(path);
    const unsigned char signature[] = {0x89, 'P', 'N', 'G',
                                       '\r', '\n', 0x1a, '\n'};
    bool png_signature = file.size() >= sizeof signature;
    for (std::size_t i = 0; png_signature && i < sizeof signature; ++i)
        png_signature = file[i] == signature[i];
    test::check(png_signature, "the saved file has a PNG signature");
    test::check(file.size() > 100,
                "the saved PNG contains encoded image data");

    json::value bad_scale = json::value::make_object();
    bad_scale.set("path", json::value(path));
    bad_scale.set("scale", json::value(5));
    test::check(screenshot->invoke(bad_scale).is_error,
                "an invalid screenshot scale is rejected");

    (void)std::remove(path.c_str());
}

void test_video_commands()
{
    test::section("video start and stop commands");

    spectrum::machine target;
    mcp::tool_registry registry;
    tools::register_capture_tools(registry, target);
    mcp::tool *start = registry.find("video_start");
    mcp::tool *stop = registry.find("video_stop");
    test::check(start != nullptr, "the video_start tool is registered");
    test::check(stop != nullptr, "the video_stop tool is registered");
    if (!start || !stop)
        return;

    test::check(stop->invoke(json::value::make_object()).is_error,
                "stopping without a recording is rejected");

    const std::string path = output_path(".y4m");
    json::value arguments = json::value::make_object();
    arguments.set("path", json::value(path));
    arguments.set("include_border", json::value(false));
    const mcp::tool_result started = start->invoke(arguments);
    test::check(!started.is_error, "video recording starts");
    test::check_eq(started.structured["width"].as_int(), 256,
                   "cropped video is 256 pixels wide");
    test::check_eq(started.structured["height"].as_int(), 192,
                   "cropped video is 192 pixels high");
    test::check(start->invoke(arguments).is_error,
                "a second simultaneous recording is rejected");

    const spectrum::run_result run = target.run_frames(2);
    test::check_eq(run.frames, 2,
                   "the machine completed the requested video frames");

    const mcp::tool_result stopped =
        stop->invoke(json::value::make_object());
    test::check(!stopped.is_error, "video recording stops cleanly");
    test::check_eq(stopped.structured["frames"].as_int(), 2,
                   "exactly two completed frames were recorded");
    test::check(stopped.structured["duration_seconds"].as_double() > 0.039,
                "the result reports an exact-rate duration");

    const std::vector<unsigned char> file = read_file(path);
    const std::string header =
        "YUV4MPEG2 W256 H192 F3500000:69888 Ip A1:1 C444\n";
    const std::size_t plane = 256 * 192;
    const std::size_t frame_size = 6 + plane * 3;
    test::check(file.size() == header.size() + frame_size * 2,
                "the YUV4MPEG stream has two complete C444 frames");
    const std::string beginning(file.begin(),
                                file.begin() +
                                    std::min(file.size(), header.size()));
    test::check_eq_str(beginning, header,
                       "the video header preserves the exact frame rate");

    const auto marker_at = [&file](std::size_t offset) {
        constexpr char marker[] = "FRAME\n";
        if (offset + sizeof marker - 1 > file.size())
            return false;
        for (std::size_t i = 0; i < sizeof marker - 1; ++i) {
            if (file[offset + i] !=
                static_cast<unsigned char>(marker[i])) {
                return false;
            }
        }
        return true;
    };
    test::check(marker_at(header.size()),
                "the first YUV frame has its marker");
    test::check(marker_at(header.size() + frame_size),
                "the second YUV frame has its marker");

    const mcp::tool_result restarted = start->invoke(arguments);
    test::check(!restarted.is_error,
                "a new recording can start after the first is closed");
    (void)target.run_frames(1);
    const mcp::tool_result restopped =
        stop->invoke(json::value::make_object());
    test::check(!restopped.is_error, "the second recording also stops");
    test::check_eq(restopped.structured["frames"].as_int(), 1,
                   "the new recording has its own frame count");

    (void)std::remove(path.c_str());
}

} // namespace

int main()
{
    test_screenshot_command();
    test_video_commands();
    return test::summary("capture");
}
