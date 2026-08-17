//
// implementation of the json document node.
//
// the only real work here is deep copying: the two container
// alternatives live behind unique_ptr, so the compiler generated copy
// operations would not compile. everything else is thin accessor code
// that prefers returning a shared empty singleton over throwing, which
// keeps protocol field extraction free of try/catch noise.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "json/value.h"

#include <cmath>
#include <limits>
#include <type_traits>

namespace json {

namespace {

//
// shared immutable results handed out when a lookup misses. returning a
// reference to these keeps the accessors total, so callers can chain
// lookups without checking each step.
//
const value &null_value()
{
    static const value instance;
    return instance;
}

const array &empty_array()
{
    static const array instance;
    return instance;
}

const object &empty_object()
{
    static const object instance;
    return instance;
}

//
// deep copy the variant payload, cloning anything held by pointer.
//
// the variant itself is not copyable because two of its alternatives
// are unique_ptr, so every alternative has to be rebuilt by hand. the
// template parameter avoids naming value::storage, which is private.
//
template <typename storage_type>
storage_type clone_storage(const storage_type &src)
{
    return std::visit(
        [](const auto &held) -> storage_type {
            using held_type = std::decay_t<decltype(held)>;

            if constexpr (std::is_same_v<held_type,
                                         std::unique_ptr<array>>) {
                return held ? std::make_unique<array>(*held)
                            : std::make_unique<array>();
            } else if constexpr (std::is_same_v<held_type,
                                                std::unique_ptr<object>>) {
                return held ? std::make_unique<object>(*held)
                            : std::make_unique<object>();
            } else {
                return held;
            }
        },
        src);
}

} // namespace

value::value() noexcept : data_(nullptr) {}

value::value(std::nullptr_t) noexcept : data_(nullptr) {}

value::value(bool v) noexcept : data_(v) {}

value::value(int v) noexcept : data_(static_cast<std::int64_t>(v)) {}

value::value(unsigned v) noexcept : data_(static_cast<std::int64_t>(v)) {}

value::value(long v) noexcept : data_(static_cast<std::int64_t>(v)) {}

value::value(unsigned long v) noexcept
    : data_(static_cast<std::int64_t>(v))
{
}

value::value(long long v) noexcept : data_(static_cast<std::int64_t>(v)) {}

value::value(unsigned long long v) noexcept
    : data_(static_cast<std::int64_t>(v))
{
}

value::value(double v) noexcept : data_(v) {}

value::value(const char *v) : data_(std::string(v ? v : "")) {}

value::value(std::string_view v) : data_(std::string(v)) {}

value::value(std::string v) : data_(std::move(v)) {}

value value::make_array()
{
    value result;
    result.data_ = std::make_unique<array>();
    return result;
}

value value::make_object()
{
    value result;
    result.data_ = std::make_unique<object>();
    return result;
}

value value::make_array(std::initializer_list<value> items)
{
    value result = make_array();
    auto &vec = *std::get<std::unique_ptr<array>>(result.data_);
    vec.reserve(items.size());
    for (const value &item : items)
        vec.push_back(item);
    return result;
}

value::value(const value &other) : data_(clone_storage(other.data_)) {}

value::value(value &&other) noexcept : data_(std::move(other.data_))
{
    other.data_ = nullptr;
}

value &value::operator=(const value &other)
{
    if (this != &other)
        data_ = clone_storage(other.data_);
    return *this;
}

value &value::operator=(value &&other) noexcept
{
    if (this != &other) {
        data_ = std::move(other.data_);
        other.data_ = nullptr;
    }
    return *this;
}

value::~value() = default;

kind value::type() const noexcept
{
    switch (data_.index()) {
    case 0: return kind::null;
    case 1: return kind::boolean;
    case 2: return kind::integer;
    case 3: return kind::real;
    case 4: return kind::string;
    case 5: return kind::array;
    default: return kind::object;
    }
}

bool value::is_null() const noexcept { return data_.index() == 0; }

bool value::is_bool() const noexcept { return data_.index() == 1; }

bool value::is_number() const noexcept
{
    return data_.index() == 2 || data_.index() == 3;
}

bool value::is_integer() const noexcept { return data_.index() == 2; }

bool value::is_string() const noexcept { return data_.index() == 4; }

bool value::is_array() const noexcept { return data_.index() == 5; }

bool value::is_object() const noexcept { return data_.index() == 6; }

bool value::as_bool(bool fallback) const noexcept
{
    if (const auto *p = std::get_if<bool>(&data_))
        return *p;
    return fallback;
}

std::int64_t value::as_int(std::int64_t fallback) const noexcept
{
    if (const auto *p = std::get_if<std::int64_t>(&data_))
        return *p;

    // accept a real number only when it is exactly integral and inside
    // the range an int64_t can represent. this lets a client send 32768
    // as 3.2768e4 without silently truncating 0.5 to 0.
    if (const auto *p = std::get_if<double>(&data_)) {
        const double d = *p;
        if (std::isfinite(d) && d == std::floor(d) &&
            d >= -9223372036854775808.0 && d < 9223372036854775808.0)
            return static_cast<std::int64_t>(d);
    }
    return fallback;
}

double value::as_double(double fallback) const noexcept
{
    if (const auto *p = std::get_if<double>(&data_))
        return *p;
    if (const auto *p = std::get_if<std::int64_t>(&data_))
        return static_cast<double>(*p);
    return fallback;
}

std::string value::as_string(std::string_view fallback) const
{
    if (const auto *p = std::get_if<std::string>(&data_))
        return *p;
    return std::string(fallback);
}

const value *value::find(std::string_view key) const noexcept
{
    const auto *p = std::get_if<std::unique_ptr<object>>(&data_);
    if (p == nullptr)
        return nullptr;
    for (const member &m : **p) {
        if (m.first == key)
            return &m.second;
    }
    return nullptr;
}

bool value::contains(std::string_view key) const noexcept
{
    return find(key) != nullptr;
}

const value &value::operator[](std::string_view key) const noexcept
{
    const value *found = find(key);
    return found != nullptr ? *found : null_value();
}

void value::set(std::string key, value v)
{
    if (!is_object())
        data_ = std::make_unique<object>();

    auto &members = *std::get<std::unique_ptr<object>>(data_);
    for (member &m : members) {
        if (m.first == key) {
            m.second = std::move(v);
            return;
        }
    }
    members.emplace_back(std::move(key), std::move(v));
}

void value::push_back(value v)
{
    if (!is_array())
        data_ = std::make_unique<array>();

    std::get<std::unique_ptr<array>>(data_)->push_back(std::move(v));
}

std::size_t value::size() const noexcept
{
    if (const auto *p = std::get_if<std::unique_ptr<array>>(&data_))
        return (*p)->size();
    if (const auto *p = std::get_if<std::unique_ptr<object>>(&data_))
        return (*p)->size();
    return 0;
}

bool value::empty() const noexcept { return size() == 0; }

const value &value::at(std::size_t index) const noexcept
{
    const auto *p = std::get_if<std::unique_ptr<array>>(&data_);
    if (p == nullptr || index >= (*p)->size())
        return null_value();
    return (**p)[index];
}

const array &value::elements() const noexcept
{
    if (const auto *p = std::get_if<std::unique_ptr<array>>(&data_))
        return **p;
    return empty_array();
}

const object &value::members() const noexcept
{
    if (const auto *p = std::get_if<std::unique_ptr<object>>(&data_))
        return **p;
    return empty_object();
}

} // namespace json
