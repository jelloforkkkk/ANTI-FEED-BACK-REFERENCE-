#include "FeedbackSuppressorDSP.h"

void FeedbackSuppressorDSP::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    circularBuffer.assign(fftSize, 0.0f);
    fftWorkspace.assign(2 * fftSize, 0.0f);
    magnitude.assign(fftSize / 2, 0.0f);
    persistentEnergy.assign(fftSize / 2, 0.0f);

    for (auto& slot : notchSlots)
    {
        slot.filter.prepare(spec);
        slot.active = false;
        slot.persistenceCount = 0;
        slot.releaseCountdown = 0;
        slot.filter.coefficients = juce::dsp::IIR::Coefficients<float>::makeAllPass(sampleRate, 1000.0f);
    }

    reset();
}

void FeedbackSuppressorDSP::reset()
{
    writePos = 0;
    samplesSinceLastAnalysis = 0;
    std::fill(circularBuffer.begin(), circularBuffer.end(), 0.0f);
    std::fill(persistentEnergy.begin(), persistentEnergy.end(), 0.0f);
    for (auto& slot : notchSlots)
    {
        slot.filter.reset();
        slot.active = false;
    }
}

int FeedbackSuppressorDSP::getNumActiveNotches() const
{
    int count = 0;
    for (auto& s : notchSlots)
        if (s.active) ++count;
    return count;
}

void FeedbackSuppressorDSP::updateNotchCoefficients(NotchSlot& slot)
{
    // High-Q notch: narrow enough to leave the surrounding tone untouched.
    const float q = 25.0f + sensitivity * 25.0f; // 25..50
    slot.filter.coefficients = juce::dsp::IIR::Coefficients<float>::makeNotch(
        sampleRate, juce::jlimit(20.0f, (float) (sampleRate * 0.49), slot.frequencyHz), q);
}

void FeedbackSuppressorDSP::process(float* samples, int numSamples)
{
    // 1) Run the (possibly active) notch filters in series over this block.
    if (enabled)
    {
        for (auto& slot : notchSlots)
        {
            if (!slot.active)
                continue;

            for (int i = 0; i < numSamples; ++i)
                samples[i] = slot.filter.processSample(samples[i]);
        }
    }

    // 2) Feed the (post-notch) signal into the analysis ring buffer.
    for (int i = 0; i < numSamples; ++i)
    {
        circularBuffer[(size_t) writePos] = samples[i];
        writePos = (writePos + 1) % fftSize;
    }

    samplesSinceLastAnalysis += numSamples;
    if (enabled && samplesSinceLastAnalysis >= hopSize)
    {
        samplesSinceLastAnalysis = 0;
        runAnalysis();
    }
}

void FeedbackSuppressorDSP::runAnalysis()
{
    // Copy the ring buffer out in correct time order, windowed, into the FFT workspace.
    std::fill(fftWorkspace.begin(), fftWorkspace.end(), 0.0f);
    for (int i = 0; i < fftSize; ++i)
        fftWorkspace[(size_t) i] = circularBuffer[(size_t) ((writePos + i) % fftSize)];

    window.multiplyWithWindowingTable(fftWorkspace.data(), fftSize);
    fft.performFrequencyOnlyForwardTransform(fftWorkspace.data());

    const int numBins = fftSize / 2;
    for (int i = 0; i < numBins; ++i)
        magnitude[(size_t) i] = fftWorkspace[(size_t) i];

    // Persistence-weighted candidate detection: a feedback tone sits well
    // above its local spectral neighbourhood AND stays there frame after
    // frame, unlike a transient or a wobbling vocal formant.
    const int guard = 2;     // bins skipped either side of the candidate (avoid self-comparison)
    const int span  = 12;    // neighbourhood width used to estimate the "floor" around a bin

    const float persistDecay = 0.85f;
    const float persistFramesNeeded = juce::jmap(sensitivity, 1.0f, 0.35f, 12.0f, 3.0f); // frames

    for (int bin = span; bin < numBins - span; ++bin)
    {
        float neighbourSum = 0.0f;
        int neighbourCount = 0;
        for (int k = -span; k <= span; ++k)
        {
            if (std::abs(k) <= guard) continue;
            neighbourSum += magnitude[(size_t) (bin + k)];
            ++neighbourCount;
        }
        const float floorMag = neighbourSum / juce::jmax(1, neighbourCount);
        const float binMag = magnitude[(size_t) bin];

        const float binDb   = juce::Decibels::gainToDecibels(binMag + 1.0e-8f);
        const float floorDb = juce::Decibels::gainToDecibels(floorMag + 1.0e-8f);

        const bool isCandidate = (binDb - floorDb) > threshold;

        // Exponential-ish persistence counter per bin.
        persistentEnergy[(size_t) bin] = isCandidate
            ? persistentEnergy[(size_t) bin] * persistDecay + 1.0f
            : persistentEnergy[(size_t) bin] * persistDecay;

        if (persistentEnergy[(size_t) bin] >= persistFramesNeeded)
        {
            const float freqHz = (float) bin * (float) sampleRate / (float) fftSize;

            // Is an existing slot already covering this frequency (within ~1.5%)?
            bool alreadyCovered = false;
            for (auto& slot : notchSlots)
            {
                if (slot.active && std::abs(slot.frequencyHz - freqHz) / freqHz < 0.015f)
                {
                    alreadyCovered = true;
                    slot.releaseCountdown = 60; // keep it alive
                    break;
                }
            }

            if (!alreadyCovered && getNumActiveNotches() < maxActiveNotches)
            {
                // Grab a free slot.
                for (auto& slot : notchSlots)
                {
                    if (!slot.active)
                    {
                        slot.active = true;
                        slot.frequencyHz = freqHz;
                        slot.releaseCountdown = 60; // ~ a few seconds of hops before we consider release
                        updateNotchCoefficients(slot);
                        break;
                    }
                }
            }
        }
    }

    // Release notches whose frequency has gone quiet for a while, so the
    // filter doesn't permanently colour the mix once the howl is gone.
    for (auto& slot : notchSlots)
    {
        if (!slot.active) continue;

        const int bin = (int) std::round(slot.frequencyHz * fftSize / sampleRate);
        const bool stillRinging = bin >= 0 && bin < numBins && persistentEnergy[(size_t) bin] > 1.0f;

        if (stillRinging)
            slot.releaseCountdown = 60;
        else
            --slot.releaseCountdown;

        if (slot.releaseCountdown <= 0)
            slot.active = false;
    }
}
