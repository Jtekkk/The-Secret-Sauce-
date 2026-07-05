// =============================================================================
// The Secret Sauce — offline test harness.
//
// Drives the real AudioProcessor the way a host would, across sample rates and
// buffer sizes, and enforces the contracts that make a plugin boring and
// reliable: reported latency is real, neutral settings null, mix/delta are
// phase-true, state and snapshots round-trip, nothing ever goes non-finite.
// =============================================================================

#include "../src/PluginProcessor.h"

#include <cstdio>
#include <random>

namespace
{
    int failures = 0;
    int checks = 0;

    void check (bool condition, const std::string& message)
    {
        ++checks;
        if (! condition)
        {
            ++failures;
            std::printf ("  [FAIL] %s\n", message.c_str());
        }
    }

    void section (const char* name) { std::printf ("== %s\n", name); }

    void setParam (SecretSauceProcessor& proc, const char* id, float denormValue)
    {
        auto* param = proc.apvts.getParameter (id);
        jassert (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (denormValue));
    }

    float getParam (SecretSauceProcessor& proc, const char* id)
    {
        auto* param = proc.apvts.getParameter (id);
        return param->convertFrom0to1 (param->getValue());
    }

    /** Everything off / neutral: the recipe must produce a bit-honest chain. */
    void setNeutral (SecretSauceProcessor& proc)
    {
        namespace p = sauce::param;
        setParam (proc, p::sauceAmt, 0.0f);
        setParam (proc, p::heat, 0.0f);
        setParam (proc, p::console, 0.0f);
        for (auto* id : { p::warmth, p::air, p::body, p::presence, p::weight, p::sparkle,
                          p::glue, p::punch, p::density, p::snap, p::tight, p::loud })
            setParam (proc, id, 0.0f);
        setParam (proc, p::width, 100.0f);
        setParam (proc, p::monoSafe, 0.0f);
        setParam (proc, p::bassMonoHz, 0.0f);
        setParam (proc, p::mix, 100.0f);
        setParam (proc, p::autoGain, 0.0f);
        setParam (proc, p::delta, 0.0f);
        setParam (proc, p::bypass, 0.0f);
        setParam (proc, p::inTrim, 0.0f);
        setParam (proc, p::output, 0.0f);
        setParam (proc, p::oversampling, 1.0f);   // Off
        setParam (proc, p::linearPhase, 0.0f);
        setParam (proc, p::drift, 0.0f);
        setParam (proc, p::extSidechain, 0.0f);
    }

    bool allFinite (const juce::AudioBuffer<float>& buffer, int numSamples)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const float* d = buffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                if (! std::isfinite (d[i]))
                    return false;
        }
        return true;
    }

    void processBlocks (SecretSauceProcessor& proc, juce::AudioBuffer<float>& io, int blockSize)
    {
        juce::MidiBuffer midi;
        const int total = io.getNumSamples();

        for (int start = 0; start < total; start += blockSize)
        {
            const int len = juce::jmin (blockSize, total - start);
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), io.getNumChannels(), start, len);
            proc.processBlock (view, midi);
        }
    }

    juce::AudioBuffer<float> makeSine (int channels, int numSamples, double sampleRate,
                                       double freq = 1000.0, float amp = 0.5f)
    {
        juce::AudioBuffer<float> buffer (channels, numSamples);
        for (int ch = 0; ch < channels; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                d[i] = amp * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / sampleRate);
        }
        return buffer;
    }

    juce::AudioBuffer<float> makeNoise (int channels, int numSamples, unsigned seed = 42)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-0.5f, 0.5f);

        juce::AudioBuffer<float> buffer (channels, numSamples);
        for (int ch = 0; ch < channels; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                d[i] = dist (rng);
        }
        return buffer;
    }

    float rmsDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b,
                   int offsetA, int offsetB, int count)
    {
        double sum = 0.0;
        int n = 0;
        for (int ch = 0; ch < juce::jmin (a.getNumChannels(), b.getNumChannels()); ++ch)
        {
            const float* da = a.getReadPointer (ch);
            const float* db = b.getReadPointer (ch);
            for (int i = 0; i < count; ++i)
            {
                const double diff = (double) da[offsetA + i] - db[offsetB + i];
                sum += diff * diff;
                ++n;
            }
        }
        return n > 0 ? (float) std::sqrt (sum / n) : 0.0f;
    }
}

