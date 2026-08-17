//
// json document node used for all mcp protocol traffic.
//
// a value holds exactly one of the six json types. numbers are stored
// either as a signed 64 bit integer or as a double, kept apart on
// purpose: addresses, register values and t-state counters must survive
// a parse and serialise round trip bit exact, which a double cannot
// promise once counters grow past 2^53.
//
// object members keep insertion order rather than sorting by key. the
// wire format stays stable and diffable, which matters when protocol
// logs are read by a human or compared byte for byte in a test.
//
// the two container types are held behind unique_ptr. a json value is
// recursive and std::variant requires its alternatives to be complete
// types, so the indirection is what makes the variant legal rather than
// a performance choice.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef JSON_VALUE_H
#define JSON_VALUE_H

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace json {

class value;

// an ordered json array.
using array = std::vector<value>;

// one key/value pair of a json object.
using member = std::pair<std::string, value>;

// a json object, in insertion order.
using object = std::vector<member>;

//
// discriminator for the type currently held by a value.
//
enum class kind {
    null,
    boolean,
    integer,
    real,
    string,
    array,
    object
};

//
// a single json document node.
//
class value {
public:
    //
    // construct a null value.
    //
    value() noexcept;

    //
    // construct a null value from an explicit nullptr.
    //
    value(std::nullptr_t) noexcept;

    //
    // construct a json boolean.
    //
    value(bool v) noexcept;

    //
    // construct a json integer. the small integral overloads exist so
    // that value(0) is not ambiguous between bool and int64_t.
    //
    value(int v) noexcept;
    value(unsigned v) noexcept;
    value(long v) noexcept;
    value(unsigned long v) noexcept;
    value(long long v) noexcept;
    value(unsigned long long v) noexcept;

    //
    // construct a json real number.
    //
    value(double v) noexcept;

    //
    // construct a json string.
    //
    value(const char *v);
    value(std::string_view v);
    value(std::string v);

    //
    // construct an empty container of the requested type.
    //
    static value make_array();
    static value make_object();

    //
    // construct an array holding the supplied elements.
    //
    static value make_array(std::initializer_list<value> items);

    value(const value &other);
    value(value &&other) noexcept;
    value &operator=(const value &other);
    value &operator=(value &&other) noexcept;
    ~value();

    //
    // Returns:
    //      the json type currently held.
    //
    kind type() const noexcept;

    bool is_null() const noexcept;
    bool is_bool() const noexcept;

    //
    // Returns:
    //      true for both integer and real numbers.
    //
    bool is_number() const noexcept;

    //
    // Returns:
    //      true only when the number was stored as an exact integer.
    //
    bool is_integer() const noexcept;
    bool is_string() const noexcept;
    bool is_array() const noexcept;
    bool is_object() const noexcept;

    //
    // typed readers. each returns fallback when the value does not hold
    // a compatible type, so callers can parse optional protocol fields
    // without first testing the type.
    //
    // an integer converts to double and a whole valued double converts
    // to integer; anything else yields fallback.
    //
    bool as_bool(bool fallback = false) const noexcept;
    std::int64_t as_int(std::int64_t fallback = 0) const noexcept;
    double as_double(double fallback = 0.0) const noexcept;

    //
    // Returns:
    //      the string contents, or fallback when not a string.
    //
    std::string as_string(std::string_view fallback = {}) const;

    //
    // Look up an object member.
    //
    // Returns:
    //      pointer to the member value, or nullptr when this is not an
    //      object or the key is absent.
    //
    const value *find(std::string_view key) const noexcept;

    //
    // Returns:
    //      true when this is an object holding key.
    //
    bool contains(std::string_view key) const noexcept;

    //
    // Look up an object member for reading.
    //
    // Returns:
    //      the member, or a shared immutable null when absent. this
    //      allows chains such as msg["params"]["addr"].as_int().
    //
    const value &operator[](std::string_view key) const noexcept;

    //
    // Insert or replace an object member.
    //
    // Notes:
    //      converts this value into an empty object first when it is
    //      not already one.
    //
    void set(std::string key, value v);

    //
    // Append to an array.
    //
    // Notes:
    //      converts this value into an empty array first when it is not
    //      already one.
    //
    void push_back(value v);

    //
    // Returns:
    //      element count for arrays and objects, 0 otherwise.
    //
    std::size_t size() const noexcept;

    //
    // Returns:
    //      true for an empty array or object, or for any non-container.
    //
    bool empty() const noexcept;

    //
    // Read an array element.
    //
    // Returns:
    //      the element, or a shared immutable null when this is not an
    //      array or index is out of range.
    //
    const value &at(std::size_t index) const noexcept;

    //
    // Returns:
    //      the underlying array, or a shared empty array when this
    //      value is not an array.
    //
    const array &elements() const noexcept;

    //
    // Returns:
    //      the underlying member list, or a shared empty list when this
    //      value is not an object.
    //
    const object &members() const noexcept;

private:
    using storage = std::variant<std::nullptr_t,
                                 bool,
                                 std::int64_t,
                                 double,
                                 std::string,
                                 std::unique_ptr<array>,
                                 std::unique_ptr<object>>;

    storage data_;
};

} // namespace json

#endif // JSON_VALUE_H
