#include <cstring>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <sys/types.h>
#include <nix/error.hh>
#include <nix/signals.hh>
#include <nix/signals-impl.hh>

#include "buffered-io.hh"

[[nodiscard]] int tryWriteLine(int fd, std::string s) {
    s += "\n";
    std::string_view sv{s};
    while (!sv.empty()) {
        nix::checkInterrupt();
        ssize_t res = write(fd, sv.data(), sv.size());
        if (res == -1 && errno != EINTR) {
            return -errno;
        }
        if (res > 0) {
            sv.remove_prefix(res);
        }
    }
    return 0;
}

LineReader::LineReader(int fd) {
    stream = fdopen(fd, "r");
    if (!stream) {
        throw nix::Error("fdopen(%d) failed: %s", fd, strerror(errno));
    }
}

LineReader::~LineReader() {
    fclose(stream);
    free(buffer);
}

LineReader::LineReader(LineReader &&other) noexcept {
    stream = other.stream;
    other.stream = nullptr;
    buffer = other.buffer;
    other.buffer = nullptr;
    len = other.len;
    other.len = 0;
}

[[nodiscard]] std::string_view LineReader::readLine() {
    ssize_t read = getline(&buffer, &len, stream);

    if (read == -1) {
        return {}; // Return an empty string_view in case of error
    }

    nix::checkInterrupt();

    // Remove trailing newline
    return { buffer, static_cast<size_t>(read) - 1 };
}
