//
// ULA memory and i/o contention, expressed as a swappable strategy.
//
// on a real 48K Spectrum the ULA and the CPU share the lower 16K of
// RAM. while the ULA is fetching display bytes it holds the CPU off the
// bus by asserting /WAIT, so an instruction touching 0x4000..0x7FFF
// takes longer than the data book says, and exactly how much longer
// depends on where the beam is. software has relied on this since 1982,
// so it cannot be treated as noise.
//
// contention_model is an interface rather than a function because the
// delay pattern is a property of the machine variant. the 48K pattern
// implemented here repeats 6,5,4,3,2,1,0,0 over each eight T-state
// block of a display line. a 128K or +2A/+3 model differs only in its
// table and its contended address range, so it can be added as another
// implementation without touching the CPU or the ULA.
//
// no_contention exists for the same reason: it makes the emulator run
// with textbook instruction timings, which is what the unit tests need
// when they check the CPU core in isolation.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef SPECTRUM_CONTENTION_H
#define SPECTRUM_CONTENTION_H

#include <vector>

#include "spectrum/timing.h"
#include "spectrum/types.h"

namespace spectrum {

//
// strategy interface for bus contention.
//
class contention_model {
public:
    virtual ~contention_model() = default;

    //
    // Returns:
    //      a short human readable name, reported by the info tool.
    //
    virtual const char *name() const = 0;

    //
    // Returns:
    //      true when the ULA shares this address with the CPU.
    //
    virtual bool is_contended_address(u16 address) const = 0;

    //
    // Extra T-states for a memory cycle starting now.
    //
    // Parameters:
    //      frame_tstate    - T-states since the frame interrupt.
    //      address         - address on the bus.
    //
    // Returns:
    //      number of T-states the CPU must be stalled, 0 when free.
    //
    virtual int memory_delay(u32 frame_tstate, u16 address) const = 0;

    //
    // Extra T-states for an i/o cycle starting now.
    //
    // Parameters:
    //      frame_tstate    - T-states since the frame interrupt.
    //      port            - full 16 bit port address.
    //
    // Returns:
    //      T-states of stall on top of the four the cycle already
    //      takes.
    //
    // Notes:
    //      the delay depends both on whether the port address decodes
    //      into the contended range and on whether A0 is low, which is
    //      what selects the ULA itself.
    //
    virtual int io_delay(u32 frame_tstate, u16 port) const = 0;
};

//
// contention as implemented by the 48K ULA.
//
class ula_contention final : public contention_model {
public:
    //
    // Build the per T-state delay table for the given geometry.
    //
    explicit ula_contention(const machine_timing &timing);

    const char *name() const override;
    bool is_contended_address(u16 address) const override;
    int memory_delay(u32 frame_tstate, u16 address) const override;
    int io_delay(u32 frame_tstate, u16 port) const override;

    //
    // Raw table lookup, ignoring the address entirely.
    //
    // Parameters:
    //      frame_tstate    - T-states since the frame interrupt; values
    //                        beyond one frame wrap around.
    //
    // Returns:
    //      the stall the ULA would impose at that moment.
    //
    // Notes:
    //      exposed for the i/o sequence, which has to sample the table
    //      several times as it walks through a single cycle, and for
    //      the tests that verify the table shape.
    //
    int raw_delay(u32 frame_tstate) const;

private:
    machine_timing timing_;
    std::vector<u8> delays_;
};

//
// a machine with no contention at all.
//
// useful for isolating CPU behaviour in tests and for running code
// faster than the real machine when timing does not matter.
//
class no_contention final : public contention_model {
public:
    const char *name() const override;
    bool is_contended_address(u16 address) const override;
    int memory_delay(u32 frame_tstate, u16 address) const override;
    int io_delay(u32 frame_tstate, u16 port) const override;
};

} // namespace spectrum

#endif // SPECTRUM_CONTENTION_H
