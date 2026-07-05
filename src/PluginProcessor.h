#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "Parameters.h"
#include "dsp/DspHelpers.h"
#include "dsp/AnalogDrift.h"
#include "dsp/Recipe.h"
#include "dsp/SaturationSuite.h"
#include "dsp/MicroDynamics.h"
#include "dsp/MusicalEQ.h"
#include "dsp/StereoProcessor.h"
#include "dsp/LowEndTightener.h"
#include "dsp/LoudnessMaximizer.h"
#include "dsp/Detector.h"
#include "dsp/AutoGain.h"
#include "dsp/ProgramSensor.h"
#include "dsp/Meters.h"
#include "dsp/DelayAlign.h"
#include "state/Snapshots.h"

// =============================================================================
// The Secret Sauce — processor.
//
// Signal flow (wet path):
//   in → input trim → [meters/detector/sensor taps]
//      → LowEndTightener → MusicalEQ → MicroDynamics
//      → [oversampled: SaturationSuite] → StereoProcessor → LoudnessMaximizer
//      → AutoGain → output trim → Mix (latency-aligned dry) → Delta monitor
//
// Latency = oversampler + limiter lookahead + linear-phase EQ; reported to the
// host, and the dry/delta paths are delayed to match so Mix is phase-true.
// =============================================================================
class SecretSauceProcessor : public juce::AudioProcessor,
                             private juce::AsyncUpdater
{
public:
    SecretSauceProcessor();
    ~SecretSauceProcessor() override;

    // --- AudioProcessor ------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    const juce::String getName() const override           { return "The Secret Sauce"; }
    bool acceptsMidi() const override                      { return false; }
    bool producesMidi() const override                     { return false; }
    bool isMidiEffect() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 0.15; }

    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override                  {}
    const juce::String getProgramName (int) override       { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    // --- Public plumbing for the editor ---------------------------------------
    juce::AudioProcessorValueTreeState apvts;
    juce::UndoManager undoManager;
    sauce::state::Snapshots snapshots { apvts };
    sauce::dsp::MeterValues meters;

    /** Re-seat the analog drift RNG (new "unit from the production line"). */
    void rerollDriftSeed();
    juce::int64 getDriftSeed() const { return drift.getSeed(); }

    /** Current total latency in samples (as reported to the host). */
    int getCurrentLatency() const { return totalLatency; }

private:
    void handleAsyncUpdate() override;   // host latency notification
    void updateLatency();
    int  resolveOsIndex() const;         // param -> oversampler slot (0..4 = 1x..16x)
    void readRawParams (sauce::dsp::RawParams& out) const;

    // --- Parameters (cached atomics) ------------------------------------------
    std::atomic<float>* pSauce = nullptr;      std::atomic<float>* pCharacter = nullptr;
    std::atomic<float>* pConsole = nullptr;    std::atomic<float>* pProgram = nullptr;
    std::atomic<float>* pWarmth = nullptr;     std::atomic<float>* pAir = nullptr;
    std::atomic<float>* pBody = nullptr;       std::atomic<float>* pPresence = nullptr;
    std::atomic<float>* pWeight = nullptr;     std::atomic<float>* pSparkle = nullptr;
    std::atomic<float>* pGlue = nullptr;       std::atomic<float>* pPunch = nullptr;
    std::atomic<float>* pDensity = nullptr;    std::atomic<float>* pSnap = nullptr;
    std::atomic<float>* pHeat = nullptr;       std::atomic<float>* pWidth = nullptr;
    std::atomic<float>* pMonoSafe = nullptr;   std::atomic<float>* pBassMono = nullptr;
    std::atomic<float>* pTight = nullptr;      std::atomic<float>* pLoud = nullptr;
    std::atomic<float>* pCeiling = nullptr;    std::atomic<float>* pInTrim = nullptr;
    std::atomic<float>* pOutput = nullptr;     std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pAutoGain = nullptr;   std::atomic<float>* pDelta = nullptr;
    std::atomic<float>* pOs = nullptr;         std::atomic<float>* pLinPhase = nullptr;
    std::atomic<float>* pDrift = nullptr;      std::atomic<float>* pExtSC = nullptr;
    std::atomic<float>* pScHP = nullptr;       std::atomic<float>* pScLP = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;

    // --- DSP -------------------------------------------------------------------
    sauce::dsp::AnalogDrift        drift;
    sauce::dsp::Recipe             recipe;
    sauce::dsp::ProgramSensor      sensor;
    sauce::dsp::Detector           detector;
    sauce::dsp::LowEndTightener    lowEnd;
    sauce::dsp::MusicalEQ          eq;
    sauce::dsp::MicroDynamics      dynamics;
    sauce::dsp::SaturationSuite    saturation;
    sauce::dsp::StereoProcessor    stereo;
    sauce::dsp::LoudnessMaximizer  maximizer;
    sauce::dsp::AutoGain           autoGain;
    sauce::dsp::DelayAlign         dryDelay;

    // One oversampler per factor (1x/2x/4x/8x/16x), created in prepareToPlay so
    // switching at runtime never allocates on the audio thread.
    static constexpr int numOsSlots = 5;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversamplers[numOsSlots];
    int currentOsSlot = 0;

    sauce::dsp::SmoothedGain inGain, outGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmooth, deltaSmooth;

    juce::AudioBuffer<float> dryBuffer, dryAligned;

    double currentSampleRate = 48000.0;
    int    currentBlockSize = 512;
    int    totalLatency = 0;
    int    preparedChannels = 2;
    bool   prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SecretSauceProcessor)
};
