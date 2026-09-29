#include "PluginEditor.h"

namespace
{
    void setupRotary(juce::Slider& s)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    }
}

FeedbackSuppressorAudioProcessorEditor::FeedbackSuppressorAudioProcessorEditor(FeedbackSuppressorAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processor(p)
{
    // --- AFC row ---------------------------------------------------------
    addAndMakeVisible(afcEnabledButton);
    afcEnabledAttachment = std::make_unique<ButtonAttachment>(processor.apvts, "afcEnabled", afcEnabledButton);

    afcFilterLengthBox.addItemList({ "256 taps (~5ms)", "512 taps (~11ms)", "1024 taps (~21ms)", "2048 taps (~43ms)" }, 1);
    addAndMakeVisible(afcFilterLengthBox);
    afcFilterLengthAttachment = std::make_unique<ComboAttachment>(processor.apvts, "afcFilterLength", afcFilterLengthBox);

    for (auto* slider : { &afcStepSizeSlider, &afcLeakageSlider,
                           &feedbackThresholdSlider, &feedbackSensitivitySlider,
                           &denoiseSlider, &dereverbSlider, &outputGainSlider })
    {
        setupRotary(*slider);
        addAndMakeVisible(slider);
    }

    for (auto* label : { &afcStepSizeLabel, &afcLeakageLabel, &afcFilterLengthLabel, &referenceStatusLabel,
                          &feedbackThresholdLabel, &feedbackSensitivityLabel,
                          &denoiseLabel, &dereverbLabel, &outputGainLabel, &activeNotchesLabel })
    {
        label->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(label);
    }

    addAndMakeVisible(feedbackEnabledButton);
    feedbackEnabledAttachment = std::make_unique<ButtonAttachment>(
        processor.apvts, "feedbackEnabled", feedbackEnabledButton);

    afcStepSizeAttachment = std::make_unique<Attachment>(processor.apvts, "afcStepSize", afcStepSizeSlider);
    afcLeakageAttachment  = std::make_unique<Attachment>(processor.apvts, "afcLeakage", afcLeakageSlider);

    thresholdAttachment   = std::make_unique<Attachment>(processor.apvts, "feedbackThreshold", feedbackThresholdSlider);
    sensitivityAttachment = std::make_unique<Attachment>(processor.apvts, "feedbackSensitivity", feedbackSensitivitySlider);
    denoiseAttachment     = std::make_unique<Attachment>(processor.apvts, "denoiseAmount", denoiseSlider);
    dereverbAttachment    = std::make_unique<Attachment>(processor.apvts, "dereverbAmount", dereverbSlider);
    outputGainAttachment  = std::make_unique<Attachment>(processor.apvts, "outputGainDb", outputGainSlider);

    setSize(620, 430);
    startTimerHz(4);
}

FeedbackSuppressorAudioProcessorEditor::~FeedbackSuppressorAudioProcessorEditor()
{
    stopTimer();
}

void FeedbackSuppressorAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff20232a));

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(22.0f, juce::Font::bold));
    g.drawText("Feedback Suppressor", getLocalBounds().removeFromTop(40),
               juce::Justification::centred);
}

void FeedbackSuppressorAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(20);
    area.removeFromTop(40); // title

    // --- AFC section -------------------------------------------------
    auto afcToggleRow = area.removeFromTop(30);
    afcEnabledButton.setBounds(afcToggleRow.removeFromLeft(220));
    afcFilterLengthLabel.setBounds(afcToggleRow.removeFromLeft(90));
    afcFilterLengthBox.setBounds(afcToggleRow.removeFromLeft(160).reduced(0, 3));
    referenceStatusLabel.setBounds(afcToggleRow);

    area.removeFromTop(6);

    auto layoutKnob = [&](juce::Rectangle<int> col, juce::Slider& slider, juce::Label& label)
    {
        auto labelArea = col.removeFromTop(20);
        label.setBounds(labelArea);
        slider.setBounds(col);
    };

    auto afcKnobRow = area.removeFromTop(110);
    const int afcKnobWidth = afcKnobRow.getWidth() / 2;
    layoutKnob(afcKnobRow.removeFromLeft(afcKnobWidth), afcStepSizeSlider, afcStepSizeLabel);
    layoutKnob(afcKnobRow.removeFromLeft(afcKnobWidth), afcLeakageSlider, afcLeakageLabel);

    area.removeFromTop(14);

    // --- Notch/denoise/dereverb section --------------------------------
    auto toggleRow = area.removeFromTop(30);
    feedbackEnabledButton.setBounds(toggleRow.withSizeKeepingCentre(220, 26));

    area.removeFromTop(6);

    const int numKnobs = 5;
    const int knobWidth = area.getWidth() / numKnobs;
    auto knobRow = area.removeFromTop(130);

    layoutKnob(knobRow.removeFromLeft(knobWidth), feedbackThresholdSlider, feedbackThresholdLabel);
    layoutKnob(knobRow.removeFromLeft(knobWidth), feedbackSensitivitySlider, feedbackSensitivityLabel);
    layoutKnob(knobRow.removeFromLeft(knobWidth), denoiseSlider, denoiseLabel);
    layoutKnob(knobRow.removeFromLeft(knobWidth), dereverbSlider, dereverbLabel);
    layoutKnob(knobRow.removeFromLeft(knobWidth), outputGainSlider, outputGainLabel);

    area.removeFromTop(8);
    activeNotchesLabel.setBounds(area.removeFromTop(24));
}

void FeedbackSuppressorAudioProcessorEditor::timerCallback()
{
    activeNotchesLabel.setText("Active notches: " + juce::String(processor.getActiveNotchCountForDisplay()),
                                juce::dontSendNotification);
    referenceStatusLabel.setText(processor.hasReferenceConnected() ? "Reference: connected"
                                                                    : "Reference: not connected",
                                  juce::dontSendNotification);
}
