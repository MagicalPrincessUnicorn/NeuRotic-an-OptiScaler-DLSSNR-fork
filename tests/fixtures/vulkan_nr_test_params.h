#pragma once
#include <nvsdk_ngx_params.h>
#include <map>
#include <variant>
#include <string>
struct StrictParams final : NVSDK_NGX_Parameter
{
    using Value = std::variant<unsigned long long, float, double, unsigned int, int,
                               ID3D11Resource*, ID3D12Resource*, void*>;
    std::map<std::string, Value> values;
    std::map<std::string, size_t> writes;
    enum class Fault { None, Missing, WrongType, Mismatch } fault = Fault::None;
    std::string faultKey;
    template<class T> void Store(const char* key, T value)
    {
        values[key] = value;
        ++writes[key];
    }
    template<class T> NVSDK_NGX_Result Read(const char* key, T* out) const
    {
        if (key == faultKey && (fault == Fault::Missing || fault == Fault::WrongType))
            return NVSDK_NGX_Result_FAIL_UnsupportedParameter;
        const auto it = values.find(key);
        if (it == values.end() || !std::holds_alternative<T>(it->second))
            return NVSDK_NGX_Result_FAIL_UnsupportedParameter;
        *out = std::get<T>(it->second);
        if (key == faultKey && fault == Fault::Mismatch)
        {
            if constexpr (std::is_pointer_v<T>) *out = reinterpret_cast<T>(uintptr_t(0xDEAD));
            else *out += T(1);
        }
        return NVSDK_NGX_Result_Success;
    }
#define PARAM_TYPE(T) \
    void Set(const char* key, T value) override { Store(key, value); } \
    NVSDK_NGX_Result Get(const char* key, T* out) const override { return Read(key, out); }
    PARAM_TYPE(unsigned long long)
    PARAM_TYPE(float)
    PARAM_TYPE(double)
    PARAM_TYPE(unsigned int)
    PARAM_TYPE(int)
    PARAM_TYPE(ID3D11Resource*)
    PARAM_TYPE(ID3D12Resource*)
    PARAM_TYPE(void*)
#undef PARAM_TYPE
    void Reset() override { values.clear(); writes.clear(); }
};
