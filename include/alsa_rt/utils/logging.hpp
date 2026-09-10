#pragma once

#include <functional>
#include <string_view>

namespace alsa_rt
{

    enum class LogLevel
    {
        Debug,
        Info,
        Warn,
        Error
    };

    using LogSink = std::function<void(LogLevel, std::string_view)>;

    inline LogSink &defaultLogSink()
    {
        static LogSink sink = [](LogLevel, std::string_view) {};
        return sink;
    }

} // namespace alsa_rt
