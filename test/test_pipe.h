#pragma once
#include <string>

namespace test {

const std::string PipeName = "\\\\.\\pipe\\testpipe";

constexpr size_t LargePipeBufferSize = 4096;
constexpr size_t SmallPipeBufferSize = 20;

} // namespace test