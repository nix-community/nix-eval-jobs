#pragma once
#include <cstdio>
#include <string>
#include <string_view>
#include <memory>
#include <cstdlib>

[[nodiscard]] auto tryWriteLine(int fd, std::string s) -> int;

struct FileDeleter {
    void operator()(FILE *file) const {
        if (file != nullptr) {
            std::fclose(file); // NOLINT(cppcoreguidelines-owning-memory)
        }
    }
};

struct MemoryDeleter {
    void operator()(void *ptr) const {
        // NOLINTBEGIN(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
        std::free(ptr);
        // NOLINTEND(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
    }
};

class LineReader {
  public:
    LineReader(const LineReader &) = delete;
    explicit LineReader(int fd);
    auto operator=(const LineReader &) -> LineReader & = delete;
    auto operator=(LineReader &&) -> LineReader & = delete;
    ~LineReader() = default;

    LineReader(LineReader &&other) noexcept;
    [[nodiscard]] auto readLine() -> std::string_view;

  private:
    std::unique_ptr<FILE, FileDeleter> stream = nullptr;
    std::unique_ptr<char, MemoryDeleter> buffer = nullptr;
    size_t len = 0;
};
