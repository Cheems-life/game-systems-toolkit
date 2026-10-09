#include <designer/persistence/file_store.hpp>

#include <algorithm>
#include <fstream>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace designer::persistence {
namespace {
bool valid_path(const std::filesystem::path& path) {
    const auto name = path.filename().string();
    const auto text = path.string();
    return !name.empty() && name != "." && name != ".." && text.find('\0') == std::string::npos;
}
#if defined(__unix__) || defined(__APPLE__)
struct Descriptor {
    int value;
    ~Descriptor() { if (value >= 0) ::close(value); }
};
struct TemporaryFile {
    int descriptor;
    const char* path;
    bool committed = false;
    ~TemporaryFile() {
        if (descriptor >= 0) ::close(descriptor);
        if (!committed) ::unlink(path);
    }
};
bool sync(int descriptor) {
    int result;
    do { result = ::fsync(descriptor); } while (result < 0 && errno == EINTR);
    return result == 0;
}
#endif
} // namespace

const char* file_error_name(FileError error) noexcept {
    switch (error) {
    case FileError::none: return "none";
    case FileError::invalid_path: return "invalid_path";
    case FileError::not_found: return "not_found";
    case FileError::too_large: return "too_large";
    case FileError::io_error: return "io_error";
    case FileError::unsupported_platform: return "unsupported_platform";
    case FileError::durability_uncertain: return "durability_uncertain";
    }
    return "unrecognized_error";
}

SaveResult save_atomic(const std::filesystem::path& path, std::string_view bytes, std::size_t max_bytes) {
    if (!valid_path(path)) return {FileError::invalid_path, false};
    if (bytes.size() > max_bytes) return {FileError::too_large, false};
#if defined(__unix__) || defined(__APPLE__)
    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
    const auto destination = path.string();
    const auto parent_text = parent.string();
    const auto pattern = (parent / ("." + path.filename().string() + ".tmp.XXXXXX")).string();
    std::vector<char> temporary_path(pattern.begin(), pattern.end());
    temporary_path.push_back('\0');
    Descriptor directory{::open(parent_text.c_str(), O_RDONLY | O_DIRECTORY)};
    if (directory.value < 0) return {FileError::io_error, false};
    const int descriptor = ::mkstemp(temporary_path.data());
    if (descriptor < 0) return {FileError::io_error, false};
    TemporaryFile temporary{descriptor, temporary_path.data()};
    std::size_t written = 0;
    while (written < bytes.size()) {
        const auto amount = std::min<std::size_t>(bytes.size() - written, 1024 * 1024);
        const auto result = ::write(descriptor, bytes.data() + written, amount);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return {FileError::io_error, false};
        written += static_cast<std::size_t>(result);
    }
    if (!sync(descriptor)) return {FileError::io_error, false};
    const auto closed = ::close(descriptor);
    temporary.descriptor = -1;
    if (closed != 0) return {FileError::io_error, false};
    if (::rename(temporary_path.data(), destination.c_str()) != 0) return {FileError::io_error, false};
    temporary.committed = true;
    if (!sync(directory.value)) return {FileError::durability_uncertain, true};
    return {FileError::none, true};
#else
    return {FileError::unsupported_platform, false};
#endif
}

FileError load_file(const std::filesystem::path& path, std::string& output, std::size_t max_bytes) {
    if (!valid_path(path)) return FileError::invalid_path;
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        if (error == std::errc::no_such_file_or_directory) return FileError::not_found;
        return FileError::io_error;
    }
    if (size > max_bytes) return FileError::too_large;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return FileError::io_error;
    std::string candidate(static_cast<std::size_t>(size), '\0');
    // Chunked reads avoid converting an unbounded size to streamsize.
    std::size_t read = 0;
    while (read < candidate.size()) {
        const auto amount = std::min<std::size_t>(candidate.size() - read, 1024 * 1024);
        if (!stream.read(candidate.data() + read, static_cast<std::streamsize>(amount))) return FileError::io_error;
        read += amount;
    }
    if (stream.peek() != std::char_traits<char>::eof() || stream.bad()) return FileError::io_error;
    output.swap(candidate);
    return FileError::none;
}

} // namespace designer::persistence
