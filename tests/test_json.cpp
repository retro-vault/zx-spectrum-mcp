//
// the JSON parser and serialiser.
//
// the parser is strict on purpose, so about half of this file is
// checking that malformed input is rejected rather than half accepted.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "json/parser.h"
#include "json/writer.h"
#include "test_support.h"

namespace {

void test_scalars()
{
    test::section("scalar values");

    const auto parsed = json::parse(R"({"a":1,"b":-2.5,"c":"x","d":true,
                                        "e":null})");
    test::check(parsed.ok, "an object of scalars parses");

    const json::value &doc = parsed.document;
    test::check(doc["a"].is_integer(), "a whole number stays an integer");
    test::check_eq(doc["a"].as_int(), 1, "and reads back exactly");
    test::check(doc["b"].is_number(), "a fraction is a number");
    test::check(!doc["b"].is_integer(), "but not an integer");
    test::check_eq_str(doc["c"].as_string(), "x", "a string reads back");
    test::check(doc["d"].as_bool(), "a boolean reads back");
    test::check(doc["e"].is_null(), "null is null");
}

void test_large_integers()
{
    test::section("integer precision");

    // T-state counters outgrow what a double can hold exactly, so
    // integers must never go through one.
    const auto parsed = json::parse(R"({"t":9007199254740993})");
    test::check(parsed.ok, "a large integer parses");
    test::check_eq(parsed.document["t"].as_int(), 9007199254740993LL,
                   "and survives without loss");

    json::value doc = json::value::make_object();
    doc.set("t", json::value(9007199254740993LL));
    test::check_contains(json::write(doc), "9007199254740993",
                         "and is written back in full");
}

void test_escapes_and_unicode()
{
    test::section("string escapes");

    const auto parsed =
        json::parse(R"({"s":"a\"b\\c\nd\te\u0041\ud83d\ude00"})");
    test::check(parsed.ok, "escapes parse");

    const std::string s = parsed.document["s"].as_string();
    test::check_contains(s, "a\"b\\c", "quote and backslash");
    test::check_contains(s, "\n", "newline");
    test::check_contains(s, "A", "a \\u escape");
    test::check_contains(s, "\xf0\x9f\x98\x80",
                         "a surrogate pair becomes one UTF-8 character");

    // a round trip must be stable, and must never emit a raw control
    // character into a line delimited stream.
    const std::string written = json::write(parsed.document);
    test::check(written.find('\n') == std::string::npos,
                "the serialised form contains no raw newline");

    const auto again = json::parse(written);
    test::check(again.ok, "the serialised form parses again");
    test::check_eq_str(again.document["s"].as_string(), s,
                       "and yields the same string");
}

void test_containers()
{
    test::section("arrays and objects");

    const auto parsed = json::parse(R"({"xs":[1,2,3],"o":{"k":"v"}})");
    test::check_eq(static_cast<long long>(parsed.document["xs"].size()), 3,
                   "an array reports its length");
    test::check_eq(parsed.document["xs"].at(1).as_int(), 2,
                   "and indexes correctly");
    test::check_eq_str(parsed.document["o"]["k"].as_string(), "v",
                       "a nested object reads back");

    // a missing key yields null rather than throwing, so chained
    // lookups on optional protocol fields are safe.
    test::check(parsed.document["nope"].is_null(), "a missing key is null");
    test::check_eq(parsed.document["nope"]["deep"].as_int(-1), -1,
                   "and chaining through it is safe");
    test::check_eq(parsed.document["xs"].at(99).as_int(-1), -1,
                   "an out of range index is safe");
}

void test_insertion_order()
{
    test::section("object member order");

    json::value doc = json::value::make_object();
    doc.set("z", json::value(1));
    doc.set("a", json::value(2));
    doc.set("m", json::value(3));

    test::check_eq_str(json::write(doc), R"({"z":1,"a":2,"m":3})",
                       "members are written in insertion order");

    doc.set("a", json::value(9));
    test::check_eq_str(json::write(doc), R"({"z":1,"a":9,"m":3})",
                       "replacing a member keeps its position");
}

void test_rejections()
{
    test::section("malformed input is rejected");

    const char *bad[] = {
        "",           "{",          "}",         "[1,]",
        "{\"a\":}",   "{\"a\" 1}",  "1 2",       "\"\\x\"",
        "01",         "{'a':1}",    "[1,2",      "tru",
        "\"unterminated", "{\"a\":1,}", "+1",    ".5",
        "1.",         "1e",         "--1",
    };

    for (const char *text : bad) {
        const auto parsed = json::parse(text);
        test::check(!parsed.ok,
                    std::string("rejects: ") +
                        (text[0] ? text : "<empty>"));
    }

    // a raw control character inside a string is illegal.
    const std::string with_control = "{\"a\":\"x\ny\"}";
    test::check(!json::parse(with_control).ok,
                "rejects a raw newline inside a string");
}

void test_depth_limit()
{
    test::section("nesting depth");

    std::string deep;
    for (int i = 0; i < json::max_parse_depth + 10; ++i)
        deep += '[';

    const auto parsed = json::parse(deep);
    test::check(!parsed.ok, "a document nested too deeply is rejected");
    test::check_contains(parsed.error, "depth",
                         "and says why");
}

} // namespace

int main()
{
    test_scalars();
    test_large_integers();
    test_escapes_and_unicode();
    test_containers();
    test_insertion_order();
    test_rejections();
    test_depth_limit();
    return test::summary("json");
}
