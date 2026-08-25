#include "FixChain.h"

namespace sourceglo
{

namespace
{
    // Corrective-band filter frequencies: Sub shelf, Low bell, LowMid bell,
    // HighMid bell, High shelf - matching the analyser's five bands.
    const float kFixFreq[FixChain::numFixBands] = { 45.0f, 150.0f, 700.0f, 3500.0f, 10000.0f };

    constexpr float kBodyHz = 180.0f, kTiltLowHz = 250.0f, kTiltHighHz = 2500.0f, kAirHz = 12000.0f;
    constexpr float kBodyRangeDb = 6.0f, kTiltRangeDb = 4.5f, kAirRangeDb = 6.0f;

    float envelopeCoeff (double sr, float ms)
    {
        return 1.0f - std::exp ((float) (-1.0 / (sr * ms * 0.001)));
    }
}

void FixChain::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    preparedBlockSize = maxBlockSize;

    for (auto& sm : fixBandSm) sm.reset (sr, 0.06);
    fixTrimSm.reset (sr, 0.06);
    bodySm.reset (sr, 0.05);
    toneSm.reset (sr, 0.05);
    airSm.reset (sr, 0.05);
    widthSm.reset (sr, 0.05);
    lowMonoSm.reset (sr, 0.06);
    chainMixSm.reset (sr, 0.05);

    toneSm.setCurrentAndTargetValue (0.5f);
    chainMixSm.setCurrentAndTargetValue (1.0f);

    fastAttack  = envelopeCoeff (sr, 0.4f);
    fastRelease = envelopeCoeff (sr, 28.0f);
    slowAttack  = envelopeCoeff (sr, 20.0f);
    slowRelease = envelopeCoeff (sr, 140.0f);
    lowLpCoeff  = 1.0f - std::exp ((float) (-2.0 * juce::MathConstants<double>::pi * 300.0 / sr));

    for (int f = 0; f < numFilters; ++f)
        lastFilterGain[f] = 1.0e9f;

    for (int ch = 0; ch < 2; ++ch)
    {
        dcFilters[ch].coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 20.0f);
        dcFilters[ch].reset();
    }
    sideHighPass.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 120.0f);
    sideHighPass.reset();

    for (int i = 0; i < 3; ++i)
    {
        oversamplers[i] = std::make_unique<juce::dsp::Oversampling<float>> (
            2, (size_t) (i + 1),
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false);
        oversamplers[i]->initProcessing ((size_t) maxBlockSize);
    }

    dryChain.setSize (2, maxBlockSize);
    reset();
}

void FixChain::reset()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        for (int f = 0; f < numFilters; ++f)
            filters[ch][f].reset();
        dcFilters[ch].reset();
    }
    sideHighPass.reset();
    envFast = envSlow = envLowFast = envLowSlow = lowLpState = 0.0f;
    for (auto& os : oversamplers)
        if (os != nullptr)
            os->reset();
}

int FixChain::getLatencySamples (int oversamplingIndex, bool hq) const noexcept
{
    if (! hq || oversamplingIndex <= 0 || oversamplers[oversamplingIndex - 1] == nullptr)
        return 0;
    return (int) std::lround (oversamplers[oversamplingIndex - 1]->getLatencyInSamples());
}

// -----------------------------------------------------------------------------
//  Fix engagement (message thread)
// -----------------------------------------------------------------------------
void FixChain::engageFix (const AnalysisResult& analysis)
{
    for (int b = 0; b < numFixBands; ++b)
    {
        // Counter the FULL measured deviation (capped +/-8 dB) whenever it
        // exceeds a +/-2 dB gate. The gate used to be SUBTRACTED as a
        // deadzone, which - multiplied by the 50 % default Fix Amount -
        // turned a +6 dB problem into a -2 dB nudge nobody could hear.
        // Correction, not re-design, is enforced by the cap alone.
        const float dev = analysis.bandDeviationDb[b];
        fixBandDb[b].store (std::abs (dev) > 2.0f
                              ? -juce::jlimit (-8.0f, 8.0f, dev) : 0.0f);
    }

    // Pull the result down to a -1 dBTP working ceiling - accounting for the
    // loudest boost the corrective EQ itself is about to add, or fixing the
    // tone would re-blow the headroom the trim just reclaimed.
    float maxBoost = 0.0f;
    for (int b = 0; b < numFixBands; ++b)
        maxBoost = juce::jmax (maxBoost, fixBandDb[b].load());

    const float predictedTp = analysis.truePeakDb + maxBoost;
    fixTrimDb.store (predictedTp > -1.0f
                       ? juce::jlimit (-12.0f, 0.0f, -(predictedTp + 1.0f))
                       : 0.0f);

    fixLowMono.store (analysis.lowCorrelation < 0.5f);
    fixDc.store (analysis.dcOffset);
    fixEngaged.store (true);
}