//==============================================================================
static void testLifecycleAcrossFormats()
{
    section ("Lifecycle across sample rates and block sizes");

    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (int bs : { 16, 64, 333, 512, 4096 })
        {
            SecretSauceProcessor proc;
            proc.setPlayConfigDetails (2, 2, sr, bs);
            proc.prepareToPlay (sr, bs);

            auto io = makeNoise (2, bs * 4);
            processBlocks (proc, io, bs);

            check (allFinite (io, io.getNumSamples()),
                   "finite output at sr=" + std::to_string (sr) + " bs=" + std::to_string (bs));

            proc.releaseResources();
        }
    }

    // Hosts that lie: declare 512, then deliver 4096 in one call.
    {
        SecretSauceProcessor proc;
        proc.setPlayConfigDetails (2, 2, 48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
        setParam (proc, sauce::param::sauceAmt, 60.0f);

        auto io = makeNoise (2, 4096);
        juce::MidiBuffer midi;
        proc.processBlock (io, midi);

        check (allFinite (io, io.getNumSamples()), "oversized block (4096 into 512) survives");
    }
}

static void testLatencyAndNull()
{
    section ("Latency honesty + neutral null");

    for (double sr : { 44100.0, 96000.0 })
    {
        SecretSauceProcessor proc;
        proc.setPlayConfigDetails (2, 2, sr, 512);
        proc.prepareToPlay (sr, 512);
        setNeutral (proc);

        // Warm up so smoothers settle.
        auto warm = makeSine (2, 4096, sr);
        processBlocks (proc, warm, 512);

        const int latency = proc.getLatencySamples();
        check (latency == proc.getCurrentLatency(), "getLatencySamples matches internal latency");
        check (latency >= 0 && latency < (int) sr, "latency sane: " + std::to_string (latency));

        // Impulse through the neutral chain must come out as a delayed impulse.
        const int n = 8192;
        juce::AudioBuffer<float> io (2, n);
        io.clear();
        io.setSample (0, 100, 1.0f);
        io.setSample (1, 100, 1.0f);

        auto input = io;
        processBlocks (proc, io, 512);

        int peakPos = 0;
        float peakVal = 0.0f;
        const float* d = io.getReadPointer (0);
        for (int i = 0; i < n; ++i)
            if (std::abs (d[i]) > peakVal) { peakVal = std::abs (d[i]); peakPos = i; }

        check (peakPos == 100 + latency,
               "impulse lands exactly at reported latency (sr=" + std::to_string ((int) sr)
                   + ", expected " + std::to_string (100 + latency) + ", got " + std::to_string (peakPos) + ")");

        const float diff = rmsDiff (io, input, latency, 0, n - latency - 256);
        check (diff < 1.0e-3f,
               "neutral chain nulls against input (rms diff " + std::to_string (diff) + ")");
    }
}

static void testMixAndDelta()
{
    section ("Mix=0 returns dry; Delta on neutral chain returns silence");

    const double sr = 48000.0;

    {
        SecretSauceProcessor proc;
        proc.setPlayConfigDetails (2, 2, sr, 512);
        proc.prepareToPlay (sr, 512);
        setNeutral (proc);

        // Crank everything, then set mix to 0: output must be (delayed) input.
        namespace p = sauce::param;
        setParam (proc, p::sauceAmt, 100.0f);
        setParam (proc, p::heat, 90.0f);
        setParam (proc, p::glue, 80.0f);
        setParam (proc, p::tight, 70.0f);
        setParam (proc, p::loud, 60.0f);
        setParam (proc, p::mix, 0.0f);

        auto warm = makeSine (2, 8192, sr);
        processBlocks (proc, warm, 512);

        const int latency = proc.getLatencySamples();
        const int n = 8192;
        auto input = makeSine (2, n, sr, 220.0);
        auto io = input;
        processBlocks (proc, io, 512);

        const float diff = rmsDiff (io, input, latency, 0, n - latency - 256);
        check (diff < 1.0e-3f, "mix=0 output equals aligned dry (rms diff " + std::to_string (diff) + ")");
    }

    {
        SecretSauceProcessor proc;
        proc.setPlayConfigDetails (2, 2, sr, 512);
        proc.prepareToPlay (sr, 512);
        setNeutral (proc);
        setParam (proc, sauce::param::delta, 1.0f);

        auto warm = makeSine (2, 8192, sr);
        processBlocks (proc, warm, 512);

        auto io = makeSine (2, 4096, sr, 330.0);
        processBlocks (proc, io, 512);

        const float rms = io.getRMSLevel (0, 2048, 2048);
        check (rms < 1.0e-3f, "delta of neutral chain is silence (rms " + std::to_string (rms) + ")");
    }
}

