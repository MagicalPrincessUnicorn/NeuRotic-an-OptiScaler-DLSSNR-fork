#include "Localization.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <deque>
#include <regex>
#include <unordered_map>
#include <vector>

namespace Neurotic
{
namespace
{
struct CatalogEntry
{
    const char* source;
    const char* translated[LanguageCount - 1];
};
#include "locales/catalog.inc"

const std::regex Format(R"(%(?:[-+#0]*\d*(?:\.\d+)?(?:hh|ll|[hljztL]|I64)?[diuoxXfFeEgGaAcsp]|%)|\{(?::[^{}]*)?\})");

std::string Normalize(std::string_view value)
{
    std::string result;
    bool space = false;
    for (unsigned char c : value)
    {
        if (std::isspace(c))
        {
            space = !result.empty();
            continue;
        }
        if (space)
            result += ' ';
        result += static_cast<char>(c);
        space = false;
    }
    return result;
}

std::string Escape(std::string_view text)
{
    std::string result;
    for (char c : text)
    {
        if (std::string_view(R"(\.^$|()[]{}*+?)").find(c) != std::string_view::npos)
            result += '\\';
        result += c;
    }
    return result;
}

struct Pattern
{
    std::string prefix;
    std::regex match;
    const CatalogEntry* entry;
};

struct CatalogIndex
{
    std::unordered_map<std::string, const CatalogEntry*> exact;
    std::vector<Pattern> patterns;
    CatalogIndex()
    {
        for (const auto& entry : Catalog)
        {
            exact.emplace(Normalize(entry.source), &entry);
            const std::string source = Normalize(entry.source);
            std::sregex_iterator it(source.begin(), source.end(), Format), end;
            if (it == end)
                continue;
            const std::string prefix = source.substr(0, it->position());
            std::string expression = "^";
            size_t previous = 0, literalSize = 0;
            for (; it != end; ++it)
            {
                const size_t pos = it->position();
                expression += Escape(source.substr(previous, pos - previous));
                literalSize += pos - previous;
                const auto token = it->str();
                if (token == "%%")
                    expression += '%';
                else
                {
                    const char type = token.back();
                    expression += (type == 's' || type == '}') ? "(.*?)" : "([ +0-9a-fA-FxX.eE-]+)";
                }
                previous = pos + it->length();
            }
            expression += Escape(source.substr(previous)) + '$';
            literalSize += source.size() - previous;
            // Bare values such as %s/%d are not translation templates.
            if (literalSize >= 4)
                patterns.push_back({ prefix, std::regex(expression, std::regex::optimize), &entry });
        }
        // Specific messages win over broad suffix/prefix templates.
        std::stable_sort(patterns.begin(), patterns.end(),
                         [](const auto& a, const auto& b) { return a.prefix.size() > b.prefix.size(); });
    }
};

const CatalogIndex& Index()
{
    static const CatalogIndex index;
    return index;
}

struct Context
{
    int language = 0;
    int depth = 0;
    std::unordered_map<std::string, std::string> cache;
    std::deque<std::string> order;
};
thread_local Context context;

std::string TranslateNormalized(const std::string& source, int recursion)
{
    const auto& index = Index();
    if (auto it = index.exact.find(source); it != index.exact.end())
        return it->second->translated[context.language - 1];
    if (recursion > 1)
        return source;
    for (const auto& pattern : index.patterns)
    {
        if (!source.starts_with(pattern.prefix))
            continue;
        std::smatch captures;
        if (!std::regex_match(source, captures, pattern.match))
            continue;
        const std::string target = pattern.entry->translated[context.language - 1];
        std::string result;
        size_t previous = 0, argument = 1;
        for (std::sregex_iterator it(target.begin(), target.end(), Format), end; it != end; ++it)
        {
            result += target.substr(previous, it->position() - previous);
            if (it->str() == "%%")
                result += '%';
            else if (argument < captures.size())
            {
                const std::string value = captures[argument++].str();
                const auto normalized = Normalize(value);
                const size_t first = value.find_first_not_of(" \r\n\t");
                const size_t last = value.find_last_not_of(" \r\n\t");
                if (first == std::string::npos)
                    result += value;
                else
                    result += value.substr(0, first) + TranslateNormalized(normalized, recursion + 1) +
                              value.substr(last + 1);
            }
            previous = it->position() + it->length();
        }
        result += target.substr(previous);
        return result;
    }
    return source;
}
} // namespace

int LanguageIndex(std::string_view code)
{
    std::string lower(code);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "pt-br")
        return 4;
    for (int i = 0; i < LanguageCount; ++i)
        if (lower == Languages[i].code)
            return i;
    return 0;
}

void SetLanguage(std::string_view code)
{
    const int language = LanguageIndex(code);
    if (context.language != language)
    {
        context.language = language;
        context.cache.clear();
        context.order.clear();
    }
}

std::string Translate(std::string_view source)
{
    if (context.language == 0 || source.empty())
        return std::string(source);
    const auto key = Normalize(source);
    auto found = context.cache.find(key);
    if (found == context.cache.end())
    {
        const auto result = TranslateNormalized(key, 0);
        if (context.cache.size() >= 2048)
        {
            context.cache.erase(context.order.front());
            context.order.pop_front();
        }
        context.order.push_back(key);
        found = context.cache.emplace(key, result).first;
    }
    // Unknown/user-provided values retain their exact bytes and whitespace.
    if (found->second == key)
        return std::string(source);
    const auto first = source.find_first_not_of(" \r\n\t");
    const auto last = source.find_last_not_of(" \r\n\t");
    if (first == std::string_view::npos)
        return std::string(source);
    return std::string(source.substr(0, first)) + found->second + std::string(source.substr(last + 1));
}

LocalizedRange::LocalizedRange(const char*& begin, const char*& end)
{
    if (!begin || context.language == 0 || context.depth != 0)
        return;
    text = Translate(end ? std::string_view(begin, end - begin) : std::string_view(begin));
    begin = text.c_str();
    end = begin + text.size();
    ++context.depth;
    scoped = true;
}

LocalizedRange::~LocalizedRange()
{
    if (scoped)
        --context.depth;
}
} // namespace Neurotic
