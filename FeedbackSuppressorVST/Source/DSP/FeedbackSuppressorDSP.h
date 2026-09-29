#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>

/**
    Classic "feedback destroyer" approach (the same family of technique used
    by hardware units like the Sabine FBX / dbx AFS): run a small analysis FFT
    continuously, look for spectral peaks that are narrow AND persistent
    across many frames (the fingerprint of an acoustic feedback ring, as
    opposed to a musical note or vocal formant, which wobbles in level and
    pitch), then clamp that frequency with a very narrow, high-Q notch filter.

    This is NOT a reverse-engineering of any commercial product's algorithm -
    it's the well-documented, decades-old approach to automatic feedback
    suppression, reimplemented from scratch.
*/
class FeedbackSuppressorDSP
{
public:
    FeedbackSuppressorDSP() = default;

    void prepare(const juce::dsp::ProcessSpec& spec);
    void reset();

    /** In-place processing of one channel's block. Call once per channel. */
    void process(float* samples, int numSamples);

    // --- Parameters ---
    void setEnabled(bool shouldBeEnabled) { enabled = shouldBeEnabled; }
    void setThresholdDb(float thresholdDb) { threshold = thresholdDb; }
    void setSensitivity(float newSensitivity) { sensitivity = juce::jlimit(0.0f, 1.0f, newSensitivity); }
    void setMaxNotches(int n) { maxActiveNotches = juce::jlimit(1, (int) notchSlots.size(), n); }

    int getNumActiveNotches() const;

private:
    static constexpr int fftOrder = 11;               // 2048-point FFT
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int hopSize  = fftSize / 4;
    static constexpr int numSlots = 8;                 // simultaneous notches

    struct NotchSlot
    {
        bool   active = false;
        float  frequencyHz = 0.0f;
        int    persistenceCount = 0;   // frames the peak has been seen
        int    releaseCountdown = 0;   // frames of low energy before release
        juce::dsp::IIR::Filter<float> filter;
    };

    void runAnalysis();
    void updateNotchCoefficients(NotchSlot& slot);

    bool  enabled = true;
    float threshold = 8.0f;     // dB a bin must exceed its local neighbourhood to be a candidate
    float sensitivity = 0.5f;   // 0 = slow/cautious, 1 = fast/aggressive locate & release

    double sampleRate = 44100.0;

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { fftSize, juce::dsp::WindowingFunction<float>::hann };

    std::vector<float> circularBuffer;
    int writePos = 0;
    int samplesSinceLastAnalysis = 0;

    std::vector<float> fftWorkspace;      // 2 * fftSize (interleaved real/imag for JUCE FFT)
    std::vector<float> magnitude;         // fftSize / 2 bins
    std::vector<float> persistentEnergy;  // smoothed per-bin energy for persistence tracking

    std::array<NotchSlot, numSlots> notchSlots;
    int maxActiveNotches = numSlots;
};