static void testStateRoundTrip()
{
    section ("State save/load round trip");

    namespace p = sauce::param;

    SecretSauceProcessor a;
    a.setPlayConfigDetails (2, 2, 48000.0, 512);
    a.prepareToPlay (48000.0, 512);

    setParam (a, p::sauceAmt, 72.5f);
    setParam (a, p::warmth, -33.0f);
    setParam (a, p::glue, 41.0f);
    setParam (a, p::width, 137.0f);
    setParam (a, p::console, 3.0f);
    setParam (a, p::linearPhase, 1.0f);

    juce::MemoryBlock state;
    a.getStateInformation (state);

    SecretSauceProcessor b;
    b.setPlayConfigDetails (2, 2, 48000.0, 512);
    b.prepareToPlay (48000.0, 512);
    b.setStateInformation (state.getData(), (int) state.getSize());

    for (auto* id : { p::sauceAmt, p::warmth, p::glue, p::width, p::console })
        check (std::abs (getParam (b, id) - getParam (a, id)) < 1.0e-3f,
               std::string ("param '") + id + "' survives the round trip");

    check (a.getDriftSeed() == b.getDriftSeed(), "drift seed survives the round trip");
}

static void testSnapshots()
{
    section ("A/B/C/D snapshots");

    namespace p = sauce::param;

    SecretSauceProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    setParam (proc, p::sauceAmt, 20.0f);
    proc.snapshots.capture (0);

    setParam (proc, p::sauceAmt, 80.0f);
    proc.snapshots.capture (1);

    proc.snapshots.recall (0);
    check (std::abs (getParam (proc, p::sauceAmt) - 20.0f) < 1.0e-3f, "recall A restores value");

    proc.snapshots.recall (1);
    check (std::abs (getParam (proc, p::sauceAmt) - 80.0f) < 1.0e-3f, "recall B restores value");

    // Snapshots must survive a state round trip too.
    juce::MemoryBlock state;
    proc.getStateInformation (state);

    SecretSauceProcessor other;
    other.setPlayConfigDetails (2, 2, 48000.0, 512);
    other.prepareToPlay (48000.0, 512);
    other.setStateInformation (state.getData(), (int) state.getSize());

    other.snapshots.recall (0);
    check (std::abs (getParam (other, p::sauceAmt) - 20.0f) < 1.0e-3f, "snapshot A survives state reload");
}

static void testParameterFuzz()
{
    section ("Parameter fuzz (random settings never break the audio)");

    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> dist (0.0f, 1.0f);

    SecretSauceProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 256);
    proc.prepareToPlay (48000.0, 256);

    auto& params = proc.getParameters();

    for (int round = 0; round < 40; ++round)
    {
        for (auto* param : params)
            if (dist (rng) < 0.4f)
                param->setValueNotifyingHost (dist (rng));

        auto io = makeNoise (2, 1024, (unsigned) round);
        processBlocks (proc, io, 256);

        if (! allFinite (io, io.getNumSamples()))
        {
            check (false, "non-finite output in fuzz round " + std::to_string (round));
            return;
        }
    }

    check (true, "40 fuzz rounds clean");
}

