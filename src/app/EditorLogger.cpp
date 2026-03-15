#include "meshtools/app/EditorLogger.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

namespace meshtools::app {

namespace {

std::string makeTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);

    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &now_time);
#else
    localtime_r(&now_time, &local_time);
#endif

    std::ostringstream stream;
    stream << std::put_time(&local_time, "%H:%M:%S")
           << '.'
           << std::setw(3)
           << std::setfill('0')
           << milliseconds.count();
    return stream.str();
}

}  // namespace

void EditorLogger::append(std::string origin, std::string message) {
    std::string log_line = '[' + makeTimestamp() + "][" + std::move(origin) + "] " + std::move(message);
    std::cout << log_line << std::endl;
    messages_.push_back(std::move(log_line));
    constexpr std::size_t max_log_messages = 200;
    if (messages_.size() > max_log_messages) {
        const auto overflow = static_cast<std::vector<std::string>::difference_type>(messages_.size() - max_log_messages);
        messages_.erase(messages_.begin(), messages_.begin() + overflow);
    }
}

void EditorLogger::replace(std::vector<std::string> messages) {
    messages_ = std::move(messages);
}

const std::vector<std::string>& EditorLogger::messages() const {
    return messages_;
}

}  // namespace meshtools::app
