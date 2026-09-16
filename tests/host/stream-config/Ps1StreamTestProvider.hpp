#pragma once
// Minimal runtime provider for the stream host test: standard storage is selected by the config,
// so only the fatal hook is required. The test executable defines it and exits with 73.
namespace he_cpp_custom {
[[noreturn]] void Fail(const char* message);
}
