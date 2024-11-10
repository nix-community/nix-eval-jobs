#include <cstring>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
// NOLINTBEGIN(modernize-deprecated-headers)
// misc-include-cleaner wants this header rather than the C++ version
#include <stdio.h>
// NOLINTEND(modernize-deprecated-headers)
#include <cstdio>
#include <nix/error.hh>
#include <nix/signals.hh>
#include <nix/signals-impl.hh>
#include <string>
#include <string_view>

#include "buffered-io.hh"

[[nodiscard]] auto tryWriteLine(int fd, std::string s) -> int {
    s += "\n";
    std::string_view sv{s};
    while (!sv.empty()) {
        nix::checkInterrupt();
        const ssize_t res = write(fd, sv.data(), sv.size());
        if (res == -1 && errno != EINTR) {
            return -errno;
        }
        if (res > 0) {
            sv.remove_prefix(res);
        }
    }
    return 0;
}

LineReader::LineReader(int fd) : stream(fdopen(fd, "r")) {
    if (stream == nullptr) {
        throw nix::Error("fdopen(%d) failed: %s", fd, strerror(errno));
    }
}

LineReader::LineReader(LineReader &&other) noexcept
    : stream(other.stream.release()), buffer(other.buffer.release()), len(other.len) {
    other.stream = nullptr;
    other.len = 0;
}

[[nodiscard]] auto LineReader::readLine() -> std::string_view {
    char *buf = buffer.release();
    const ssize_t read = getline(&buf, &len, stream.get());
    buffer.reset(buf);

    if (read == -1) {
        return {}; // Return an empty string_view in case of error
    }

    nix::checkInterrupt();

    // Remove trailing newline
    char *line = buffer.get();
    return {line, static_cast<size_t>(read) - 1};
}
