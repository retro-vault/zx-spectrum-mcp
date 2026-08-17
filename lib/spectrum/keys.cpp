//
// key names, aliases and character to keystroke translation.
//
// the tables here are the only place that knows how a Spectrum key is
// spelled. keeping the aliases in one array means a new spelling costs
// one line and cannot drift out of step with the parser.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "spectrum/keys.h"

#include <array>
#include <cctype>

namespace spectrum {

namespace {

struct key_entry {
    key k;
    const char *name;
};

//
// canonical names, in matrix order. the order is also the order the
// keys are reported in, so it stays predictable for a reader.
//
constexpr key_entry key_table[] = {
    {key::caps_shift, "CAPS_SHIFT"},
    {key::z, "Z"},
    {key::x, "X"},
    {key::c, "C"},
    {key::v, "V"},

    {key::a, "A"},
    {key::s, "S"},
    {key::d, "D"},
    {key::f, "F"},
    {key::g, "G"},

    {key::q, "Q"},
    {key::w, "W"},
    {key::e, "E"},
    {key::r, "R"},
    {key::t, "T"},

    {key::num_1, "1"},
    {key::num_2, "2"},
    {key::num_3, "3"},
    {key::num_4, "4"},
    {key::num_5, "5"},

    {key::num_0, "0"},
    {key::num_9, "9"},
    {key::num_8, "8"},
    {key::num_7, "7"},
    {key::num_6, "6"},

    {key::p, "P"},
    {key::o, "O"},
    {key::i, "I"},
    {key::u, "U"},
    {key::y, "Y"},

    {key::enter, "ENTER"},
    {key::l, "L"},
    {key::k, "K"},
    {key::j, "J"},
    {key::h, "H"},

    {key::space, "SPACE"},
    {key::symbol_shift, "SYMBOL_SHIFT"},
    {key::m, "M"},
    {key::n, "N"},
    {key::b, "B"},
};

struct alias_entry {
    const char *alias;
    key k;
};

//
// alternative spellings. these are compared after normalisation, so
// they must already be upper case with no separators.
//
constexpr alias_entry alias_table[] = {
    {"CS", key::caps_shift},
    {"SHIFT", key::caps_shift},
    {"CAPS", key::caps_shift},
    {"SS", key::symbol_shift},
    {"SYM", key::symbol_shift},
    {"SYMSHIFT", key::symbol_shift},
    {"RETURN", key::enter},
    {"CR", key::enter},
    {"NEWLINE", key::enter},
    {"BREAK", key::space},
};

//
// strip separators and upper case, so that the many ways of writing
// "caps shift" all collapse onto one spelling.
//
std::string normalise(std::string_view name)
{
    std::string out;
    out.reserve(name.size());
    for (const char c : name) {
        if (c == ' ' || c == '_' || c == '-')
            continue;
        out.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
}

//
// letter keys indexed by 'a'..'z'.
//
constexpr key letter_keys[26] = {
    key::a, key::b, key::c, key::d, key::e, key::f, key::g,
    key::h, key::i, key::j, key::k, key::l, key::m, key::n,
    key::o, key::p, key::q, key::r, key::s, key::t, key::u,
    key::v, key::w, key::x, key::y, key::z,
};

//
// digit keys indexed by '0'..'9'.
//
constexpr key digit_keys[10] = {
    key::num_0, key::num_1, key::num_2, key::num_3, key::num_4,
    key::num_5, key::num_6, key::num_7, key::num_8, key::num_9,
};

struct symbol_entry {
    char c;
    key k;
};

//
// punctuation reachable with symbol shift held down.
//
constexpr symbol_entry symbol_table[] = {
    {'!', key::num_1}, {'@', key::num_2}, {'#', key::num_3},
    {'$', key::num_4}, {'%', key::num_5}, {'&', key::num_6},
    {'\'', key::num_7}, {'(', key::num_8}, {')', key::num_9},
    {'_', key::num_0},

    {'<', key::r}, {'>', key::t}, {';', key::o}, {'"', key::p},
    {'-', key::j}, {'+', key::k}, {'=', key::l}, {'.', key::m},
    {',', key::n}, {'/', key::v}, {'?', key::c}, {':', key::z},
    {'*', key::b}, {'^', key::h},
};

} // namespace

const char *key_name(key k)
{
    for (const key_entry &entry : key_table) {
        if (entry.k == k)
            return entry.name;
    }
    return "?";
}

std::optional<key> key_from_name(std::string_view name)
{
    const std::string wanted = normalise(name);
    if (wanted.empty())
        return std::nullopt;

    // the canonical names are spelled for a reader, so "CAPS_SHIFT"
    // carries an underscore that normalisation strips out of the input.
    // both sides have to go through the same treatment or the canonical
    // spelling would be the one name that never matched.
    for (const key_entry &entry : key_table) {
        if (wanted == normalise(entry.name))
            return entry.k;
    }
    for (const alias_entry &entry : alias_table) {
        if (wanted == normalise(entry.alias))
            return entry.k;
    }
    return std::nullopt;
}

std::span<const key> all_keys()
{
    static const std::array<key, std::size(key_table)> keys = [] {
        std::array<key, std::size(key_table)> result{};
        for (std::size_t i = 0; i < std::size(key_table); ++i)
            result[i] = key_table[i].k;
        return result;
    }();
    return keys;
}

std::optional<key_chord> chord_for_character(char c)
{
    if (c >= 'a' && c <= 'z')
        return key_chord{letter_keys[c - 'a'], std::nullopt};

    if (c >= 'A' && c <= 'Z')
        return key_chord{letter_keys[c - 'A'], key::caps_shift};

    if (c >= '0' && c <= '9')
        return key_chord{digit_keys[c - '0'], std::nullopt};

    if (c == ' ')
        return key_chord{key::space, std::nullopt};

    // both line endings mean the same thing to a Spectrum.
    if (c == '\n' || c == '\r')
        return key_chord{key::enter, std::nullopt};

    for (const symbol_entry &entry : symbol_table) {
        if (entry.c == c)
            return key_chord{entry.k, key::symbol_shift};
    }

    return std::nullopt;
}

std::string key_name_list()
{
    std::string out;
    for (const key_entry &entry : key_table) {
        if (!out.empty())
            out += ", ";
        out += entry.name;
    }
    return out;
}

} // namespace spectrum
