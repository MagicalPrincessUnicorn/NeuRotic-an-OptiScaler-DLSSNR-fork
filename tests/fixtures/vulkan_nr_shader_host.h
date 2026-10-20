#pragma once
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdio>
// Standalone non-proprietary host: preserve shader implementation and Vulkan calls,
// removing only the application's logging and unrelated precompiled dependencies.
#define LOG_ERROR(...) std::fprintf(stderr, "Vulkan shader operation failed\n")
#define LOG_DEBUG(...) ((void)0)
#define LOG_INFO(...) ((void)0)
