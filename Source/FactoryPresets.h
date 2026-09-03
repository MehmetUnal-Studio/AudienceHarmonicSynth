#pragma once

// One source of truth for the screenshot-aligned performance baseline visible
// in the Cosmic Microwave UI. These are plain constants so the APVTS layout,
// preset recall, processor fallbacks and regression tests cannot drift apart.
namespace CosmicFactoryPresets
{
    static constexpr int count = 8;
    static constexpr int firstUdpPort = 6062;
    static constexpr int lastUdpPort = firstUdpPort + count - 1;

    static constexpr int midiOutputType = 1;       // Notes Only
    static constexpr int midiOutputPath = 1;       // External Only
    static constexpr int midiOutputOption = 1;     // Port-named virtual endpoint
    static constexpr int midiOutputRouteKind = 1;  // Virtual route
    static constexpr int exclusiveUdpPort = 1;
    static constexpr int safetyGovernorEnabled = 0;
    static constexpr int normalMidiRoutingMode = 1; // Per Source 1-16
    static constexpr int normalMidiChannel = 0;

    static constexpr int timeMode = 0;              // Flow
    static constexpr int clockSource = 0;           // Host
    static constexpr float internalBpm = 120.0f;
    static constexpr int gridDivision = 3;          // 1/32
    static constexpr int maxAttacksPerStep = 16;
    static constexpr int maxActiveVoices = 16;
    static constexpr float gatePercent = 100.0f;
    static constexpr int temporalSpread = 4;        // 16 steps
    static constexpr int crowdGovernorEnabled = 0;  // Manual

    // Fixed musical note lifetime is independent of Time Field attack
    // quantisation. Choice order is 2n, 4n, 8n, 16n, 32n.
    static constexpr int noteDuration = 3;           // 16n
    // Same-pitch Ensemble pulses default to the historical tied-tail
    // behaviour. Choice order is Tie, Retrigger.
    static constexpr int ensembleSameNoteMode = 0;   // Tie
    // Choice order is 64, 128, 256 participants. The physical MIDI topology
    // remains sixteen channels for every capacity.
    static constexpr int sourceCapacity = 0;         // 64 / 4 per channel

    static constexpr int conductorGroup = 0;        // Group 1
    static constexpr int conductorAttackBudget = 16;
    static constexpr int conductorVoiceBudget = 16;

    static constexpr int crowdMacrosEnabled = 0;
    static constexpr int crowdMacroChannel = 0;     // Ch 1
    static constexpr int crowdMacroDensityCc = 20;
    static constexpr int crowdMacroCentroidXCc = 21;
    static constexpr int crowdMacroCentroidYCc = 22;
    static constexpr int crowdMacroMotionCc = 23;
    static constexpr int crowdMacroRate = 1;        // 10 Hz
    static constexpr int pitchSystem = 1;            // Atomic
    static constexpr int scaleRoot = 0;              // C
    static constexpr int scaleRootOctave = 2;
    static constexpr int scaleMode = 0;
    static constexpr int scaleOctaves = 4;
    static constexpr int spectralElement = 28;       // Zn / Zinc
    static constexpr int atomicScaleMode = 0;        // Core

    struct ZonePreset
    {
        int index = 0;
        int udpPort = firstUdpPort;
        int expectedZoneChoice = 1; // APVTS: Any=0, A=1 ... Z=26
        int conductorRoleChoice = 1; // A=Leader, B-H=Follower
    };

    constexpr int sanitiseIndex (int index) noexcept
    {
        return index < 0 ? 0 : index >= count ? count - 1 : index;
    }

    constexpr ZonePreset zonePreset (int index) noexcept
    {
        const int safe = sanitiseIndex (index);
        return { safe, firstUdpPort + safe, safe + 1, safe == 0 ? 1 : 2 };
    }
}
