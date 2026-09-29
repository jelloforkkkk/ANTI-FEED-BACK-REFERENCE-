#include "AdaptiveFeedbackCanceller.h"

void AdaptiveFeedbackCanceller::prepare(int filterLengthInTaps)
{
    numTaps = filterLengthInTaps;
    weights.assign((size_t) numTaps, 0.0f);
    refHistory.assign((size_t) numTaps, 0.0f);
    reset();
}

void AdaptiveFeedbackCanceller::reset()
{
    std::fill(weights.begin(), weights.end(), 0.0f);
    std::fill(refHistory.begin(), refHistory.end(), 0.0f);
    historyPos = 0;
    referenceEnergy = 0.0f;
}

void AdaptiveFeedbackCanceller::process(float* micSamples, const float* referenceSamples, int numSamples)
{
    if (!enabled || weights.empty())
        return;

    const int N = numTaps;

    for (int n = 0; n < numSamples; ++n)
    {
        const float refIn = referenceSamples[n];

        // Push the new reference sample into the circular history, updating
        // the running energy sum incrementally (subtract the sample that's
        // about to be overwritten, add the new one).
        const float oldSample = refHistory[(size_t) historyPos];
        referenceEnergy += refIn * refIn - oldSample * oldSample;
        if (referenceEnergy < 0.0f) referenceEnergy = 0.0f; // guard tiny negative drift from float error
        refHistory[(size_t) historyPos] = refIn;

        // --- Predict the feedback component via FIR convolution ---------
        // weights[k] pairs with the reference sample k steps in the past.
        float prediction = 0.0f;
        int idx = historyPos;
        for (int k = 0; k < N; ++k)
        {
            prediction += weights[(size_t) k] * refHistory[(size_t) idx];
            idx = (idx == 0) ? (N - 1) : (idx - 1);
        }

        // --- Cancel: residual is what's left after removing the predicted
        //     leaked reference from the mic signal. This residual is both
        //     the plugin's output AND the error signal that drives adaptation.
        const float micIn = micSamples[n];
        const float error = micIn - prediction;
        micSamples[n] = error;

        // --- NLMS weight update, normalised by reference energy ----------
        const float normalisedStep = mu * error / (referenceEnergy + epsilon);
        idx = historyPos;
        for (int k = 0; k < N; ++k)
        {
            float& w = weights[(size_t) k];
            w = w * (1.0f - leakage) + normalisedStep * refHistory[(size_t) idx];
            idx = (idx == 0) ? (N - 1) : (idx - 1);
        }

        historyPos = (historyPos + 1) % N;
    }
}
