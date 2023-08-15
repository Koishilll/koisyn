#pragma once

#include "std/std_precomp.h"
#include "koisession.h"
#include "checksum.h"
#include "rpng.h"

namespace ks3::detail
{

using namespace std;

// Inputs from player and will send on wire.
// The size of array must >= 0 and it can't be empty.
template <int FixedSize = 0, bool DynamicSize = true>
    requires (FixedSize >= 0 and (FixedSize != 0 || DynamicSize))
struct InputData;

// could be <0, true>
template <>
struct InputData<0, true>
{
    static constexpr int FIXED_SIZE = 0;
    static constexpr bool DYNAMIC_SIZE = true;
    span<uint8_t> dyn;
};

// could be any <N, false> for N > 0
template <int FixedSize> requires (FixedSize > 0)
struct InputData<FixedSize, false>
{
    static constexpr int FIXED_SIZE = FixedSize;
    static constexpr bool DYNAMIC_SIZE = false;
    array<uint8_t, FixedSize> fixed;
};

// could be any <N, true> for N > 0
template <int FixedSize> requires (FixedSize > 0)
struct InputData<FixedSize, true>
{
    static constexpr int FIXED_SIZE = FixedSize;
    static constexpr bool DYNAMIC_SIZE = true;
    array<uint8_t, FixedSize> fixed;
    span<uint8_t> dyn;
};

template <typename State>
using InputFor = InputData<State::FIXED_INPUT_SIZE, State::DYNAMIC_INPUT_SIZE>;

// your game state needs to meet this requirements
template <typename State>
concept ConceptGameState =
    ConceptChecksumCalculable<State> &&
    requires
    {
        int{State::MAX_NUM_PLAYERS};
        int{State::FRAME_RATE};
        int{State::INPUT_DELAY_FRAME};
        int{State::FIXED_INPUT_SIZE};
        bool{State::DYNAMIC_INPUT_SIZE};
        new InputFor<State>;
    } &&
    requires (State a, span<const InputFor<State>> cmds)
    {
        a.Advance(cmds);
        { a.Over() } noexcept -> same_as<bool>;
    };

class KoiSynBase
{
public:
    // TODO: need a thread safe queue
    shared_ptr<queue<QUIC_BUFFER>> InputQueue;

    virtual ~KoiSynBase() noexcept {};
    virtual void Advance() = 0;
};

template <ConceptGameState State>
class KoiSyn final : public KoiSynBase
{
public:
    ~KoiSyn() noexcept {}
    void Advance() noexcept override {}
};

} // namespace ks3::detail