static void testDenormalSafety()
{
    section ("Denormal / tiny-signal safety");

    SecretSauceProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    setParam (proc, sauce::param::sauceAmt, 85.0f);
    setParam (proc, sauce::param::loud, 60.0f);

    juce::AudioBuffer<float> io (2, 4096);
    for (int ch = 0; ch < 2; ++ch)
    {
        float* d = io.getWritePointer (ch);
        for (int i = 0; i < 4096; ++i)
            d[i] = 1.0e-30f * (i % 2 == 0 ? 1.0f : -1.0f);
    }

    processBlocks (proc, io, 512);
    check (allFinite (io, io.getNumSamples()), "tiny signals stay finite");

    // Silence after signal: envelope tails must decay without denormal stalls.
    auto loud = makeSine (2, 4096, 48000.0, 100.0, 0.9f);
    processBlocks (proc, loud, 512);

    juce::AudioBuffer<float> silence (2, 8192);
    silence.clear();

    const auto start = juce::Time::getMillisecondCounterHiRes();
    processBlocks (proc, silence, 512);
    const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;

    check (allFinite (silence, silence.getNumSamples()), "silence tail finite");
    check (elapsed < 500.0, "silence processing not denormal-stalled (" + std::to_string (elapsed) + " ms)");
}

static void testBlockSizeInvariance()
{
    section ("Block size invariance");

    auto run = [] (int blockSize)
    {
        SecretSauceProcessor proc;
        proc.setPlayConfigDetails (2, 2, 48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
        setNeutral (proc);
        setParam (proc, sauce::param::sauceAmt, 60.0f);
        setParam (proc, sauce::param::heat, 40.0f);
        setParam (proc, sauce::param::glue, 30.0f);

        auto io = makeNoise (2, 8192, 7);
        processBlocks (proc, io, blockSize);
        return io;
    };

    auto big = run (512);
    auto small = run (64);

    const float diff = rmsDiff (big, small, 4096, 4096, 2048);
    check (diff < 0.02f, "512-sample vs 64-sample blocks agree (rms diff " + std::to_string (diff) + ")");
}

static void testOversamplingAlignment()
{
    section ("Oversampling settings stay latency-honest and null on tones");

    const double sr = 48000.0;

    for (float osChoice : { 2.0f, 3.0f, 4.0f, 5.0f })   // 2x, 4x, 8x, 16x
    {
        SecretSauceProcessor proc;
        proc.setPlayConfigDetails (2, 2, sr, 512);
        proc.prepareToPlay (sr, 512);
        setNeutral (proc);
        setParam (proc, sauce::param::oversampling, osChoice);

        auto warm = makeSine (2, 8192, sr);
        processBlocks (proc, warm, 512);

        const int latency = proc.getLatencySamples();
        const int n = 16384;
        auto input = makeSine (2, n, sr, 997.0);
        auto io = input;
        processBlocks (proc, io, 512);

        const float diff = rmsDiff (io, input, latency, 0, n - latency - 512);
        check (diff < 5.0e-3f,
               "os choice " + std::to_string ((int) osChoice) + " nulls at reported latency (rms "
                   + std::to_string (diff) + ")");
    }
}

static void testLinearPhaseLatency()
{
    section ("Linear-phase EQ reports honest latency");

    const double sr = 48000.0;

    SecretSauceProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, 512);
    proc.prepareToPlay (sr, 512);
    setNeutral (proc);
    setParam (proc, sauce::param::air, 60.0f);          // engage a HF band
    setParam (proc, sauce::param::linearPhase, 1.0f);

    const int minPhaseLatency = proc.getLatencySamples();

    // Let the background IR build/load and the engagement flip happen.
    for (int i = 0; i < 40; ++i)
    {
        auto warm = makeSine (2, 2048, sr);
        processBlocks (proc, warm, 512);
        juce::Thread::sleep (10);
    }

    const int latency = proc.getLatencySamples();
    check (latency > minPhaseLatency + 500,
           "linear phase raises reported latency (" + std::to_string (latency) + ")");

    // A symmetric linear-phase FIR centres the impulse at the reported latency.
    const int n = 16384;
    juce::AudioBuffer<float> io (2, n);
    io.clear();
    io.setSample (0, 100, 1.0f);
    io.setSample (1, 100, 1.0f);
    processBlocks (proc, io, 512);

    int peakPos = 0;
    float peakVal = 0.0f;
    const float* d = io.getReadPointer (0);
    for (int i = 0; i < n; ++i)
        if (std::abs (d[i]) > peakVal) { peakVal = std::abs (d[i]); peakPos = i; }

    check (std::abs (peakPos - (100 + latency)) <= 2,
           "linear-phase impulse centres at reported latency (expected "
               + std::to_string (100 + latency) + ", got " + std::to_string (peakPos) + ")");
}

