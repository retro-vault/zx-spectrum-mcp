//
// the key matrix, key naming, and typing text.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "spectrum/machine.h"
#include "test_support.h"

using namespace spectrum;

namespace {

void test_matrix_layout()
{
    test::section("half-row selection");

    keyboard keys;

    // a key that is down pulls its bit low in the half-row selected by
    // the address line that is low.
    keys.press(key::caps_shift);
    test::check_eq(keys.scan(0xfefe) & 0x01, 0,
                   "caps shift pulls bit 0 low in its half-row");
    test::check_eq(keys.scan(0xfdfe) & 0x01, 1,
                   "and leaves other half-rows alone");

    keys.release(key::caps_shift);
    test::check_eq(keys.scan(0xfefe) & 0x1f, 0x1f,
                   "releasing restores every bit");

    // the five keys of a half-row occupy bits 0 to 4 in order.
    const key row[5] = {key::caps_shift, key::z, key::x, key::c, key::v};
    for (int bit = 0; bit < 5; ++bit) {
        keys.release_all();
        keys.press(row[bit]);
        const u8 scanned = keys.scan(0xfefe);
        test::check_eq(scanned & 0x1f, 0x1f & ~(1 << bit),
                       std::string(key_name(row[bit])) + " drives bit " +
                           std::to_string(bit));
    }

    // selecting several half-rows at once merges their keys, which is
    // how the ROM tests for any key at all.
    keys.release_all();
    keys.press(key::space);
    test::check_eq(keys.scan(0x7ffe) & 0x01, 0,
                   "space appears in its own half-row");
    test::check_eq(keys.scan(0x00fe) & 0x01, 0,
                   "and when every half-row is selected");
}

void test_key_names()
{
    test::section("key names and aliases");

    test::check(key_from_name("A") == key::a, "a letter by name");
    test::check(key_from_name("a") == key::a, "matching ignores case");
    test::check(key_from_name("CAPS_SHIFT") == key::caps_shift,
                "the canonical shift name");
    test::check(key_from_name("caps shift") == key::caps_shift,
                "a space instead of an underscore");
    test::check(key_from_name("caps-shift") == key::caps_shift,
                "a hyphen instead of an underscore");
    test::check(key_from_name("CS") == key::caps_shift, "the alias");
    test::check(key_from_name("SS") == key::symbol_shift,
                "the symbol shift alias");
    test::check(key_from_name("RETURN") == key::enter, "an enter alias");
    test::check(key_from_name("0") == key::num_0, "a digit");
    test::check(!key_from_name("F1").has_value(),
                "a key the machine does not have is rejected");
    test::check(!key_from_name("").has_value(),
                "an empty name is rejected");

    test::check_eq(static_cast<long long>(all_keys().size()), 40,
                   "the matrix has forty keys");
}

void test_character_translation()
{
    test::section("typing characters");

    const auto lower = chord_for_character('a');
    test::check(lower && lower->primary == key::a && !lower->modifier,
                "a lower case letter needs no shift");

    const auto upper = chord_for_character('A');
    test::check(upper && upper->primary == key::a &&
                    upper->modifier == key::caps_shift,
                "an upper case letter needs caps shift");

    const auto quote = chord_for_character('"');
    test::check(quote && quote->primary == key::p &&
                    quote->modifier == key::symbol_shift,
                "a quote is symbol shift and P");

    const auto newline = chord_for_character('\n');
    test::check(newline && newline->primary == key::enter,
                "a newline is ENTER");

    const auto space = chord_for_character(' ');
    test::check(space && space->primary == key::space, "a space is SPACE");

    test::check(!chord_for_character('\x01').has_value(),
                "a control character cannot be typed");
    test::check(!chord_for_character('~').has_value(),
                "a character not on the keyboard is refused");
}

void test_port_reads_through_the_ula()
{
    test::section("reading the keyboard through port 0xFE");

    machine target;

    // nothing pressed: the five key bits read high, and the two unused
    // lines float high too.
    const u8 idle = target.read_port(0xfefe);
    test::check_eq(idle & 0x1f, 0x1f, "no key is down");
    test::check_eq(idle & 0xa0, 0xa0, "the unused lines read high");

    target.keys().press(key::z);
    test::check_eq(target.read_port(0xfefe) & 0x02, 0,
                   "Z reads low once pressed");
    test::check_eq(target.read_port(0xfdfe) & 0x02, 0x02,
                   "and not in a different half-row");

    target.keys().release_all();
    test::check_eq(target.read_port(0xfefe) & 0x1f, 0x1f,
                   "releasing everything clears the matrix");
}

void test_border_and_speaker_writes()
{
    test::section("writing port 0xFE");

    machine target;

    target.write_port(0xfe, 0x05);
    test::check_eq(target.video().border(), 5, "bits 0 to 2 set the border");

    target.write_port(0xfe, 0x18);
    test::check(target.video().speaker(), "bit 4 drives the speaker");
    test::check(target.video().mic(), "bit 3 drives the tape output");
    test::check_eq(target.video().border(), 0,
                   "and the border came from the same write");
}

} // namespace

int main()
{
    test_matrix_layout();
    test_key_names();
    test_character_translation();
    test_port_reads_through_the_ula();
    test_border_and_speaker_writes();
    return test::summary("keyboard");
}
