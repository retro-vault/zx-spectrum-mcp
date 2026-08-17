//
// persistent screen capture tools.
//
// screenshot writes the same indexed PNG returned by the screen tool. Video
// records every completed emulated frame as YUV4MPEG2: it is a deliberately
// simple, streamable video format that needs no platform codecs or external
// processes and preserves the Spectrum's exact 50.08 Hz frame rate.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "png/encoder.h"
#include "tools/registration.h"
#include "tools/support.h"

namespace tools {

namespace {

struct indexed_image {
    std::vector<spectrum::u8> pixels;
    int width = 0;
    int height = 0;
};

//
// Copy either the complete visible raster or its 256x192 display area.
//
indexed_image capture_indexed(const spectrum::framebuffer &frame,
                              const spectrum::machine_timing &timing,
                              bool include_border)
{
    indexed_image image;

    if (include_border) {
        const std::span<const spectrum::u8> source = frame.pixels();
        image.pixels.assign(source.begin(), source.end());
        image.width = frame.width();
        image.height = frame.height();
        return image;
    }

    const int x0 = timing.display_origin_x();
    const int y0 = timing.display_origin_y();
    image.width = timing.display_tstates * 2;
    image.height = timing.display_lines;
    image.pixels.reserve(static_cast<std::size_t>(image.width) *
                         static_cast<std::size_t>(image.height));

    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x)
            image.pixels.push_back(frame.pixel(x0 + x, y0 + y));
    }
    return image;
}

std::vector<png::colour> encoder_palette()
{
    std::vector<png::colour> result;
    result.reserve(spectrum::zx_palette().size());
    for (const spectrum::rgb &entry : spectrum::zx_palette())
        result.push_back({entry.r, entry.g, entry.b});
    return result;
}

bool usable_path(const std::string &path)
{
    return !path.empty() && path.find('\0') == std::string::npos;
}

//
// An active video stream shared by the start and stop tool objects.
//
class video_session {
public:
    ~video_session()
    {
        if (file_.is_open())
            file_.close();
    }

    bool start(const std::string &path, bool include_border,
               const spectrum::machine_timing &timing,
               const spectrum::framebuffer &frame, std::string &error)
    {
        if (active_) {
            error = "a video recording is already active";
            return false;
        }
        if (!usable_path(path)) {
            error = "'path' must be a non-empty file name";
            return false;
        }

        path_ = path;
        include_border_ = include_border;
        width_ = include_border ? frame.width() : timing.display_tstates * 2;
        height_ = include_border ? frame.height() : timing.display_lines;
        frames_ = 0;
        bytes_ = 0;
        error_.clear();

        file_.clear();
        file_.open(path_, std::ios::binary | std::ios::trunc);
        if (!file_) {
            error = "cannot open '" + path_ + "' for video output";
            return false;
        }

        const std::string header =
            "YUV4MPEG2 W" + std::to_string(width_) + " H" +
            std::to_string(height_) + " F" +
            std::to_string(timing.cpu_hz) + ":" +
            std::to_string(timing.tstates_per_frame()) +
            " Ip A1:1 C444\n";
        file_.write(header.data(), static_cast<std::streamsize>(header.size()));
        if (!file_) {
            file_.close();
            error = "cannot write the video header to '" + path_ + "'";
            return false;
        }

        bytes_ = header.size();
        active_ = true;
        error.clear();
        return true;
    }

