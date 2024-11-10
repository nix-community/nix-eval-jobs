#pragma once
#include <cstdio>
#include <string>
#include <string_view>

[[nodiscard]] auto tryWriteLine(int fd, std::string s) -> int;

class LineReader {
  public:
    LineReader(int fd);
    ~LineReader();

    LineReader(LineReader &&other) noexcept;
    [[nodiscard]] auto readLine() -> std::string_view;

  private:
    FILE *stream = nullptr;
    char *buffer = nullptr;
    size_t len = 0;
};
