#pragma once
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <algorithm>
#include <format>
#include <mutex>
#include <stdexcept>
#include <cstdint>
#define LOG_ERROR(...) ((void)0)
#define LOG_WARN(...) ((void)0)
#define LOG_INFO(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
#define SAFE_RELEASE(p) do { if (p) { (p)->Release(); (p) = nullptr; } } while (0)
struct ScopedSkipHeapCapture {};
namespace Util { inline void GetDeviceRemovedReason(ID3D12Device*) {} }
struct State { bool isShuttingDown = false; static State& Instance() { static State state; return state; } };
