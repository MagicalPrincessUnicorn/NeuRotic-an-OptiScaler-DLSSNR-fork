#pragma once

#include "CustomOptional.h"
#include <mutex>
#include <atomic>
#include <cstdint>

// Only NR opts into this domain. Transactions must cover config copies/updates only;
// never retain one across GPU, scanner, UI, or other external calls.
struct NrConfigSynchronization
{
    // Reload invalidates UI previews even when the loaded values happen to be identical.
    inline static std::atomic<uint64_t> profileGeneration { 0 };
    static uint64_t ProfileGeneration() { return profileGeneration.load(); }
    static void InvalidateProfileEdits() { ++profileGeneration; }
    static std::recursive_mutex& Mutex()
    {
        // Config and pinned hooks have no coordinated teardown. Retain this mutex for process
        // lifetime so late callbacks/static destructors cannot access a destroyed lock.
        static std::recursive_mutex* const mutex = new std::recursive_mutex;
        return *mutex;
    }
    using Guard = std::lock_guard<std::recursive_mutex>;

    // Nonmovable capability: its lifetime proves the NR mutex is held by this thread.
    // Use on the stack only, and never pass it to another thread.
    class Transaction
    {
        Guard _lock { Mutex() };
      public:
        Transaction() = default;
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;
    };
};

// Composition deliberately hides std::optional's reference-returning interface.
// Every read copies while locked; no pointer, reference, or mutable base escapes.
template <class T, HasDefaultValue defaultState = WithDefault> class NrOptional
{
    CustomOptional<T, defaultState> _value;

  public:
    NrOptional(T defaultValue) requires(defaultState != NoDefault) : _value(std::move(defaultValue)) {}
    NrOptional() requires(defaultState == NoDefault) = default;

    NrOptional(const NrOptional& other) : _value(CopyStorage(other)) {}
    NrOptional& operator=(const NrOptional& other)
    {
        NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
        _value = other._value;
        return *this;
    }

    NrOptional& operator=(const T& value)
    {
        NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
        _value = value;
        return *this;
    }
    NrOptional& operator=(T&& value)
    {
        NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
        _value = std::move(value);
        return *this;
    }
    NrOptional& operator=(const std::optional<T>& value)
    {
        NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
        _value = value;
        return *this;
    }
    NrOptional& operator=(std::optional<T>&& value)
    {
        NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
        _value = std::move(value);
        return *this;
    }
    NrOptional& operator=(const char* value) requires std::same_as<T, std::string>
    {
        NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
        _value = value;
        return *this;
    }
    void set_volatile_value(const T& value)
    {
        NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
        _value.set_volatile_value(value);
    }
    void set_from_config(const std::optional<T>& value)
    {
        NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
        _value.set_from_config(value);
    }
    void reset()
    {
        NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
        _value.reset();
    }
    bool has_value() const
    {
        NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
        return _value.has_value();
    }
    explicit operator bool() const { return has_value(); }
    T value() const
    {
        NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
        return _value.value();
    }
    T operator*() const { return value(); }
    template <class U> T value_or(U&& other) const
    {
        NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
        return _value.value_or(std::forward<U>(other));
    }
    T value_or_default() const requires(defaultState != NoDefault)
    {
        NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
        return _value.value_or_default();
    }
    // Engaged value, without serialization/default suppression (e.g. enum serialization).
    std::optional<T> snapshot() const
    {
        NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
        return static_cast<const std::optional<T>&>(_value);
    }
    CustomOptional<T, defaultState> CopyForSnapshot(const NrConfigSynchronization::Transaction&) const
    {
        return _value;
    }
    std::optional<T> value_for_config()
    {
        NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
        return _value.value_for_config();
    }
    T value_for_config_or(T other)
    {
        NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
        return _value.value_for_config_or(std::move(other));
    }

  private:
    static CustomOptional<T, defaultState> CopyStorage(const NrOptional& other)
    {
        NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
        return other._value;
    }
};
