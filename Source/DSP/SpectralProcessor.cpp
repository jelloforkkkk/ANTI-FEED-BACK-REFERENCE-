#include "SpectralProcessor.h"

namespace
{
    // Periodic (DFT-even) Hann window - the right choice for STFT at 75% overlap.
    std::vector<float> makePeriodicHann(int size)
    {
        std::vector<float> w((size_t) size);
        for (int i = 0; i < size; ++i)
            w[(size_t) i] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * (float) i / (float) size);
        return w;
    }
}

void SpectralProcessor::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    inputRing.assign(fftSize, 0.0f);
    outputAccum.assign(fftSize, 0.0f);
    windowNormAccum.assign(fftSize, 0.0f);

    frameTime.assign(fftSize, 0.0f);
    fftData.assign(2 * fftSize, 0.0f);

    const int numBins = fftSize / 2 + 1;
    noiseFloor.assign((size_t) numBins, 0.0f);
    reverbEnvelope.assign((size_t) numBins, 0.0f);

    reset();
}

void SpectralProcessor::reset()
{
    std::fill(inputRing.begin(), inputRing.end(), 0.0f);
    std::fill(outputAccum.begin(), outputAccum.end(), 0.0f);
    std::fill(windowNormAccum.begin(), windowNormAccum.end(), 0.0f);
    std::fill(noiseFloor.begin(), noiseFloor.end(), 0.0f);
    std::fill(reverbEnvelope.begin(), reverbEnvelope.end(), 0.0f);
    noiseFloorInitialised = false;
    ringWritePos = 0;
    samplesAvailable = 0;
    outputReadPos = 0;
}

void SpectralProcessor::process(float* samples, int numSamples)
{
    static const std::vector<float> hann = makePeriodicHann(fftSize);
    const int latency = getLatencySamples();

    for (int i = 0; i < numSamples; ++i)
    {
        const long long t = (long long) ringWritePos; // address is just "time mod fftSize"; ringWritePos already lives mod fftSize
        inputRing[(size_t) t] = samples[i];

        ++samplesAvailable;
        if (samplesAvailable >= hopSize)
        {
            samplesAvailable = 0;
            processFrame();
        }

        // Read out the sample from `latency` samples ago, then clear that slot
        // so it's ready to accumulate fresh contributions next time round.
        const int outAddr = (int) (((long long) ringWritePos - latency % fftSize + fftSize) % fftSize);
        const float norm = windowNormAccum[(size_t) outAddr];
        samples[i] = norm > 1.0e-6f ? outputAccum[(size_t) outAddr] / norm : 0.0f;
        outputAccum[(size_t) outAddr] = 0.0f;
        windowNormAccum[(size_t) outAddr] = 0.0f;

        ringWritePos = (ringWritePos + 1) % fftSize;
    }

    juce::ignoreUnused(hann);
}

void SpectralProcessor::processFrame()
{
    static const std::vector<float> hann = makePeriodicHann(fftSize);

    // Pull the most recent fftSize samples out of the ring buffer in correct
    // time order, and apply the analysis window.
    for (int i = 0; i < fftSize; ++i)
    {
        const int addr = (int) (((long long) ringWritePos - fftSize + 1 + i + (long long) fftSize * 1024) % fftSize);
        frameTime[(size_t) i] = inputRing[(size_t) addr] * hann[(size_t) i];
    }

    // Pack as complex (imag = 0) for the forward transform.
    std::fill(fftData.begin(), fftData.end(), 0.0f);
    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) (2 * i)] = frameTime[(size_t) i];

    fft.perform(reinterpret_cast<const juce::dsp::Complex<float>*>(fftData.data()),
                reinterpret_cast<juce::dsp::Complex<float>*>(fftData.data()),
                false);

    const int numBins = fftSize / 2 + 1;

    for (int k = 0; k < numBins; ++k)
    {
        const float re = fftData[(size_t) (2 * k)];
        const float im = fftData[(size_t) (2 * k + 1)];
        const float mag = std::sqrt(re * re + im * im);
        const float phase = std::atan2(im, re);

        // --- Noise floor: minimum-statistics style tracker ---------------
        if (!noiseFloorInitialised)
            noiseFloor[(size_t) k] = mag;
        else if (mag < noiseFloor[(size_t) k])
            noiseFloor[(size_t) k] = mag;
        else
            noiseFloor[(size_t) k] += (mag - noiseFloor[(size_t) k]) * noiseRiseRate;

        // --- Reverb tail: decaying high-water mark ------------------------
        const float predictedTail = reverbEnvelope[(size_t) k] * reverbDecay;
        reverbEnvelope[(size_t) k] = juce::jmax(mag, predictedTail);

        // --- Combine into a subtractive gain mask -------------------------
        const float noiseSubtraction  = denoiseAmount   * overSubtraction * noiseFloor[(size_t) k];
        const float reverbSubtraction = dereverbAmount  * predictedTail;

        const float target = juce::jmax(mag - noiseSubtraction - reverbSubtraction,
                                         spectralFloor * mag);

        const float mask = mag > 1.0e-9f ? target / mag : 1.0f;
        const float newMag = mag * mask;

        fftData[(size_t) (2 * k)]     = newMag * std::cos(phase);
        fftData[(size_t) (2 * k + 1)] = newMag * std::sin(phase);
    }
    noiseFloorInitialised = true;

    // Mirror to negative frequencies so the inverse transform is real-valued.
    for (int k = 1; k < fftSize / 2; ++k)
    {
        const int mirror = fftSize - k;
        fftData[(size_t) (2 * mirror)]     =  fftData[(size_t) (2 * k)];
        fftData[(size_t) (2 * mirror + 1)] = -fftData[(size_t) (2 * k + 1)];
    }

    fft.perform(reinterpret_cast<const juce::dsp::Complex<float>*>(fftData.data()),
                reinterpret_cast<juce::dsp::Complex<float>*>(fftData.data()),
                true);
    // NOTE: assumes JUCE's inverse FFT includes the 1/N scaling. If output
    // level is off when you first build this, that's the first thing to check
    // (juce::dsp::FFT documentation / a quick unity-gain bypass test will tell you).

    // Overlap-add the (real part of the) synthesised frame back in, and
    // accumulate the window sum for weighted-overlap-add normalisation.
    for (int i = 0; i < fftSize; ++i)
    {
        const int addr = (int) (((long long) ringWritePos - fftSize + 1 + i + (long long) fftSize * 1024) % fftSize);
        outputAccum[(size_t) addr]     += fftData[(size_t) (2 * i)];
        windowNormAccum[(size_t) addr] += hann[(size_t) i];
    }
}
