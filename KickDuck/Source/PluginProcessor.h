#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

#define U8(s) juce::String::fromUTF8 (s)

namespace kd
{
struct Ring
{
    std::vector<float> d;
    int mask = 0, w = 0;
    void init (int n)
    {
        int s = 1;
        while (s < n) s <<= 1;
        d.assign ((size_t) s, 0.0f);
        mask = s - 1;
        w = 0;
    }
    void push (float x) { d[(size_t) w] = x; w = (w + 1) & mask; }
    float read (int delay) const { return d[(size_t) ((w - 1 - delay) & mask)]; }
    int size() const { return mask + 1; }
};

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void setLowpass (double fs, float fc)
    {
        fc = juce::jlimit (10.0f, (float) (fs * 0.45), fc);
        const float w = juce::MathConstants<float>::twoPi * fc / (float) fs;
        const float cs = std::cos (w), sn = std::sin (w);
        const float alpha = sn / (2.0f * 0.7071f);
        const float a0 = 1.0f + alpha;
        b0 = (1.0f - cs) * 0.5f / a0;
        b1 = (1.0f - cs) / a0;
        b2 = b0;
        a1 = -2.0f * cs / a0;
        a2 = (1.0f - alpha) / a0;
    }
    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0.0f; }
};
} // namespace kd

class KickDuckProcessor : public juce::AudioProcessor,
                          private juce::AudioProcessorValueTreeState::Listener
{
public:
    static constexpr float kLookaheadMs = 14.0f;
    static constexpr int kCurvePoints = 16;
    static constexpr int kScopePoints = 480;
    static constexpr int kZoomPoints = 400;
    static constexpr int kMaxSegments = 4;

    KickDuckProcessor();
    ~KickDuckProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "KickDuck"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- Auto-fase (llamar desde el hilo de mensajes) ---
    void requestAutoPhase();
    void undoAutoPhase();
    void pollAutoPhase();
    juce::String getAutoPhaseStatus() const;

    // --- Copiar curva del kick ---
    void requestCopyCurve();
    juce::String getCopyStatus() const;

    juce::AudioProcessorValueTreeState apvts;

    // Datos para la interfaz (escritos por el hilo de audio)
    std::array<std::atomic<float>, kCurvePoints> curve;
    std::array<std::atomic<float>, kScopePoints> scopeKick, scopeBass, scopeDepth;
    std::atomic<int> scopePos { 0 };
    std::array<std::atomic<float>, kZoomPoints> zoomKick, zoomBass;
    std::atomic<float> correlation { 0.0f };
    std::atomic<bool> corrValid { false };
    std::atomic<float> kickLevel { 0.0f };
    std::atomic<int> autoState { 0 }; // 0 libre,1 capturando,2 listo,3 buscando,4 ok,5 sin senal
    std::atomic<float> autoScore { 0.0f };
    std::atomic<int> capShown { 0 };
    std::atomic<int> copyState { 0 };     // 0 libre,1 copiando,2 listo,3 sin senal,4 aplicado
    std::atomic<int> copyHitsShown { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String&, float) override;
    int computeLatency() const;
    void storeCapturedSegment();
    void finishCapture();
    void startAutoPhaseSearch();
    void finishCopy();
    void resetDspState();

    double sr = 44100.0;
    int lookaheadSamples = 0;
    int ringSize = 0;

    std::atomic<float> *pMode, *pCurveMode, *pAmount, *pRelease, *pThr, *pMix, *pAttack, *pHold,
        *pLp, *pFull, *pPhase, *pDelay, *pPol, *pSens, *pSmooth;

    kd::Ring ringIn[2], ringKick, rawKick, rawBass;
    juce::dsp::LinkwitzRileyFilter<float> lr[2];
    float lastLp = -1.0f;
    float apX[2] {}, apY[2] {};
    kd::Biquad lpKickRaw, lpBassRaw, lpKickCorr, lpBassCorr;

    // detector
    float env = 0.0f, envRel = 0.0f;
    bool armed = true;
    int gap = 0, minGap = 0;

    // curva de ducking
    int t = -1;
    float startDepth = 0.0f, depthSm = 0.0f, smoothCoef = 0.0f;

    // correlacion
    bool corrIdle = true;
    int corrWait = 0, corrLen = 0, winSamples = 0, zPos = 0;
    double sxy = 0, sxx = 0, syy = 0;
    std::vector<float> zk, zb;

    // seguir kick y copiar curva
    float envF = 0.0f, cpEnv = 0.0f;
    int cpPos = -1, cpLen = 0, cpHits = 0, cpElapsed = 0;
    bool wasSplit = false;
    int tailSkip = 0;
    float cpBin[kCurvePoints] {}, cpAcc[kCurvePoints] {};
    std::atomic<bool> cpActive { false }, cpReset { false };

    // scope
    int spp = 1, scopeCount = 0;
    float pkKick = 0, pkBass = 0, pkDepth = 0;

    // captura para auto-fase
    std::atomic<bool> capActive { false }, capReset { false };
    int capCount = 0, capElapsed = 0, capWait = -1, capAfter = 0, capPad = 0, capLen = 0;
    std::vector<float> capKick[kMaxSegments], capBass[kMaxSegments];

    struct Undo { bool valid = false; float pol = 0, phase = 0, delay = 0; } undo;
    std::shared_ptr<std::atomic<bool>> alive { std::make_shared<std::atomic<bool>> (true) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickDuckProcessor)
};
