#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class FeedbackSuppressorAudioProcessorEditor : public juce::AudioProcessorEditor,
                                                private juce::Timer
{
public:
    explicit FeedbackSuppressorAudioProcessorEditor(FeedbackSuppressorAudioProcessor&);
    ~FeedbackSuppressorAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    FeedbackSuppressorAudioProcessor& processor;

    juce::ToggleButton afcEnabledButton { "Adaptive Cancellation" };
    juce::ComboBox afcFilterLengthBox;
    juce::Slider afcStepSizeSlider, afcLeakageSlider;
    juce::Label afcStepSizeLabel { {}, "Adapt Speed" },
                afcLeakageLabel { {}, "Leakage" },
                afcFilterLengthLabel { {}, "Filter Length" },
                referenceStatusLabel { {}, "Reference: not connected" };

    juce::ToggleButton feedbackEnabledButton { "Notch Safety Net" };

    juce::Slider feedbackThresholdSlider, feedbackSensitivitySlider,
                 denoiseSlider, dereverbSlider, outputGainSlider;

    juce::Label feedbackThresholdLabel { {}, "Threshold" },
                feedbackSensitivityLabel { {}, "Sensitivity" },
                denoiseLabel { {}, "Denoise" },
                dereverbLabel { {}, "De-reverb" },
                outputGainLabel { {}, "Output" },
                activeNotchesLabel { {}, "Active notches: 0" };

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<Attachment> thresholdAttachment, sensitivityAttachment,
                                 denoiseAttachment, dereverbAttachment, outputGainAttachment,
                                 afcStepSizeAttachment, afcLeakageAttachment;
    std::unique_ptr<ButtonAttachment> feedbackEnabledAttachment, afcEnabledAttachment;
    std::unique_ptr<ComboAttachment> afcFilterLengthAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FeedbackSuppressorAudioProcessorEditor)
};