void FixChain::disengageFix()
{
    fixEngaged.store (false);
}

void FixChain::addTrimDb (float delta) noexcept
{
    fixTrimDb.store (juce::jlimit (-12.0f, 0.0f, fixTrimDb.load() + delta));
}

FixChain::FixState FixChain::getFixState() const
{
    FixState s;
    for (int b = 0; b < numFixBands; ++b)
        s.bandGainDb[b] = fixBandDb[b].load();
    s.trimDb   = fixTrimDb.load();
    s.lowMono  = fixLowMono.load();
    s.dcFilter = fixDc.load();
    return s;
}

void FixChain::setFixState (const FixState& state, bool engaged)
{
    for (int b = 0; b < numFixBands; ++b)
        fixBandDb[b].store (state.bandGainDb[b]);
    fixTrimDb.store (state.trimDb);
    fixLowMono.store (state.lowMono);
    fixDc.store (state.dcFilter);
    fixEngaged.store (engaged);
}

// -----------------------------------------------------------------------------
//  Audio thread
// -----------------------------------------------------------------------------
void FixChain::updateFilters (float fixAmount, const MacroValues& macros) noexcept
{
    float desired[numFilters];

    for (int b = 0; b < numFixBands; ++b)
        desired[b] = fixBandSm[b].getCurrentValue();

    desired[numFixBands + 0] = bodySm.getCurrentValue() * kBodyRangeDb;
    const float tilt = (toneSm.getCurrentValue() - 0.5f) * 2.0f * kTiltRangeDb;
    desired[numFixBands + 1] = -tilt;
    desired[numFixBands + 2] =  tilt;
    desired[numFixBands + 3] = airSm.getCurrentValue() * kAirRangeDb;

    juce::ignoreUnused (fixAmount, macros);

    for (int f = 0; f < numFilters; ++f)
    {
        if (std::abs (desired[f] - lastFilterGain[f]) < 0.02f)
            continue;
        lastFilterGain[f] = desired[f];

        const float gain = juce::Decibels::decibelsToGain (desired[f]);
        juce::dsp::IIR::Coefficients<float>::Ptr coeffs;

        if (f == 0)
            coeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf (sr, kFixFreq[0], 0.8f, gain);
        else if (f < numFixBands - 1)
            coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, kFixFreq[f], 0.8f, gain);
        else if (f == numFixBands - 1)
            coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, kFixFreq[f], 0.8f, gain);
        else if (f == numFixBands + 0)
            coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, kBodyHz, 0.9f, gain);
        else if (f == numFixBands + 1)
            coeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf (sr, kTiltLowHz, 0.7f, gain);
        else if (f == numFixBands + 2)
            coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, kTiltHighHz, 0.7f, gain);
        else
            coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
                        sr, juce::jmin (kAirHz, (float) sr * 0.42f), 0.8f, gain);

        for (int ch = 0; ch < 2; ++ch)
            filters[ch][f].coefficients = coeffs;
    }
}

float FixChain::transientGain (float l, float r, float punchAmt, float transAmt) noexcept
{
    const float mag = juce::jmax (std::abs (l), std::abs (r));

    envFast += (mag > envFast ? fastAttack : fastRelease) * (mag - envFast);
    envSlow += (mag > envSlow ? slowAttack : slowRelease) * (mag - envSlow);

    lowLpState += lowLpCoeff * (0.5f * (l + r) - lowLpState);
    const float lowMag = std::abs (lowLpState);
    envLowFast += (lowMag > envLowFast ? fastAttack : fastRelease) * (lowMag - envLowFast);
    envLowSlow += (lowMag > envLowSlow ? slowAttack : slowRelease) * (lowMag - envLowSlow);

    const float full = juce::jmax (0.0f, envFast - envSlow) / (envSlow + 0.02f);
    const float low  = juce::jmax (0.0f, envLowFast - envLowSlow) / (envLowSlow + 0.02f);

    return juce::jmin (2.2f, 1.0f + transAmt * 0.9f * juce::jmin (1.5f, full)
                                  + punchAmt * 1.1f * juce::jmin (1.5f, low));
}

