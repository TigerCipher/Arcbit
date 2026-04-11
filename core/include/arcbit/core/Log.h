#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <array>
#include <memory>
#include <string>

// ---------------------------------------------------------------------------
// DLL export / import
// ---------------------------------------------------------------------------
#if defined(_WIN32)
    #if defined(ARCBIT_CORE_EXPORTS)
        #define ARCBIT_CORE_API __declspec(dllexport)
    #else
        #define ARCBIT_CORE_API __declspec(dllimport)
    #endif
#else
    #if defined(ARCBIT_CORE_EXPORTS)
        #define ARCBIT_CORE_API __attribute__((visibility("default")))
    #else
        #define ARCBIT_CORE_API
    #endif
#endif

namespace Arcbit {

// ---------------------------------------------------------------------------
// Channel — identifies which engine subsystem produced a log message.
// Each channel can have its log level tuned independently.
// ---------------------------------------------------------------------------
enum class Channel : uint8_t
{
    Engine = 0,
    Render,
    ECS,
    Platform,
    Audio,
    Combat,
    Scripting,
    Count
};

// ---------------------------------------------------------------------------
// Log
// ---------------------------------------------------------------------------
class ARCBIT_CORE_API Log
{
public:
    // Call once at startup before any other threads are running.
    // fileOutput: if true, also writes all messages to `filePath`.
    static void Init(bool fileOutput = false, const std::string& filePath = "arcbit.log");
    static void Shutdown();

    // Per-channel level override (e.g. silence Render spam during combat work).
    static void SetLevel(Channel channel, spdlog::level::level_enum level);
    static void SetAllLevels(spdlog::level::level_enum level);

    // -----------------------------------------------------------------------
    // Logging methods — one per severity level.
    // Thread-safe: spdlog loggers are internally mutex-protected.
    // -----------------------------------------------------------------------
    template<typename... Args>
    static void Trace(Channel channel, fmt::format_string<Args...> fmt, Args&&... args)
    {
        GetLogger(channel).trace(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void Debug(Channel channel, fmt::format_string<Args...> fmt, Args&&... args)
    {
        GetLogger(channel).debug(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void Info(Channel channel, fmt::format_string<Args...> fmt, Args&&... args)
    {
        GetLogger(channel).info(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void Warn(Channel channel, fmt::format_string<Args...> fmt, Args&&... args)
    {
        GetLogger(channel).warn(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void Error(Channel channel, fmt::format_string<Args...> fmt, Args&&... args)
    {
        GetLogger(channel).error(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void Critical(Channel channel, fmt::format_string<Args...> fmt, Args&&... args)
    {
        GetLogger(channel).critical(fmt, std::forward<Args>(args)...);
    }

private:
    static spdlog::logger& GetLogger(Channel channel);

    static constexpr size_t ChannelCount = static_cast<size_t>(Channel::Count);
    static std::array<std::shared_ptr<spdlog::logger>, ChannelCount> s_Loggers;
};

} // namespace Arcbit

// ---------------------------------------------------------------------------
// Convenience macros — shorter call sites, channel baked in.
//
// Usage:
//   LOG_INFO(Engine,    "Device created");
//   LOG_WARN(Render,    "Swapchain suboptimal, recreating");
//   LOG_ERROR(ECS,      "Entity {} not found", id);
// ---------------------------------------------------------------------------
#define LOG_TRACE(channel, ...)    ::Arcbit::Log::Trace(   ::Arcbit::Channel::channel, __VA_ARGS__)
#define LOG_DEBUG(channel, ...)    ::Arcbit::Log::Debug(   ::Arcbit::Channel::channel, __VA_ARGS__)
#define LOG_INFO(channel, ...)     ::Arcbit::Log::Info(    ::Arcbit::Channel::channel, __VA_ARGS__)
#define LOG_WARN(channel, ...)     ::Arcbit::Log::Warn(    ::Arcbit::Channel::channel, __VA_ARGS__)
#define LOG_ERROR(channel, ...)    ::Arcbit::Log::Error(   ::Arcbit::Channel::channel, __VA_ARGS__)
#define LOG_CRITICAL(channel, ...) ::Arcbit::Log::Critical(::Arcbit::Channel::channel, __VA_ARGS__)
