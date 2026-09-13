#include "eng/log/Logger.hpp"

#include <algorithm>

namespace eng::log {

Logger& Logger::get() noexcept {
    static Logger instance; // magic static: thread-safe na inicialização
    return instance;
}

void Logger::addSink(LogSink& sink) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (std::find(sinks_.begin(), sinks_.end(), &sink) == sinks_.end()) {
        sinks_.push_back(&sink); // idempotente: duplicata é ignorada
    }
}

void Logger::removeSink(LogSink& sink) {
    const std::lock_guard<std::mutex> lock(mutex_);
    sinks_.erase(std::remove(sinks_.begin(), sinks_.end(), &sink), sinks_.end());
}

void Logger::setMinLevel(LogLevel level) noexcept {
    const std::lock_guard<std::mutex> lock(mutex_);
    minLevel_ = level;
}

LogLevel Logger::minLevel() const noexcept {
    const std::lock_guard<std::mutex> lock(mutex_);
    return minLevel_;
}

void Logger::log(LogLevel level, std::string_view category, std::string_view message) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (level < minLevel_) {
        return;
    }
    for (LogSink* sink : sinks_) {
        sink->write(level, category, message);
    }
}

void Logger::flush() {
    const std::lock_guard<std::mutex> lock(mutex_);
    for (LogSink* sink : sinks_) {
        sink->flush();
    }
}

std::size_t Logger::sinkCount() const noexcept {
    const std::lock_guard<std::mutex> lock(mutex_);
    return sinks_.size();
}

} // namespace eng::log
