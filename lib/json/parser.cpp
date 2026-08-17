//
// implementation of the strict recursive descent json parser.
//
// the scanner walks the input once, tracking a byte cursor so every
// diagnostic can name the exact offset that failed. failures propagate
// by setting a sticky error on the scanner rather than by throwing, so
// a malformed message costs a return trip rather than a stack unwind.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "json/parser.h"

#include <cctype>
#include <charconv>
#include <cstdlib>
#include <string>

namespace json {

namespace {

//
// unicode replacement character, substituted for malformed escapes so
// that a bad \u sequence degrades instead of killing the whole message.
//
constexpr char32_t replacement_char = 0xfffd;

//
// single pass scanner over the document text.
//
class scanner {
public:
    explicit scanner(std::string_view text) : text_(text) {}

    //
    // parse one complete document and verify nothing follows it.
    //
    parse_result run();

private:
    std::string_view text_;
    std::size_t pos_ = 0;
    int depth_ = 0;
    bool failed_ = false;
    std::string error_;
    std::size_t error_pos_ = 0;

    bool at_end() const { return pos_ >= text_.size(); }
    char peek() const { return at_end() ? '\0' : text_[pos_]; }

    //
    // record the first failure only; later errors are consequences.
    //
    void fail(std::string message)
    {
        if (!failed_) {
            failed_ = true;
            error_ = std::move(message);
            error_pos_ = pos_;
        }
    }

    void skip_whitespace();
    bool expect(char c);

    value parse_value();
    value parse_object();
    value parse_array();
    value parse_string_value();
    value parse_number();
    value parse_literal();

    bool parse_string_raw(std::string &out);
    bool parse_hex4(char32_t &out);
    static void append_utf8(std::string &out, char32_t cp);
};

void scanner::skip_whitespace()
{
    while (!at_end()) {
        const char c = text_[pos_];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            ++pos_;
        else
            break;
    }
}

bool scanner::expect(char c)
{
    if (peek() != c) {
        fail(std::string("expected '") + c + "'");
        return false;
    }
    ++pos_;
    return true;
}

void scanner::append_utf8(std::string &out, char32_t cp)
{
    if (cp <= 0x7f) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}

bool scanner::parse_hex4(char32_t &out)
{
    if (pos_ + 4 > text_.size()) {
        fail("truncated \\u escape");
        return false;
    }

    char32_t acc = 0;
    for (int i = 0; i < 4; ++i) {
        const char c = text_[pos_ + static_cast<std::size_t>(i)];
        acc <<= 4;
        if (c >= '0' && c <= '9')
            acc |= static_cast<char32_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            acc |= static_cast<char32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            acc |= static_cast<char32_t>(c - 'A' + 10);
        else {
            fail("bad hex digit in \\u escape");
            return false;
        }
    }
    pos_ += 4;
    out = acc;
    return true;
}

bool scanner::parse_string_raw(std::string &out)
{
    if (!expect('"'))
        return false;

    while (true) {
        if (at_end()) {
            fail("unterminated string");
            return false;
        }

        const char c = text_[pos_];

        if (c == '"') {
            ++pos_;
            return true;
        }

        // rfc 8259 forbids raw control characters inside a string.
        if (static_cast<unsigned char>(c) < 0x20) {
            fail("raw control character in string");
            return false;
        }

        if (c != '\\') {
            out.push_back(c);
            ++pos_;
            continue;
        }

        ++pos_;
        if (at_end()) {
            fail("unterminated escape");
            return false;
        }

        const char esc = text_[pos_++];
        switch (esc) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
            char32_t cp = 0;
            if (!parse_hex4(cp))
                return false;

            // a high surrogate must be followed by its low partner;
            // anything else is emitted as the replacement character.
            if (cp >= 0xd800 && cp <= 0xdbff) {
                if (pos_ + 1 < text_.size() && text_[pos_] == '\\' &&
                    text_[pos_ + 1] == 'u') {
                    const std::size_t save = pos_;
                    pos_ += 2;
                    char32_t low = 0;
                    if (!parse_hex4(low))
                        return false;
                    if (low >= 0xdc00 && low <= 0xdfff) {
                        cp = 0x10000 + ((cp - 0xd800) << 10) +
                             (low - 0xdc00);
                    } else {
                        pos_ = save;
                        cp = replacement_char;
                    }
                } else {
                    cp = replacement_char;
                }
            } else if (cp >= 0xdc00 && cp <= 0xdfff) {
                cp = replacement_char;
            }
            append_utf8(out, cp);
            break;
        }
        default:
            fail("unknown escape sequence");
            return false;
        }
    }
}

value scanner::parse_string_value()
{
    std::string text;
    if (!parse_string_raw(text))
        return value();
    return value(std::move(text));
}

