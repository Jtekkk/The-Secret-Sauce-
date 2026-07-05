#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace p = sauce::param;

//==============================================================================
SecretSauceProcessor::SecretSauceProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, &undoManager, "PARAMETERS", p::createLayout())
{
    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id); };

    pSauce = raw (p::sauceAmt);       pCharacter = raw (p::character);
    pConsole = raw (p::console);      pProgram = raw (p::programMode);
    pWarmth = raw (p::warmth);        pAir = raw (p::air);
    pBody = raw (p::body);            pPresence = raw (p::presence);
    pWeight = raw (p::weight);        pSparkle = raw (p::sparkle);
    pGlue = raw (p::glue);            pPunch = raw (p::punch);
    pDensity = raw (p::density);      pSnap = raw (p::snap);
    pHeat = raw (p::heat);            pWidth = raw (p::width);
    pMonoSafe = raw (p::monoSafe);    pBassMono = raw (p::bassMonoHz);
    pTight = raw (p::tight);          pLoud = raw (p::loud);
    pCeiling = raw (p::ceiling);      pInTrim = raw (p::inTrim);
    pOutput = raw (p::output);        pMix = raw (p::mix);
    pAutoGain = raw (p::autoGain);    pDelta = raw (p::delta);
    pOs = raw (p::oversampling);      pLinPhase = raw (p::linearPhase);
    pDrift = raw (p::drift);          pExtSC = raw (p::extSidechain);
    pScHP = raw (p::scHighpass);      pScLP = raw (p::scLowpass);

    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (p::bypass));
    jassert (bypassParam != nullptr);

    // Each instance ships as its own "unit off the line": random drift seed,
    // stable once saved with the session.
    const auto seed = juce::Random::getSystemRandom().nextInt64();
    driftSeed.store (seed, std::memory_order_relaxed);
    drift.setSeed (seed);            // safe: no audio thread yet
    recipe.setDrift (&drift);
}

SecretSauceProcessor::~SecretSauceProcessor() = default;

//==============================================================================
bool SecretSauceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn  = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != mainOut)
        return false;

    if (mainIn != juce::AudioChannelSet::mono() && mainIn != juce::AudioChannelSet::stereo())
        return false;

    // Sidechain: disabled, mono or stereo all fine.
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

//==============================================================================
void SecretSauceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = juce::jmax (16, samplesPerBlock);
    preparedChannels  = juce::jmax (1, getMainBusNumOutputChannels());

    const auto channels = (juce::uint32) preparedChannels;

    // Build every oversampler up front so runtime quality switches never
    // allocate on the audio thread. Slot n = 2^n oversampling.
    for (int slot = 0; slot < numOsSlots; ++slot)
    {
        oversamplers[slot] = std::make_unique<juce::dsp::Oversampling<float>> (
            channels, (size_t) slot,
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
            true /*max quality*/, true /*integer latency*/);
        oversamplers[slot]->initProcessing ((size_t) currentBlockSize);
    }

    currentOsSlot = resolveOsIndex();

    // Saturation runs at the highest possible oversampled rate; prepare it for
    // the worst case so runtime switches only retune coefficients.
    const double maxOsRate = sampleRate * (1 << (numOsSlots - 1));
    saturation.prepare ({ maxOsRate, (juce::uint32) (currentBlockSize << (numOsSlots - 1)), channels });

    detector.prepare (sampleRate, currentBlockSize, preparedChannels);
    sensor.prepare (sampleRate, currentBlockSize, preparedChannels);
    lowEnd.prepare (sampleRate, currentBlockSize, preparedChannels);
    eq.prepare (sampleRate, currentBlockSize, preparedChannels);
    dynamics.prepare (sampleRate, currentBlockSize, preparedChannels);
    stereo.prepare (sampleRate, currentBlockSize, preparedChannels);
    maximizer.prepare (sampleRate, currentBlockSize, preparedChannels);
    autoGain.prepare (sampleRate, currentBlockSize, preparedChannels);

    inGain.prepare (sampleRate);
    outGain.prepare (sampleRate);
    mixSmooth.reset (sampleRate, 0.03);
    mixSmooth.setCurrentAndTargetValue (juce::jlimit (0.0f, 1.0f, pMix->load() * 0.01f));
    deltaSmooth.reset (sampleRate, 0.03);
    deltaSmooth.setCurrentAndTargetValue (pDelta->load() > 0.5f ? 1.0f : 0.0f);

    dryBuffer.setSize (preparedChannels, currentBlockSize);
    dryAligned.setSize (preparedChannels, currentBlockSize);

    // Worst-case latency budget: 16x FIR oversampler + limiter lookahead +
    // linear-phase EQ. 1 s is a generous ceiling at any rate we support.
    const int maxAlign = (int) std::ceil (sampleRate) + 8192;
    dryDelay.prepare (sampleRate, currentBlockSize, preparedChannels, maxAlign);

    updateLatency();
    prepared = true;
}

