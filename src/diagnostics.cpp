#include "pxr/base/arch/rDiagnostics.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <streambuf>
#include <vector>

namespace {
std::atomic<bool> failed{false};
struct Diagnostics {
    std::mutex mutex;
    std::array<std::array<char, 2048>, 128> messages{};
    size_t first = 0, count = 0;
    unsigned long long dropped = 0;
};
Diagnostics& diagnostics() { static Diagnostics state; return state; }
void record(const char* text) noexcept {
    try {
        auto& state = diagnostics();
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.count < state.messages.size()) {
            auto& message = state.messages[(state.first + state.count) % state.messages.size()];
            std::snprintf(message.data(), message.size(), "%s", text);
            ++state.count;
        } else if (state.dropped < std::numeric_limits<unsigned long long>::max()) {
            ++state.dropped;
        }
    } catch (...) { /* Logging cannot escape into a worker or C caller. */ }
}
// A null target denotes the diagnostic queue; non-null targets are real files.
int format_output(FILE* stream, const char* format, va_list args) noexcept {
    char buffer[2048];
    va_list copy;
    va_copy(copy, args);
    int count = std::vsnprintf(buffer, sizeof(buffer), format, copy);
    va_end(copy);
    if (count < 0) return count;
    if (!stream) {
        record(buffer);
        return count;
    }
    // Keep actual file output intact, including long formatted records. Only
    // console diagnostics are bounded; USD data must never be truncated.
    try {
        if (static_cast<size_t>(count) < sizeof(buffer))
            return std::fwrite(buffer, 1, count, stream) == static_cast<size_t>(count) ? count : -1;
        std::vector<char> text(static_cast<size_t>(count) + 1);
        std::vsnprintf(text.data(), text.size(), format, args);
        return std::fwrite(text.data(), 1, count, stream) == static_cast<size_t>(count) ? count : -1;
    } catch (...) { return -1; }
}
// One stream per thread prevents interleaved formatting and stream-state races.
// Flush on newline or a full buffer so long debug output stays bounded.
class DiagnosticBuffer : public std::streambuf {
    std::array<char, 2048> text{};
    size_t count = 0;
    int sync() override {
        if (count) {
            text[count] = '\0';
            record(text.data());
            count = 0;
        }
        return 0;
    }
    int_type overflow(int_type value) override {
        if (traits_type::eq_int_type(value, traits_type::eof())) return traits_type::not_eof(value);
        char c = traits_type::to_char_type(value);
        text[count++] = c;
        if (c == '\n' || count == text.size() - 1) sync();
        return value;
    }
public:
    ~DiagnosticBuffer() override { sync(); }
};
struct DiagnosticStream {
    DiagnosticBuffer buffer;
    std::ostream stream{&buffer};
};
}
extern "C" int LibOpenUsdPrintf(const char* format, ...) {
    va_list args; va_start(args, format);
    int result = format_output(nullptr, format, args);
    va_end(args);
    return result;
}
extern "C" int LibOpenUsdFprintf(FILE* stream, const char* format, ...) {
    va_list args; va_start(args, format);
    int result = format_output(stream, format, args);
    va_end(args);
    return result;
}
extern "C" int LibOpenUsdFputs(const char* text, FILE* stream) {
    if (!stream) { record(text); return 0; }
    size_t size = std::strlen(text);
    return std::fwrite(text, 1, size, stream) == size ? 0 : EOF;
}
extern "C" int LibOpenUsdPuts(const char* text) { return LibOpenUsdPrintf("%s\n", text); }
extern "C" int LibOpenUsdPutchar(int character) {
    char text[] = {static_cast<char>(character), '\0'};
    record(text);
    return static_cast<unsigned char>(character);
}
std::ostream& LibOpenUsdCout() { thread_local DiagnosticStream stream; return stream.stream; }
std::ostream& LibOpenUsdCerr() { return LibOpenUsdCout(); }
extern "C" int LibOpenUsdDiagnosticNext(char* message, size_t capacity) {
    if (!message || !capacity) return 0;
    message[0] = '\0';
    try {
        auto& state = diagnostics();
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.count) {
            std::snprintf(message, capacity, "%s", state.messages[state.first].data());
            state.first = (state.first + 1) % state.messages.size();
            --state.count;
            return 1;
        }
        if (state.dropped) {
            std::snprintf(message, capacity, "%llu additional OpenUSD diagnostics were dropped (queue capacity 128).", state.dropped);
            state.dropped = 0;
            return 1;
        }
    } catch (...) {}
    return 0;
}
void LibOpenUsdRecordFailure(const char* message) noexcept {
    failed.store(true, std::memory_order_release);
    record(message);
}
[[noreturn]] void LibOpenUsdFatal(const char* message) {
    LibOpenUsdRecordFailure(message);
    throw LibOpenUsdFatalError(message);
}
void LibOpenUsdCheckFailure() {
    if (failed.load(std::memory_order_acquire))
        throw LibOpenUsdFatalError("OpenUSD is unavailable after a native invariant failure; restart R.");
}