void FixChain::process (juce::AudioBuffer<float>& buffer, const MacroValues& m) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    if (numSamples == 0 || numSamples > preparedBlockSize)
        return;

    // --- targets from the current parameter values -------------------------
    const bool engaged = fixEngaged.load();
    for (int b = 0; b < numFixBands; ++b)
        fixBandSm[b].setTargetValue (engaged ? fixBandDb[b].load() * m.fixAmount : 0.0f);
    // The headroom trim is a safety ceiling, not a flavour: it applies in
    // full whenever the fix is engaged, regardless of Fix Amount.
    fixTrimSm.setTargetValue (engaged ? fixTrimDb.load() : 0.0f);
    lowMonoSm.setTargetValue (engaged && fixLowMono.load() ? 1.0f : 0.0f);
    bodySm.setTargetValue (m.body);
    toneSm.setTargetValue (m.tone);
    airSm.setTargetValue (m.air);
    widthSm.setTargetValue (m.stereo);
    chainMixSm.setTargetValue (m.compare ? 0.0f : 1.0f);

    // Keep the pre-chain signal for the A/B mix.
    for (int ch = 0; ch < numCh; ++ch)
        dryChain.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    updateFilters (m.fixAmount, m);

    // Block-rate smoothers (the filters take their value once per block).
    for (int b = 0; b < numFixBands; ++b)
        fixBandSm[b].skip (numSamples);
    bodySm.skip (numSamples);
    toneSm.skip (numSamples);
    airSm.skip (numSamples);

    const bool dcOn = engaged && fixDc.load();
    float* L = buffer.getWritePointer (0);
    float* R = numCh > 1 ? buffer.getWritePointer (1) : L;

    // --- trim + EQ + transient shaping, one pass ---------------------------
    for (int i = 0; i < numSamples; ++i)
    {
        const float trim = juce::Decibels::decibelsToGain (fixTrimSm.getNextValue());
        float l = L[i] * trim;
        float r = R[i] * trim;

        if (dcOn)
        {
            l = dcFilters[0].processSample (l);
            r = dcFilters[1].processSample (r);
        }

        for (int f = 0; f < numFilters; ++f)
        {
            l = filters[0][f].processSample (l);
            r = filters[1][f].processSample (r);
        }

        const float tGain = transientGain (l, r, m.punch, m.transients);
        L[i] = l * tGain;
        R[i] = r * tGain;
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        for (int f = 0; f < numFilters; ++f)
            filters[ch][f].snapToZero();
        dcFilters[ch].snapToZero();
    }

    // --- saturation, oversampled when HQ is on -----------------------------
    if (m.saturate > 0.001f)
    {
        const float drive = m.saturate;
        const float g = 1.0f + 4.0f * drive;
        // Makeup tuned so a -12 dBFS sine holds its level at full drive
        // (tanh + naive peak normalisation would pump quiet material +4 dB).
        const float comp = 1.0f / (1.0f + 2.4f * drive);

        auto saturate = [drive, g, comp] (float* data, size_t n)
        {
            for (size_t i = 0; i < n; ++i)
            {
                const float x = data[i];
                data[i] = x + drive * (std::tanh (x * g) * comp - x);
            }
        };

        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(),
                                            (size_t) numCh, (size_t) numSamples);

        if (m.hq && m.oversampling > 0)
        {
            auto& os = *oversamplers[m.oversampling - 1];
            auto up = os.processSamplesUp (block);
            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
                saturate (up.getChannelPointer (ch), up.getNumSamples());
            os.processSamplesDown (block);
        }
        else
        {
            for (int ch = 0; ch < numCh; ++ch)
                saturate (buffer.getWritePointer (ch), (size_t) numSamples);
        }
    }

    // --- low-mono fix + width + chain mix ----------------------------------
    for (int i = 0; i < numSamples; ++i)
    {
        float l = L[i], r = R[i];

        const float mid  = 0.5f * (l + r);
        float side = 0.5f * (l - r);

        const float monoAmt = lowMonoSm.getNextValue();
        if (monoAmt > 0.0001f)
        {
            const float hp = sideHighPass.processSample (side);
            side += monoAmt * (hp - side);
        }
        else
        {
            sideHighPass.processSample (side);   // keep state warm
        }

        side *= 1.0f + widthSm.getNextValue() * 0.6f;

        l = mid + side;
        r = mid - side;

        const float mix = chainMixSm.getNextValue();
        L[i] = dryChain.getSample (0, i) + mix * (l - dryChain.getSample (0, i));
        R[i] = dryChain.getSample (juce::jmin (1, numCh - 1), i)
                 + mix * (r - dryChain.getSample (juce::jmin (1, numCh - 1), i));
    }
    sideHighPass.snapToZero();
}

} // namespace sourceglo