void SecretSauceProcessor::releaseResources()
{
    prepared = false;
}

int SecretSauceProcessor::resolveOsIndex() const
{
    const int choice = (int) pOs->load();   // 0 Auto, 1 Off, 2 2x, 3 4x, 4 8x, 5 16x

    if (choice >= 1)
        return juce::jlimit (0, numOsSlots - 1, choice - 1);

    // Auto: keep aliasing inaudible without burning CPU.
    //   realtime: 2x below 88.2 kHz, off at high rates
    //   offline render: 8x below 88.2 kHz, 2x at high rates
    const bool highRate = currentSampleRate >= 88200.0;

    if (isNonRealtime())
        return highRate ? 1 : 3;

    return highRate ? 0 : 1;
}

void SecretSauceProcessor::updateLatency()
{
    const auto* os = oversamplers[currentOsSlot].get();
    const int osLatency = os != nullptr ? (int) std::lround (os->getLatencyInSamples()) : 0;

    totalLatency = osLatency + maximizer.latencySamples() + eq.latencySamples();
    setLatencySamples (totalLatency);
}

void SecretSauceProcessor::handleAsyncUpdate()
{
    updateLatency();
    updateHostDisplay (ChangeDetails().withLatencyChanged (true));
}

void SecretSauceProcessor::setDriftSeed (juce::int64 seed)
{
    driftSeed.store (seed, std::memory_order_relaxed);
    driftSeedDirty.store (true, std::memory_order_release);
}

void SecretSauceProcessor::rerollDriftSeed()
{
    setDriftSeed (juce::Random::getSystemRandom().nextInt64());
}

//==============================================================================
void SecretSauceProcessor::readRawParams (sauce::dsp::RawParams& out) const
{
    out.sauce        = pSauce->load();
    out.character    = (int) pCharacter->load();
    out.console      = (int) pConsole->load();
    out.programMode  = (int) pProgram->load();
    out.warmth       = pWarmth->load();
    out.air          = pAir->load();
    out.body         = pBody->load();
    out.presence     = pPresence->load();
    out.weight       = pWeight->load();
    out.sparkle      = pSparkle->load();
    out.glue         = pGlue->load();
    out.punch        = pPunch->load();
    out.density      = pDensity->load();
    out.snap         = pSnap->load();
    out.heat         = pHeat->load();
    out.width        = pWidth->load();
    out.monoSafe     = pMonoSafe->load() > 0.5f;
    out.bassMonoHz   = pBassMono->load();
    out.tight        = pTight->load();
    out.loud         = pLoud->load();
    out.ceilingDb    = pCeiling->load();
    out.driftDepth   = pDrift->load();
    out.scHighpassHz = pScHP->load();
    out.scLowpassHz  = pScLP->load();
    out.extSidechain = pExtSC->load() > 0.5f;
}

void SecretSauceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Some hosts hand over more samples than they declared in prepareToPlay.
    // Our buffers (dry copies, detector, oversamplers) are sized to the
    // declared maximum, so larger blocks are processed in declared-size chunks.
    if (prepared && buffer.getNumSamples() > currentBlockSize)
    {
        const int total = buffer.getNumSamples();
        for (int start = 0; start < total; start += currentBlockSize)
        {
            const int len = juce::jmin (currentBlockSize, total - start);
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(),
                                           buffer.getNumChannels(), start, len);
            processBlock (view, midi);
        }
        return;
    }

    // Apply a pending drift re-seed between blocks, never mid-recipe.
    if (driftSeedDirty.exchange (false, std::memory_order_acquire))
        drift.setSeed (driftSeed.load (std::memory_order_relaxed));

    auto mainBus = getBusBuffer (buffer, true, 0);
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin (mainBus.getNumChannels(), preparedChannels);

    if (! prepared || numSamples == 0 || numChannels == 0)
        return;

    // Clear any output channels beyond our processed set.
    for (int ch = numChannels; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    // ---- Parameter snapshot & recipe ---------------------------------------
    sauce::dsp::RawParams raw;
    readRawParams (raw);

    // Oversampling / linear-phase switches change latency: swap glitch-safely
    // and tell the host from the message thread.
    const int wantedOsSlot = resolveOsIndex();
    if (wantedOsSlot != currentOsSlot)
    {
        currentOsSlot = wantedOsSlot;
        oversamplers[currentOsSlot]->reset();
        updateLatency();          // keep our own alignment correct immediately
        triggerAsyncUpdate();     // host notification on the message thread
    }

    const auto settings = recipe.compute (raw, sensor.current());

    detector.update (settings.detector);
    lowEnd.update (settings.low);

    auto eqSettings = settings.eq;
    eqSettings.linearPhase = pLinPhase->load() > 0.5f;
    eq.update (eqSettings);

    dynamics.update (settings.dyn);

    auto satSettings = settings.sat;
    satSettings.oversampledRate = currentSampleRate * (double) (1 << currentOsSlot);
    saturation.update (satSettings);
    stereo.update (settings.stereo);
    maximizer.update (settings.max);

    const int eqLatency = eq.latencySamples();
    const int expectedLatency = (int) std::lround (oversamplers[currentOsSlot]->getLatencyInSamples())
                                + maximizer.latencySamples() + eqLatency;
    if (expectedLatency != totalLatency)
    {
        updateLatency();
        triggerAsyncUpdate();
    }

    // ---- Bypass: latency-matched dry pass so toggling is phase-continuous ---
    if (bypassParam->get())
    {
        dryDelay.setDelay (totalLatency);
        dryDelay.process (mainBus, mainBus, numSamples);
        sauce::dsp::publishLevels (mainBus, numSamples, meters.inPeak, meters.inRms);
        sauce::dsp::publishLevels (mainBus, numSamples, meters.outPeak, meters.outRms, &meters.correlation);
        return;
    }

    // ---- Input trim ----------------------------------------------------------
    inGain.setTargetDb (pInTrim->load());
    inGain.applyTo (mainBus, numSamples);

    // ---- Taps: meters, auto-gain reference, programme sensing ----------------
    sauce::dsp::publishLevels (mainBus, numSamples, meters.inPeak, meters.inRms);
    autoGain.measurePre (mainBus, numSamples);
    meters.inLufs.store (autoGain.inputLufs(), std::memory_order_relaxed);
    sensor.analyze (mainBus, numSamples);
    meters.detectedProgram.store ((int) sensor.current(), std::memory_order_relaxed);

    // ---- Dry copy (post-trim), aligned to the wet path's latency -------------
    for (int ch = 0; ch < numChannels; ++ch)
        dryBuffer.copyFrom (ch, 0, mainBus, ch, 0, numSamples);

    dryDelay.setDelay (totalLatency);
    dryDelay.process (dryBuffer, dryAligned, numSamples);

    // ---- Detector (internal signal or external sidechain) --------------------
    const float* env = nullptr;
    {
        auto scBus = getBusBuffer (buffer, true, 1);
        const bool useExt = raw.extSidechain && scBus.getNumChannels() > 0;
        env = detector.process (useExt ? scBus : mainBus, numSamples);
    }

    // ---- The chain ------------------------------------------------------------
    lowEnd.process (mainBus, numSamples);
    eq.process (mainBus, numSamples);
    dynamics.process (mainBus, numSamples, env);

    {
        juce::dsp::AudioBlock<float> block (mainBus.getArrayOfWritePointers(),
                                            (size_t) numChannels, (size_t) numSamples);
        auto& os = *oversamplers[currentOsSlot];
        auto osBlock = os.processSamplesUp (block);
        saturation.process (osBlock);
        os.processSamplesDown (block);
    }

    stereo.process (mainBus, numSamples);
    maximizer.process (mainBus, numSamples);

    // ---- Honest loudness: measure, compensate, then user output trim ---------
    autoGain.measurePost (mainBus, numSamples);
    autoGain.apply (mainBus, numSamples, pAutoGain->load() > 0.5f);
    meters.outLufs.store (autoGain.outputLufs(), std::memory_order_relaxed);
    meters.autoGainDb.store (autoGain.appliedDb(), std::memory_order_relaxed);

    outGain.setTargetDb (pOutput->load());
    outGain.applyTo (mainBus, numSamples);

    // ---- Mix (parallel processing) & Delta monitor ----------------------------
    mixSmooth.setTargetValue (juce::jlimit (0.0f, 1.0f, pMix->load() * 0.01f));
    deltaSmooth.setTargetValue (pDelta->load() > 0.5f ? 1.0f : 0.0f);

    for (int i = 0; i < numSamples; ++i)
    {
        const float mixAmt   = mixSmooth.getNextValue();
        const float deltaAmt = deltaSmooth.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* wet = mainBus.getWritePointer (ch);
            const float dry = dryAligned.getReadPointer (ch)[i];

            // Linear crossfade keeps parallel blends phase-true (paths are
            // time-aligned by dryDelay).
            float out = wet[i] * mixAmt + dry * (1.0f - mixAmt);

            // Delta: what the plugin is actually adding/removing.
            out -= deltaAmt * dry;

            wet[i] = sauce::dsp::sanitize (out);
        }
    }

    // ---- Output metering -------------------------------------------------------
    sauce::dsp::publishLevels (mainBus, numSamples, meters.outPeak, meters.outRms, &meters.correlation);
    meters.grDynamicsDb.store (dynamics.getGainReductionDb(), std::memory_order_relaxed);
    meters.grLimiterDb.store (maximizer.getGainReductionDb(), std::memory_order_relaxed);

    float outPeak = juce::jmax (meters.outPeak[0].load (std::memory_order_relaxed),
                                meters.outPeak[1].load (std::memory_order_relaxed));
    if (outPeak > 1.0f)
        meters.clipped.store (true, std::memory_order_relaxed);
}

//==============================================================================
void SecretSauceProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("SECRETSAUCE");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("driftSeed", drift.getSeed(), nullptr);

    root.appendChild (apvts.copyState(), nullptr);
    snapshots.writeTo (root);

    juce::MemoryOutputStream stream (destData, false);
    root.writeToStream (stream);
}

void SecretSauceProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto root = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! root.isValid() || ! root.hasType ("SECRETSAUCE"))
        return;

    setDriftSeed ((juce::int64) root.getProperty ("driftSeed", (juce::int64) 0x5EC5A0CE));

    auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid())
        apvts.replaceState (params.createCopy());

    snapshots.readFrom (root);
}

//==============================================================================
juce::AudioProcessorEditor* SecretSauceProcessor::createEditor()
{
    return new SecretSauceEditor (*this);
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SecretSauceProcessor();
}
