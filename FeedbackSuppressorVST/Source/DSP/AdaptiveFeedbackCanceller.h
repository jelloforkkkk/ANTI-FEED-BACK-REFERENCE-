#pragma once
#include <vector>
#include <cmath>

/**
    Normalised LMS adaptive feedback canceller.

    Models the acoustic path from loudspeaker (reference signal) to a given
    microphone as an FIR filter, continuously adapts that filter to match
    the real room, predicts the leaked copy of the reference in the mic
    signal, and subtracts it. This is the same family of algorithm used in
    acoustic echo cancellation, applied to the feedback-cancellation problem.

    IMPORTANT - closed-loop bias: because the reference signal (main mix)
    typically already contains this same mic's contribution, the adaptive
    filter can be biased toward partially cancelling the wanted direct
    vocal sound too, not just the room's feedback path. The standard
    mitigation - keep the step size small and rely on the fact that the
    room path is fixed while singing is not, so the *persistent* correlation
    the filter locks onto is the room, not the voice - is implemented here,
    but this remains the fundamental trade-off of reference-based AFC.
    If you hear the canceller ducking or thinning out the choir's direct
    sound, reduce stepSize first.

    One instance per microphone channel (each mic has its own acoustic path
    to the loudspeakers).
*/
class AdaptiveFeedbackCanceller
{
public:
    void prepare(int filterLengthInTaps);
    void reset();

    void setStepSize(float newMu) { mu = newMu; }
    void setLeakage(float newLeak) { leakage = newLeak; }
    void setEnabled(bool shouldBeEnabled) { enabled = shouldBeEnabled; }

    /**
        In-place: micSamples is overwritten with the cancelled (residual)
        signal. referenceSamples is read-only (the signal actually driving
        the loudspeakers). Both buffers must be numSamples long.
    */
    void process(float* micSamples, const float* referenceSamples, int numSamples);

private:
    int numTaps = 1024;
    bool enabled = true;

    float mu = 0.02f;        // adaptation step size - keep small (0.005-0.05 typical)
    float leakage = 0.00005f; // per-sample weight leak, guards against coefficient drift
    static constexpr float epsilon = 1.0e-6f; // regularisation to avoid divide-by-zero

    std::vector<float> weights;      // adaptive filter taps
    std::vector<float> refHistory;   // circular buffer of the last numTaps reference samples
    int historyPos = 0;
    float referenceEnergy = 0.0f;    // running sum of refHistory^2, kept incrementally
};
