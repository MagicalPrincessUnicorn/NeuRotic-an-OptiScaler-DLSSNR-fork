#pragma once
#include <Windows.h>
#include <filesystem>
#include <optional>
#include <queue>
#include <chrono>
#include <algorithm>
#include <vector>

namespace Neurotic::Runtime
{
inline bool PlainPath(const std::filesystem::path& path)
{
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}
template<class Accept>
std::optional<std::filesystem::path> FindDependency(const std::filesystem::path& root,
    const std::filesystem::path& name, Accept accept)
{
    if (!PlainPath(root) || name.has_parent_path()) return {};
    // Refuse linked ancestors as well as linked descendants. A selected search
    // root cannot silently redirect this traversal outside its namespace.
    for (auto parent = root.parent_path(); !parent.empty() && parent != parent.parent_path(); parent = parent.parent_path())
        if (!PlainPath(parent)) return {};
    std::queue<std::pair<std::filesystem::path, unsigned>> pending;
    pending.emplace(root, 0);
    unsigned visited = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (!pending.empty() && visited < 8192 && std::chrono::steady_clock::now() < deadline)
    {
        auto [dir, depth] = std::move(pending.front()); pending.pop();
        std::error_code ec;
        std::filesystem::directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec), end;
        std::vector<std::filesystem::directory_entry> entries;
        for (; !ec && it != end && visited++ < 8192; it.increment(ec)) entries.push_back(*it);
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.path() < b.path(); });
        for (const auto& entry : entries)
        {
            if (!PlainPath(entry.path())) continue;
            if (entry.is_regular_file(ec) && !ec && entry.path().filename() == name && accept(entry.path()))
                return entry.path();
            ec.clear();
            if (depth < 8 && entry.is_directory(ec) && !ec) pending.emplace(entry.path(), depth + 1);
        }
    }
    return {};
}
}
