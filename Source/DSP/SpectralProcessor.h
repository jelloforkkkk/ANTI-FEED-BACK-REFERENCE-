#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>

/**
    STFT-based denoiser + de-reverberator.

    Denoise: classic minimum-statistics noise floor tracking per bin, then
    spectral subtraction with an over-subtraction factor and a spectral
    floor (Berouti-style), which is the textbook approach behind most
    "denoise" plugins.

    De-reverb: tracks a per-bin decaying "high-water mark" of recent energy
    (an exponential model of a reverberant tail) and subtracts a portion of
    the *predicted* leftover tail from the current frame. This is a
    simplified relative of spectral-subtraction dereverberation algorithms
    (e.g. Lebart et al., Habets) - not a reimplementation of any specific
    commercial product.

    Runs single-channel; instantiate one per channel.
*/
class SpectralProcessor
{
public:
    SpectralProcessor() = default;

    void prepare(const juce::dsp::ProcessSpec& spec);
    void reset();

    /** Processes numSamples in place. Introduces latency of (fftSize - hopSize) samples. */
    void process(float* samples, int numSamples);

    int getLatencySamples() const { return fftSize - hopSize; }

    void setDenoiseAmount(float amount01) { denoiseAmount = juce::jlimit(0.0f, 1.0f, amount01); }
    void setDereverbAmount(float amount01) { dereverbAmount = juce::jlimit(0.0f, 1.0f, amount01); }

private:
    static constexpr int fftOrder = 11;              // 2048
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int overlapFactor = 4;           // 75% overlap
    static constexpr int hopSize  = fftSize / overlapFactor;

    void processFrame();

    double sampleRate = 44100.0;

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { fftSize, juce::dsp::WindowingFunction<float>::hann, false };

    // Input ring buffer (time domain) and output overlap-add accumulator.
    std::vector<float> inputRing;
    std::vector<float> outputAccum;
    std::vector<float> windowNormAccum;   // sum of squared windows, for correct OLA reconstruction
    int ringWritePos = 0;
    int samplesAvailable = 0;             // how many new samples are waiting for a hop
    int outputReadPos = 0;

    std::vector<float> frameTime;         // fftSize, windowed time-domain frame
    std::vector<float> fftData;           // 2 * fftSize complex interleaved workspace

    std::vector<float> noiseFloor;        // per-bin, numBins
    std::vector<float> reverbEnvelope;    // per-bin, numBins
    bool noiseFloorInitialised = false;

    float denoiseAmount = 0.0f;
    float dereverbAmount = 0.0f;

    static constexpr float overSubtraction = 2.0f;    // Berouti oversubtraction factor
    static constexpr float spectralFloor   = 0.05f;   // keep at least 5% of original magnitude
    static constexpr float noiseRiseRate   = 0.02f;   // how fast the floor estimate is allowed to climb
    static constexpr float reverbDecay     = 0.72f;   // per-hop decay of the tracked reverberant tail
};