static void testLimiterCeiling()
{
    section ("Maximizer respects the ceiling");

    const double sr = 48000.0;

    SecretSauceProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, 512);
    proc.prepareToPlay (sr, 512);
    setNeutral (proc);
    setParam (proc, sauce::param::loud, 100.0f);
    setParam (proc, sauce::param::ceiling, -1.0f);

    auto warm = makeSine (2, 24000, sr, 120.0, 0.9f);
    processBlocks (proc, warm, 512);

    auto io = makeSine (2, 24000, sr, 120.0, 0.9f);
    processBlocks (proc, io, 512);

    float peak = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        peak = juce::jmax (peak, io.getMagnitude (ch, 4096, 24000 - 4096));

    // -1 dBTP target; allow the documented transient-preserve overshoot.
    check (peak <= sauce::dsp::dbToGain (-1.0f + 1.4f),
           "steady-state peak stays near the ceiling (peak "
               + std::to_string (sauce::dsp::gainToDb (peak)) + " dB)");
    check (peak > 0.05f, "maximizer passes signal");

    // Adversarial case: single-sample spikes riding a quiet sine, auto-gain ON
    // (which sits after the limiter). The Ceiling promise must hold on the
    // FINAL output — this is the regression test for the one-pole-attack
    // overshoot and the post-limiter gain staging.
    {
        SecretSauceProcessor spiky;
        spiky.setPlayConfigDetails (2, 2, sr, 512);
        spiky.prepareToPlay (sr, 512);
        setNeutral (spiky);
        setParam (spiky, sauce::param::loud, 100.0f);
        setParam (spiky, sauce::param::ceiling, -1.0f);
        setParam (spiky, sauce::param::autoGain, 1.0f);

        auto makeSpiky = [&] (int n)
        {
            auto b = makeSine (2, n, sr, 220.0, 0.25f);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 300; i < n; i += 1553)
                    b.getWritePointer (ch)[i] = (i / 1553) % 2 == 0 ? 2.0f : -2.0f;
            return b;
        };

        auto spikyWarm = makeSpiky (48000);
        processBlocks (spiky, spikyWarm, 512);

        auto io = makeSpiky (48000);
        processBlocks (spiky, io, 512);

        float spikePeak = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            spikePeak = juce::jmax (spikePeak, io.getMagnitude (ch, 0, io.getNumSamples()));

        check (spikePeak <= sauce::dsp::dbToGain (-1.0f) + 1.0e-4f,
               "spikes + auto-gain never pass the ceiling (peak "
                   + std::to_string (sauce::dsp::gainToDb (spikePeak)) + " dB)");
    }
}

static void testAutoGainMatch()
{
    section ("Auto gain holds loudness through heavy processing");

    const double sr = 48000.0;

    SecretSauceProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, 512);
    proc.prepareToPlay (sr, 512);
    setNeutral (proc);
    setParam (proc, sauce::param::sauceAmt, 70.0f);
    setParam (proc, sauce::param::heat, 60.0f);
    setParam (proc, sauce::param::glue, 40.0f);
    setParam (proc, sauce::param::autoGain, 1.0f);

    // 6 s of programme so the estimators settle and the slew completes.
    const float inAmp = 0.25f;
    for (int i = 0; i < 12; ++i)
    {
        auto blockAudio = makeSine (2, 24000, sr, 220.0, inAmp);
        processBlocks (proc, blockAudio, 512);
    }

    auto io = makeSine (2, 24000, sr, 220.0, inAmp);
    processBlocks (proc, io, 512);

    const float outRms = io.getRMSLevel (0, 4096, 24000 - 4096);
    const float inRms  = inAmp * 0.7071f;
    const float deltaDb = std::abs (sauce::dsp::gainToDb (outRms / inRms));

    check (deltaDb < 2.5f,
           "output loudness within 2.5 dB of input under heavy settings (delta "
               + std::to_string (deltaDb) + " dB)");
}