value scanner::parse_number()
{
    const std::size_t start = pos_;

    if (peek() == '-')
        ++pos_;

    // integer part: either a single 0 or a non-zero leading digit run.
    if (peek() == '0') {
        ++pos_;
    } else if (std::isdigit(static_cast<unsigned char>(peek()))) {
        while (std::isdigit(static_cast<unsigned char>(peek())))
            ++pos_;
    } else {
        fail("expected a number");
        return value();
    }

    bool is_real = false;

    if (peek() == '.') {
        is_real = true;
        ++pos_;
        if (!std::isdigit(static_cast<unsigned char>(peek()))) {
            fail("expected digits after decimal point");
            return value();
        }
        while (std::isdigit(static_cast<unsigned char>(peek())))
            ++pos_;
    }

    if (peek() == 'e' || peek() == 'E') {
        is_real = true;
        ++pos_;
        if (peek() == '+' || peek() == '-')
            ++pos_;
        if (!std::isdigit(static_cast<unsigned char>(peek()))) {
            fail("expected digits in exponent");
            return value();
        }
        while (std::isdigit(static_cast<unsigned char>(peek())))
            ++pos_;
    }

    const std::string_view token = text_.substr(start, pos_ - start);

    // keep whole numbers exact when they fit; fall back to double for
    // fractions, exponents and anything that overflows int64.
    if (!is_real) {
        std::int64_t as_int = 0;
        const char *first = token.data();
        const char *last = first + token.size();
        const auto res = std::from_chars(first, last, as_int);
        if (res.ec == std::errc() && res.ptr == last)
            return value(as_int);
    }

    // std::from_chars for double is not available everywhere at the
    // required precision, so parse through a nul terminated copy.
    const std::string buffer(token);
    char *end = nullptr;
    const double as_double = std::strtod(buffer.c_str(), &end);
    if (end != buffer.c_str() + buffer.size()) {
        fail("malformed number");
        return value();
    }
    return value(as_double);
}

value scanner::parse_literal()
{
    const auto starts_with = [this](std::string_view word) {
        return text_.compare(pos_, word.size(), word) == 0;
    };

    if (starts_with("true")) {
        pos_ += 4;
        return value(true);
    }
    if (starts_with("false")) {
        pos_ += 5;
        return value(false);
    }
    if (starts_with("null")) {
        pos_ += 4;
        return value();
    }

    fail("unexpected token");
    return value();
}

value scanner::parse_array()
{
    if (!expect('['))
        return value();

    value result = value::make_array();

    skip_whitespace();
    if (peek() == ']') {
        ++pos_;
        return result;
    }

    while (true) {
        skip_whitespace();
        value element = parse_value();
        if (failed_)
            return value();
        result.push_back(std::move(element));

        skip_whitespace();
        if (peek() == ',') {
            ++pos_;
            continue;
        }
        if (peek() == ']') {
            ++pos_;
            return result;
        }
        fail("expected ',' or ']' in array");
        return value();
    }
}

value scanner::parse_object()
{
    if (!expect('{'))
        return value();

    value result = value::make_object();

    skip_whitespace();
    if (peek() == '}') {
        ++pos_;
        return result;
    }

    while (true) {
        skip_whitespace();

        std::string key;
        if (!parse_string_raw(key))
            return value();

        skip_whitespace();
        if (!expect(':'))
            return value();

        skip_whitespace();
        value element = parse_value();
        if (failed_)
            return value();
        result.set(std::move(key), std::move(element));

        skip_whitespace();
        if (peek() == ',') {
            ++pos_;
            continue;
        }
        if (peek() == '}') {
            ++pos_;
            return result;
        }
        fail("expected ',' or '}' in object");
        return value();
    }
}

value scanner::parse_value()
{
    if (failed_)
        return value();

    if (depth_ >= max_parse_depth) {
        fail("maximum nesting depth exceeded");
        return value();
    }

    if (at_end()) {
        fail("unexpected end of input");
        return value();
    }

    const char c = peek();

    if (c == '{' || c == '[') {
        ++depth_;
        value result = (c == '{') ? parse_object() : parse_array();
        --depth_;
        return result;
    }
    if (c == '"')
        return parse_string_value();
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c)))
        return parse_number();
    return parse_literal();
}

parse_result scanner::run()
{
    parse_result result;

    skip_whitespace();
    value doc = parse_value();

    if (!failed_) {
        skip_whitespace();
        if (!at_end())
            fail("trailing data after document");
    }

    if (failed_) {
        result.ok = false;
        result.error = error_;
        result.offset = error_pos_;
        return result;
    }

    result.ok = true;
    result.document = std::move(doc);
    result.offset = pos_;
    return result;
}

} // namespace

parse_result parse(std::string_view text)
{
    scanner s(text);
    return s.run();
}

} // namespace json
