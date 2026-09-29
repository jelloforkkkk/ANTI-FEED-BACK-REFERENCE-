#include "PluginProcessor.h"
#include "PluginEditor.h"

FeedbackSuppressorAudioProcessor::FeedbackSuppressorAudioProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::mono(), true)
                          .withOutput("Output", juce::AudioChannelSet::mono(), true)
                          .withInput("Reference", juce::AudioChannelSet::mono(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    afcEnabledParam      = apvts.getRawParameterValue("afcEnabled");
    afcFilterLengthParam = apvts.getRawParameterValue("afcFilterLength");
    afcStepSizeParam     = apvts.getRawParameterValue("afcStepSize");
    afcLeakageParam      = apvts.getRawParameterValue("afcLeakage");

    feedbackEnabledParam     = apvts.getRawParameterValue("feedbackEnabled");
    feedbackThresholdParam   = apvts.getRawParameterValue("feedbackThreshold");
    feedbackSensitivityParam = apvts.getRawParameterValue("feedbackSensitivity");
    denoiseAmountParam       = apvts.getRawParameterValue("denoiseAmount");
    dereverbAmountParam      = apvts.getRawParameterValue("dereverbAmount");
    outputGainDbParam        = apvts.getRawParameterValue("outputGainDb");
}

juce::AudioProcessorValueTreeState::ParameterLayout FeedbackSuppressorAudioProcessor::createParameterLayout()
{
    using Range = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "afcEnabled", "Adaptive Cancellation (needs Reference input)", true));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "afcFilterLength", "AFC Filter Length",
        juce::StringArray { "256 taps (~5ms)", "512 taps (~11ms)", "1024 taps (~21ms)", "2048 taps (~43ms)" },
        2));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "afcStepSize", "AFC Adaptation Speed", Range(0.001f, 0.08f, 0.001f), 0.02f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "afcLeakage", "AFC Leakage", Range(0.0f, 0.001f, 0.00001f), 0.00005f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "feedbackEnabled", "Notch Safety Net", true));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "feedbackThreshold", "Feedback Threshold", Range(2.0f, 20.0f, 0.1f), 8.0f, "dB"));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "feedbackSensitivity", "Feedback Sensitivity", Range(0.0f, 1.0f, 0.01f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "denoiseAmount", "Denoise", Range(0.0f, 1.0f, 0.01f), 0.3f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "dereverbAmount", "De-reverb", Range(0.0f, 1.0f, 0.01f), 0.3f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "outputGainDb", "Output Gain", Range(-24.0f, 24.0f, 0.1f), 0.0f, "dB"));

    return { params.begin(), params.end() };
}

void FeedbackSuppressorAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const int numChannels = juce::jmax(getMainBusNumInputChannels(), 1);

    feedbackStages.assign((size_t) numChannels, FeedbackSuppressorDSP());
    spectralStages.assign((size_t) numChannels, SpectralProcessor());
    afcStages.assign((size_t) numChannels, AdaptiveFeedbackCanceller());
    referenceScratch.assign((size_t) samplesPerBlock, 0.0f);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 1 };

    for (auto& stage : feedbackStages) stage.prepare(spec);
    for (auto& stage : spectralStages) stage.prepare(spec);

    lastPreparedFilterLengthChoice = -1; // force AFC (re)prepare below
    rebuildAfcStagesIfNeeded();

    if (!spectralStages.empty())
        setLatencySamples(spectralStages.front().getLatencySamples());
}

void FeedbackSuppressorAudioProcessor::rebuildAfcStagesIfNeeded()
{
    static const int tapCounts[] = { 256, 512, 1024, 2048 };
    const int choice = juce::jlimit(0, 3, (int) std::round(afcFilterLengthParam->load()));

    if (choice != lastPreparedFilterLengthChoice)
    {
        lastPreparedFilterLengthChoice = choice;
        for (auto& stage : afcStages)
            stage.prepare(tapCounts[choice]);
    }
}

bool FeedbackSuppressorAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != layouts.getMainOutputChannelSet())
        return false;
    if (layouts.getMainInputChannelSet().isDisabled())
        return false;

    // Reference bus (input bus index 1) may be mono or disabled - never stereo.
    const auto refSet = layouts.getChannelSet(true, 1);
    if (!refSet.isDisabled() && refSet != juce::AudioChannelSet::mono())
        return false;

    return true;
}

void FeedbackSuppressorAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    rebuildAfcStagesIfNeeded();

    auto mainBlock = getBusBuffer(buffer, true, 0);
    auto refBlock  = getBusBuffer(buffer, true, 1);

    const int numSamples = mainBlock.getNumSamples();
    const bool afcEnabled = afcEnabledParam->load() > 0.5f;
    const bool referenceAvailable = refBlock.getNumChannels() > 0;
    const bool fbEnabled = feedbackEnabledParam->load() > 0.5f;

    const float* referenceData = referenceAvailable ? refBlock.getReadPointer(0) : nullptr;

    for (int ch = 0; ch < mainBlock.getNumChannels() && ch < (int) feedbackStages.size(); ++ch)
    {
        auto* data = mainBlock.getWritePointer(ch);

        if (afcEnabled && referenceAvailable)
        {
            afcStages[(size_t) ch].setEnabled(true);
            afcStages[(size_t) ch].setStepSize(afcStepSizeParam->load());
            afcStages[(size_t) ch].setLeakage(afcLeakageParam->load());
            afcStages[(size_t) ch].process(data, referenceData, numSamples);
        }

        feedbackStages[(size_t) ch].setEnabled(fbEnabled);
        feedbackStages[(size_t) ch].setThresholdDb(feedbackThresholdParam->load());
        feedbackStages[(size_t) ch].setSensitivity(feedbackSensitivityParam->load());
        feedbackStages[(size_t) ch].process(data, numSamples);

        spectralStages[(size_t) ch].setDenoiseAmount(denoiseAmountParam->load());
        spectralStages[(size_t) ch].setDereverbAmount(dereverbAmountParam->load());
        spectralStages[(size_t) ch].process(data, numSamples);
    }

    mainBlock.applyGain(juce::Decibels::decibelsToGain(outputGainDbParam->load()));
}

bool FeedbackSuppressorAudioProcessor::hasReferenceConnected() const
{
    auto* bus = getBus(true, 1);
    return bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() > 0;
}

int FeedbackSuppressorAudioProcessor::getActiveNotchCountForDisplay() const
{
    if (feedbackStages.empty())
        return 0;
    return feedbackStages.front().getNumActiveNotches();
}

juce::AudioProcessorEditor* FeedbackSuppressorAudioProcessor::createEditor()
{
    return new FeedbackSuppressorAudioProcessorEditor(*this);
}

void FeedbackSuppressorAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
}

void FeedbackSuppressorAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

// This creates the actual plugin instance.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FeedbackSuppressorAudioProcessor();
}
