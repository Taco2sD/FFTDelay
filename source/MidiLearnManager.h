#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <atomic>

// Omnisphere-style MIDI Learn, generic over every parameter plus a couple of
// learnable actions (preset Next / Previous). State is keyed by CC number so the
// audio thread can look an incoming CC up in O(1); all fields are atomics so the
// message thread (UI: learn / clear / edit) and the audio thread (apply) never
// need a lock.
//
// A "target" is either a parameter's host index (0 .. numParams-1) or one of the
// action sentinels below.
class MidiLearnManager
{
public:
    static constexpr int numCC = 128;

    enum Action { noTarget = -1, presetNext = 100000, presetPrev = 100001 };

    MidiLearnManager() { clearAll(); }

    void clearAll() noexcept
    {
        for (int cc = 0; cc < numCC; ++cc)
        {
            target  [(size_t) cc].store (noTarget);
            channel [(size_t) cc].store (0);          // 0 = omni
            rangeMin[(size_t) cc].store (0.0f);
            rangeMax[(size_t) cc].store (1.0f);
            invert  [(size_t) cc].store (false);
        }
        learnTarget.store (noTarget);
    }

    // --- Message-thread editing ------------------------------------------------
    void arm     (int t) noexcept { learnTarget.store (t); }
    void disarm  ()      noexcept { learnTarget.store (noTarget); }
    int  getArmed()      const noexcept { return learnTarget.load(); }

    int ccForTarget (int t) const noexcept
    {
        for (int cc = 0; cc < numCC; ++cc)
            if (target[(size_t) cc].load() == t)
                return cc;
        return -1;
    }

    void clearTarget (int t) noexcept
    {
        for (int cc = 0; cc < numCC; ++cc)
            if (target[(size_t) cc].load() == t)
                target[(size_t) cc].store (noTarget);
    }

    bool getInvert (int t) const noexcept
    {
        const int cc = ccForTarget (t);
        return cc >= 0 && invert[(size_t) cc].load();
    }

    void setInvert (int t, bool inv) noexcept
    {
        const int cc = ccForTarget (t);
        if (cc >= 0) invert[(size_t) cc].store (inv);
    }

    // Bind a CC to the currently-armed target (called on the audio thread when a
    // CC arrives while armed). Resets that CC's range/channel to defaults.
    void bind (int cc, int t) noexcept
    {
        if (juce::isPositiveAndBelow (cc, numCC))
        {
            target  [(size_t) cc].store (t);
            channel [(size_t) cc].store (0);
            rangeMin[(size_t) cc].store (0.0f);
            rangeMax[(size_t) cc].store (1.0f);
            invert  [(size_t) cc].store (false);
        }
    }

    // CC-indexed assignment data (public for direct audio-thread access).
    std::array<std::atomic<int>,   numCC> target;
    std::array<std::atomic<int>,   numCC> channel;    // 0 = omni, else 1..16
    std::array<std::atomic<float>, numCC> rangeMin, rangeMax;
    std::array<std::atomic<bool>,  numCC> invert;
    std::atomic<int> learnTarget { noTarget };
};