static void testDeltaHearsColour()
{
    section ("Delta exposes added colour");

    const double sr = 48000.0;

    SecretSauceProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, 512);
    proc.prepareToPlay (sr, 512);
    setNeutral (proc);
    setParam (proc, sauce::param::sauceAmt, 70.0f);
    setParam (proc, sauce::param::heat, 60.0f);
    setParam (proc, sauce::param::delta, 1.0f);

    auto warm = makeSine (2, 12000, sr, 220.0);
    processBlocks (proc, warm, 512);

    auto io = makeSine (2, 12000, sr, 220.0);
    processBlocks (proc, io, 512);

    const float rms = io.getRMSLevel (0, 6000, 4096);
    check (rms > 1.0e-4f, "delta of a coloured chain is audible (rms " + std::to_string (rms) + ")");
    check (rms < 0.5f, "delta is a residue, not the full signal (rms " + std::to_string (rms) + ")");
}

static void testMonoProcessing()
{
    section ("Mono bus processing");

    const double sr = 48000.0;

    SecretSauceProcessor proc;
    proc.setPlayConfigDetails (1, 1, sr, 512);
    proc.prepareToPlay (sr, 512);
    setParam (proc, sauce::param::sauceAmt, 60.0f);
    setParam (proc, sauce::param::heat, 40.0f);
    setParam (proc, sauce::param::width, 160.0f);      // must be a no-op in mono

    auto io = makeSine (1, 8192, sr, 220.0);
    processBlocks (proc, io, 512);

    check (allFinite (io, io.getNumSamples()), "mono chain stays finite");
    check (io.getRMSLevel (0, 4096, 2048) > 1.0e-3f, "mono chain passes signal");
}

static void testSidechainSmoke()
{
    section ("External sidechain path");

    const double sr = 48000.0;

    SecretSauceProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, 512);
    proc.prepareToPlay (sr, 512);
    setParam (proc, sauce::param::extSidechain, 1.0f);
    setParam (proc, sauce::param::glue, 60.0f);
    setParam (proc, sauce::param::scHighpass, 120.0f);
    setParam (proc, sauce::param::scLowpass, 8000.0f);

    auto io = makeSine (2, 8192, sr, 220.0);
    processBlocks (proc, io, 512);

    check (allFinite (io, io.getNumSamples()), "ext sidechain enabled (bus absent) stays finite");
}

static void testCpuSmoke()
{
    section ("CPU smoke (must run comfortably in realtime)");

    SecretSauceProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    setParam (proc, sauce::param::sauceAmt, 70.0f);
    setParam (proc, sauce::param::heat, 50.0f);
    setParam (proc, sauce::param::glue, 40.0f);
    setParam (proc, sauce::param::tight, 40.0f);
    setParam (proc, sauce::param::loud, 40.0f);
    setParam (proc, sauce::param::oversampling, 3.0f);   // 4x — a demanding setting

    const double seconds = 10.0;
    const int blockSize = 512;
    const int blocks = (int) (seconds * 48000.0 / blockSize);

    auto io = makeNoise (2, blockSize);
    juce::MidiBuffer midi;

    const auto start = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < blocks; ++i)
        proc.processBlock (io, midi);
    const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - start;

    const double realtimeFactor = (elapsedMs / 1000.0) / seconds;
    std::printf ("  realtime factor at 4x oversampling: %.3f (1.0 = just keeping up)\n", realtimeFactor);
    check (realtimeFactor < 1.0, "processes faster than realtime at 4x oversampling");
    check (allFinite (io, io.getNumSamples()), "cpu smoke output finite");
}

//==============================================================================
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf ("The Secret Sauce — offline test harness\n\n");

    testLifecycleAcrossFormats();
    testLatencyAndNull();
    testMixAndDelta();
    testStateRoundTrip();
    testSnapshots();
    testParameterFuzz();
    testDenormalSafety();
    testBlockSizeInvariance();
    testOversamplingAlignment();
    testLinearPhaseLatency();
    testLimiterCeiling();
    testAutoGainMatch();
    testDeltaHearsColour();
    testMonoProcessing();
    testSidechainSmoke();
    testCpuSmoke();

    std::printf ("\n%d checks, %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
