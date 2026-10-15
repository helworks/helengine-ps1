#pragma once
#include <stdexcept>
namespace he_cpp_custom { [[noreturn]] inline void Fail(const char* message) { throw std::runtime_error(message); } }