    void capture(const spectrum::framebuffer &frame,
                 const spectrum::machine_timing &timing)
    {
        if (!active_ || !error_.empty())
            return;

        const indexed_image image =
            capture_indexed(frame, timing, include_border_);
        const std::size_t plane_size = image.pixels.size();
        std::vector<spectrum::u8> yuv(plane_size * 3);
        const std::span<const spectrum::rgb> palette = spectrum::zx_palette();

        for (std::size_t i = 0; i < plane_size; ++i) {
            const spectrum::rgb colour =
                palette[image.pixels[i] & 0x0f];
            const int r = colour.r;
            const int g = colour.g;
            const int b = colour.b;
            const auto byte = [](int value) {
                return static_cast<spectrum::u8>(
                    std::clamp(value, 0, 255));
            };

            // Integer BT.601 studio-range conversion. C444 retains one
            // chroma value per Spectrum pixel, avoiding colour bleeding.
            yuv[i] = byte(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
            yuv[plane_size + i] =
                byte(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
            yuv[plane_size * 2 + i] =
                byte(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
        }

        constexpr std::string_view marker = "FRAME\n";
        file_.write(marker.data(),
                    static_cast<std::streamsize>(marker.size()));
        file_.write(reinterpret_cast<const char *>(yuv.data()),
                    static_cast<std::streamsize>(yuv.size()));
        if (!file_) {
            error_ = "a write failed while recording '" + path_ + "'";
            return;
        }

        ++frames_;
        bytes_ += marker.size() + yuv.size();
    }

    bool active() const { return active_; }
    const std::string &path() const { return path_; }
    int width() const { return width_; }
    int height() const { return height_; }
    spectrum::u64 frames() const { return frames_; }
    std::size_t bytes() const { return bytes_; }

    bool stop(std::string &error)
    {
        if (!active_) {
            error = "no video recording is active";
            return false;
        }

        active_ = false;
        file_.close();
        if (!error_.empty()) {
            error = error_;
            return false;
        }
        if (file_.fail()) {
            error = "cannot finish video output '" + path_ + "'";
            return false;
        }

        error.clear();
        return true;
    }

private:
    std::ofstream file_;
    std::string path_;
    std::string error_;
    bool active_ = false;
    bool include_border_ = true;
    int width_ = 0;
    int height_ = 0;
    spectrum::u64 frames_ = 0;
    std::size_t bytes_ = 0;
};

class screenshot_tool final : public machine_tool {
public:
    using machine_tool::machine_tool;

    std::string name() const override { return "screenshot"; }

    std::string description() const override
    {
        return "Save the current rendered screen to a PNG file, replacing "
               "an existing file at that path. By default "
               "the 352x288 border is included; it can be cropped to the "
               "256x192 display or enlarged by an integer scale.";
    }

    json::value input_schema() const override
    {
        return schema_builder()
            .string("path", "Destination PNG file path.")
            .boolean("include_border",
                     "Include the screen border. Default true.")
            .integer("scale", "Integer magnification, 1 to 4. Default 1.",
                     1, 4)
            .required({"path"})
            .build();
    }

    mcp::tool_result invoke(const json::value &arguments) override
    {
        const std::string path = arguments["path"].as_string();
        if (!usable_path(path))
            return mcp::tool_result::failure(
                "'path' must be a non-empty file name");

        const auto scale = read_int_in(arguments["scale"], 1, 4, 1);
        if (!scale)
            return mcp::tool_result::failure(
                "'scale' must be between 1 and 4");

        const bool include_border =
            arguments["include_border"].as_bool(true);
        const indexed_image image = capture_indexed(
            machine().video().screen(), machine().timing(), include_border);
        const std::vector<std::uint8_t> png = png::encode_indexed(
            image.pixels, image.width, image.height, encoder_palette(),
            static_cast<int>(*scale));
        if (png.empty())
            return mcp::tool_result::failure(
                "the PNG encoder failed to produce an image");

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
            return mcp::tool_result::failure(
                "cannot open '" + path + "' for screenshot output");
        output.write(reinterpret_cast<const char *>(png.data()),
                     static_cast<std::streamsize>(png.size()));
        output.close();
        if (output.fail())
            return mcp::tool_result::failure(
                "cannot write screenshot output '" + path + "'");

        const int width = image.width * static_cast<int>(*scale);
        const int height = image.height * static_cast<int>(*scale);
        json::value structured = json::value::make_object();
        structured.set("path", json::value(path));
        structured.set("width", json::value(width));
        structured.set("height", json::value(height));
        structured.set("frame", json::value(machine().frame_number()));
        structured.set("bytes", json::value(
                                    static_cast<std::int64_t>(png.size())));

        return mcp::tool_result::of(
            "saved " + std::to_string(width) + "x" +
                std::to_string(height) + " PNG screenshot to '" + path +
                "'",
            std::move(structured));
    }
};

class video_start_tool final : public machine_tool {
public:
    video_start_tool(spectrum::machine &target,
                     std::shared_ptr<video_session> session)
        : machine_tool(target), session_(std::move(session))
    {
    }

    std::string name() const override { return "video_start"; }

    std::string description() const override
    {
        return "Start recording completed emulated frames to a YUV4MPEG2 "
               "(.y4m) video at the Spectrum's exact 50.08 Hz rate. The "
               "destination is replaced. Recording advances only when the "
               "machine runs. Finish it with video_stop.";
    }

    json::value input_schema() const override
    {
        return schema_builder()
            .string("path", "Destination YUV4MPEG2 video file path.")
            .boolean("include_border",
                     "Include the screen border. Default true.")
            .required({"path"})
            .build();
    }

    mcp::tool_result invoke(const json::value &arguments) override
    {
        const std::string path = arguments["path"].as_string();
        const bool include_border =
            arguments["include_border"].as_bool(true);
        std::string error;
        if (!session_->start(path, include_border, machine().timing(),
                             machine().video().screen(), error)) {
            return mcp::tool_result::failure(error);
        }

        const std::shared_ptr<video_session> session = session_;
        const spectrum::machine_timing timing = machine().timing();
        machine().set_frame_observer(
            [session, timing](const spectrum::framebuffer &frame,
                              spectrum::u64) {
                session->capture(frame, timing);
            });

        json::value structured = json::value::make_object();
        structured.set("path", json::value(path));
        structured.set("width", json::value(session_->width()));
        structured.set("height", json::value(session_->height()));
        structured.set("format", json::value("yuv4mpeg2-c444"));
        structured.set("fps_numerator",
                       json::value(machine().timing().cpu_hz));
        structured.set("fps_denominator",
                       json::value(machine().timing().tstates_per_frame()));

        return mcp::tool_result::of(
            "recording " + std::to_string(session_->width()) + "x" +
                std::to_string(session_->height()) + " video to '" + path +
                "'",
            std::move(structured));
    }

private:
    std::shared_ptr<video_session> session_;
};

class video_stop_tool final : public machine_tool {
public:
    video_stop_tool(spectrum::machine &target,
                    std::shared_ptr<video_session> session)
        : machine_tool(target), session_(std::move(session))
    {
    }

    std::string name() const override { return "video_stop"; }

    std::string description() const override
    {
        return "Stop and close the active YUV4MPEG2 video recording, "
               "returning its path, frame count, duration and byte size.";
    }

    json::value input_schema() const override
    {
        return schema_builder().build();
    }

    mcp::tool_result invoke(const json::value &) override
    {
        if (!session_->active())
            return mcp::tool_result::failure(
                "no video recording is active");

        machine().set_frame_observer({});
        std::string error;
        const bool stopped = session_->stop(error);
        if (!stopped)
            return mcp::tool_result::failure(error);

        const double duration =
            static_cast<double>(session_->frames()) *
            machine().timing().tstates_per_frame() /
            machine().timing().cpu_hz;

        json::value structured = json::value::make_object();
        structured.set("path", json::value(session_->path()));
        structured.set("width", json::value(session_->width()));
        structured.set("height", json::value(session_->height()));
        structured.set("frames", json::value(session_->frames()));
        structured.set("duration_seconds", json::value(duration));
        structured.set("bytes", json::value(
                                    static_cast<std::int64_t>(
                                        session_->bytes())));

        char duration_text[64];
        std::snprintf(duration_text, sizeof duration_text, "%.3f", duration);
        return mcp::tool_result::of(
            "saved " + std::to_string(session_->frames()) + " frames (" +
                duration_text + " seconds) to '" + session_->path() + "'",
            std::move(structured));
    }

private:
    std::shared_ptr<video_session> session_;
};

} // namespace

void register_capture_tools(mcp::tool_registry &registry,
                            spectrum::machine &target)
{
    auto session = std::make_shared<video_session>();
    registry.add(std::make_unique<screenshot_tool>(target));
    registry.add(std::make_unique<video_start_tool>(target, session));
    registry.add(std::make_unique<video_stop_tool>(target, session));
}

} // namespace tools
