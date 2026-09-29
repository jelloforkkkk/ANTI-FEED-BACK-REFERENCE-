#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/FeedbackSuppressorDSP.h"
#include "DSP/SpectralProcessor.h"
#include "DSP/AdaptiveFeedbackCanceller.h"

class FeedbackSuppressorAudioProcessor : public juce::AudioProcessor
{
public:
    FeedbackSuppressorAudioProcessor();
    ~FeedbackSuppressorAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Feedback Suppressor"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // For simple status indicators in the editor.
    int getActiveNotchCountForDisplay() const;
    bool hasReferenceConnected() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::vector<AdaptiveFeedbackCanceller> afcStages;
    std::vector<FeedbackSuppressorDSP> feedbackStages;
    std::vector<SpectralProcessor> spectralStages;

    // Scratch buffer holding the reference signal for the current block
    // (copied out of the aux bus once per block, reused per main channel).
    std::vector<float> referenceScratch;

    std::atomic<float>* afcEnabledParam = nullptr;
    std::atomic<float>* afcFilterLengthParam = nullptr; // choice index: 0=256,1=512,2=1024,3=2048
    std::atomic<float>* afcStepSizeParam = nullptr;
    std::atomic<float>* afcLeakageParam = nullptr;

    std::atomic<float>* feedbackEnabledParam = nullptr;
    std::atomic<float>* feedbackThresholdParam = nullptr;
    std::atomic<float>* feedbackSensitivityParam = nullptr;
    std::atomic<float>* denoiseAmountParam = nullptr;
    std::atomic<float>* dereverbAmountParam = nullptr;
    std::atomic<float>* outputGainDbParam = nullptr;

    int lastPreparedFilterLengthChoice = -1;
    void rebuildAfcStagesIfNeeded();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FeedbackSuppressorAudioProcessor)
};
