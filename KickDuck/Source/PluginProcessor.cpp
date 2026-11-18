#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

namespace
{
// Rotacion de fase con un all-pass de primer orden: la fase en la frecuencia fc vale -theta.
void allpassCoef (float thetaDeg, float fc, double fs, float& a, bool& bypass, float& sign)
{
    sign = 1.0f;
    bypass = false;
    a = 0.0f;
    if (thetaDeg < 0.5f) { bypass = true; return; }
    if (thetaDeg >= 179.5f) { bypass = true; sign = -1.0f; return; }
    float f0 = fc / std::tan (degreesToRadians (thetaDeg) * 0.5f);
    f0 = jlimit (1.0f, (float) (fs * 0.45), f0);
    const float tn = std::tan (MathConstants<float>::pi * f0 / (float) fs);
    a = (tn - 1.0f) / (tn + 1.0f);
}

struct SearchResult { bool ok = false; bool pol = false; float phase = 0, delayMs = 0, corr = 0; };

SearchResult runSearch (const std::vector<std::vector<float>>& K, const std::vector<std::vector<float>>& B,
                        double fs, float fc, int pad, bool lockPol, bool curPol, bool lockPh, float curPhase,
                        bool lockDel, float curDelay, float dMinMs, float dMaxMs)
{
    SearchResult r;
    if (K.empty()) return r;

    std::vector<float> phases;
    if (lockPh) phases.push_back (curPhase);
    else for (int a = 0; a <= 165; a += 15) phases.push_back ((float) a);

    int sMin, sMax, step;
    if (lockDel) { sMin = sMax = roundToInt (curDelay * (float) fs / 1000.0f); step = 1; }
    else
    {
        sMin = roundToInt (dMinMs * (float) fs / 1000.0f);
        sMax = jmin (pad, roundToInt (dMaxMs * (float) fs / 1000.0f));
        step = jmax (1, roundToInt (fs * 0.000125));
    }

    float best = -2.0f;
    for (float theta : phases)
    {
        float a; bool bypass; float sign;
        allpassCoef (theta, fc, fs, a, bypass, sign);

        std::vector<std::vector<float>> pb (B.size());
        for (size_t k = 0; k < B.size(); ++k)
        {
            pb[k].resize (B[k].size());
            float x1 = 0, y1 = 0;
            for (size_t i = 0; i < B[k].size(); ++i)
            {
                const float x = B[k][i];
                const float y = bypass ? x : (a * x + x1 - a * y1);
                x1 = x; y1 = y;
                pb[k][i] = y;
            }
        }

        for (int s = sMin; s <= sMax; s += step)
        {
            double sxy = 0, sxx = 0, syy = 0;
            for (size_t k = 0; k < K.size(); ++k)
            {
                const int len = (int) K[k].size();
                for (int i = pad; i < len; ++i)
                {
                    const int j = i - s;
                    if (j < 0 || j >= len) continue;
                    const double kv = K[k][(size_t) i], bv = pb[k][(size_t) j];
                    sxy += kv * bv; sxx += kv * kv; syy += bv * bv;
                }
            }
            const float c = (float) (sxy / std::sqrt (sxx * syy + 1e-20)) * sign;
            for (int p = 0; p < 2; ++p)
            {
                const bool pol = (p == 1);
                if (lockPol && pol != curPol) continue;
                const float score = pol ? -c : c;
                if (score > best)
                {
                    best = score; r.ok = true; r.pol = pol; r.phase = theta;
                    r.delayMs = (float) s * 1000.0f / (float) fs; r.corr = score;
                }
            }
        }
    }
    return r;
}
} // namespace

AudioProcessorValueTreeState::ParameterLayout KickDuckProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto F = [] (const char* id, const char* name, NormalisableRange<float> r, float def, const char* unit)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                                                      AudioParameterFloatAttributes().withLabel (unit));
    };
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Modo",
                 StringArray { U8 ("Preciso (14 ms)"), U8 ("Rápido (0 ms)"), "MIDI" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "curveMode", 1 }, "Curva",
                 StringArray { "Auto", "Manual" }, 0));
    p.push_back (F ("amount", "Amount", { 0.0f, 100.0f, 0.1f }, 100.0f, " %"));
    p.push_back (F ("release", "Release", { 20.0f, 1000.0f, 1.0f, 0.4f }, 180.0f, " ms"));
    p.push_back (F ("threshold", "Threshold", { -60.0f, 0.0f, 0.1f }, -24.0f, " dB"));
    p.push_back (F ("mix", "Mix", { 0.0f, 100.0f, 0.1f }, 100.0f, " %"));
    p.push_back (F ("attack", "Attack", { 0.1f, 20.0f, 0.01f, 0.4f }, 0.5f, " ms"));
    p.push_back (F ("hold", "Hold", { 0.0f, 100.0f, 0.1f }, 5.0f, " ms"));
    p.push_back (F ("lpFreq", "Graves LP", { 40.0f, 500.0f, 1.0f, 0.5f }, 150.0f, " Hz"));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "fullBand", 1 }, "Banda completa", false));
    p.push_back (F ("phase", "Fase", { 0.0f, 180.0f, 0.1f }, 0.0f, U8 ("°").toRawUTF8()));
    p.push_back (F ("delay", "Delay", { -14.0f, 20.0f, 0.01f }, 0.0f, " ms"));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "polarity", 1 }, "Polaridad", false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "lockPol", 1 }, "Bloquear polaridad", false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "lockPhase", 1 }, "Bloquear fase", false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "lockDelay", 1 }, "Bloquear delay", false));
    return { p.begin(), p.end() };
}

KickDuckProcessor::KickDuckProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pMode = apvts.getRawParameterValue ("mode");
    pCurveMode = apvts.getRawParameterValue ("curveMode");
    pAmount = apvts.getRawParameterValue ("amount");
    pRelease = apvts.getRawParameterValue ("release");
    pThr = apvts.getRawParameterValue ("threshold");
    pMix = apvts.getRawParameterValue ("mix");
    pAttack = apvts.getRawParameterValue ("attack");
    pHold = apvts.getRawParameterValue ("hold");
    pLp = apvts.getRawParameterValue ("lpFreq");
    pFull = apvts.getRawParameterValue ("fullBand");
    pPhase = apvts.getRawParameterValue ("phase");
    pDelay = apvts.getRawParameterValue ("delay");
    pPol = apvts.getRawParameterValue ("polarity");

    for (int i = 0; i < kCurvePoints; ++i)
        curve[(size_t) i] = std::pow (1.0f - (float) i / (float) (kCurvePoints - 1), 1.5f);
    for (auto& a : scopeKick) a = 0.0f;
    for (auto& a : scopeBass) a = 0.0f;
    for (auto& a : scopeDepth) a = 0.0f;
    for (auto& a : zoomKick) a = 0.0f;
    for (auto& a : zoomBass) a = 0.0f;

    apvts.addParameterListener ("mode", this);
}

KickDuckProcessor::~KickDuckProcessor()
{
    *alive = false;
    apvts.removeParameterListener ("mode", this);
}

int KickDuckProcessor::computeLatency() const
{
    return ((int) pMode->load() == 0) ? lookaheadSamples : 0;
}

void KickDuckProcessor::parameterChanged (const String& id, float)
{
    if (id == "mode") setLatencySamples (computeLatency());
}

bool KickDuckProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& in = l.getMainInputChannelSet();
    const auto& out = l.getMainOutputChannelSet();
    if (in != out) return false;
    if (out != AudioChannelSet::stereo() && out != AudioChannelSet::mono()) return false;
    const auto& sc = l.getChannelSet (true, 1);
    return sc.isDisabled() || sc == AudioChannelSet::mono() || sc == AudioChannelSet::stereo();
}

void KickDuckProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    lookaheadSamples = roundToInt (kLookaheadMs * 0.001 * sr);
    ringSize = (int) (sr * 0.25);

    for (auto& r : ringIn) r.init (ringSize);
    ringKick.init (ringSize);
    rawKick.init (ringSize);
    rawBass.init (ringSize);

    dsp::ProcessSpec spec { sr, (uint32) samplesPerBlock, 2 };
    for (auto& f : lr) { f.prepare (spec); f.reset(); }
    lastLp = -1.0f;
    for (int c = 0; c < 2; ++c) apX[c] = apY[c] = 0.0f;
    lpKickRaw.reset(); lpBassRaw.reset(); lpKickCorr.reset(); lpBassCorr.reset();

    env = 0.0f;
    envRel = std::exp (-1.0f / (0.010f * (float) sr));
    armed = true; gap = 0; minGap = (int) (0.05 * sr);
    t = -1; startDepth = depthSm = 0.0f;
    smoothCoef = 1.0f - std::exp (-1.0f / (0.0003f * (float) sr));

    winSamples = (int) (0.040 * sr);
    corrIdle = true;
    zk.assign ((size_t) kZoomPoints, 0.0f);
    zb.assign ((size_t) kZoomPoints, 0.0f);

    spp = jmax (1, (int) (1.5 * sr / kScopePoints));
    scopeCount = 0; pkKick = pkBass = pkDepth = 0.0f;

    capPad = (int) (0.010 * sr);
    capAfter = (int) (0.060 * sr);
    capLen = capPad + capAfter;
    for (int k = 0; k < kMaxSegments; ++k)
    {
        capKick[k].assign ((size_t) capLen, 0.0f);
        capBass[k].assign ((size_t) capLen, 0.0f);
    }
    capActive = false; capCount = 0; capWait = -1;

    setLatencySamples (computeLatency());
}

void KickDuckProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;

    auto mainBus = getBusBuffer (buffer, true, 0);
    auto scBus = getBusBuffer (buffer, true, 1);
    const int n = buffer.getNumSamples();
    const int nCh = jmin (2, mainBus.getNumChannels());
    const int scCh = scBus.getNumChannels();
    if (nCh == 0) return;

    if (capReset.exchange (false)) { capCount = 0; capElapsed = 0; capWait = -1; }

    // parametros
    const int mode = (int) pMode->load();
    const bool manual = pCurveMode->load() > 0.5f;
    const float amount = pAmount->load() * 0.01f;
    const float mix = pMix->load() * 0.01f;
    const float thr = Decibels::decibelsToGain (pThr->load());
    const float lp = pLp->load();
    const bool full = pFull->load() > 0.5f;
    const bool pol = pPol->load() > 0.5f;
    const int L = (mode == 0) ? lookaheadSamples : 0;
    const int dSamp = roundToInt (pDelay->load() * (float) sr / 1000.0f);
    const int wetDelay = jlimit (0, ringSize - 2, L + dSamp);

    float apA; bool apBypass; float apSign;
    allpassCoef (pPhase->load(), lp, sr, apA, apBypass, apSign);
    const float polSign = (pol ? -1.0f : 1.0f) * apSign;

    if (lp != lastLp)
    {
        lastLp = lp;
        for (auto& f : lr) f.setCutoffFrequency (lp);
        lpKickRaw.setLowpass (sr, lp); lpBassRaw.setLowpass (sr, lp);
        lpKickCorr.setLowpass (sr, lp); lpBassCorr.setLowpass (sr, lp);
    }

    const int atk = jmax (1, (int) (pAttack->load() * 0.001f * (float) sr));
    const int hld = (int) (pHold->load() * 0.001f * (float) sr);
    const int rel = jmax (1, (int) (pRelease->load() * 0.001f * (float) sr));
    const int totalLen = manual ? rel : (atk + hld + rel);
    const int carryLen = jmax (atk, (int) (0.002 * sr));

    // MIDI
    int noteOns[64]; int nNotes = 0, nIdx = 0;
    if (mode == 2)
        for (const auto m : midi)
            if (m.getMessage().isNoteOn() && nNotes < 64) noteOns[nNotes++] = m.samplePosition;

    float blockPeak = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        float in[2];
        in[0] = mainBus.getSample (0, i);
        in[1] = (nCh > 1) ? mainBus.getSample (1, i) : in[0];

        float sc = 0.0f;
        if (scCh > 0)
        {
            sc = scBus.getSample (0, i);
            if (scCh > 1) sc = 0.5f * (sc + scBus.getSample (1, i));
        }
        blockPeak = jmax (blockPeak, std::abs (sc));

        ringIn[0].push (in[0]);
        ringIn[1].push (in[1]);
        ringKick.push (sc);
        rawKick.push (lpKickRaw.process (sc));
        rawBass.push (lpBassRaw.process (0.5f * (in[0] + in[1])));

        // deteccion del kick
        const float a = std::abs (sc);
        env = (a > env) ? a : env * envRel;
        bool trig = false;
        if (mode == 2)
        {
            while (nIdx < nNotes && noteOns[nIdx] <= i) { trig = true; ++nIdx; }
        }
        else
        {
            if (gap > 0) --gap;
            if (! armed && env < thr * 0.5f) armed = true;
            if (armed && env > thr && gap <= 0) { trig = true; armed = false; gap = minGap; }
        }

        if (trig)
        {
            startDepth = depthSm;
            t = 0;
            if (corrIdle) { corrIdle = false; corrWait = L; corrLen = winSamples; sxy = sxx = syy = 0; zPos = 0; }
            if (capActive.load() && capWait < 0) capWait = capAfter;
        }

        // curva de ducking
        float depth = 0.0f;
        if (t >= 0)
        {
            if (manual)
            {
                const float x = (float) t / (float) totalLen * (float) (kCurvePoints - 1);
                const int i0 = jlimit (0, kCurvePoints - 2, (int) x);
                const float fr = x - (float) i0;
                depth = curve[(size_t) i0].load() * (1.0f - fr) + curve[(size_t) i0 + 1].load() * fr;
            }
            else if (t < atk) depth = (float) t / (float) atk;
            else if (t < atk + hld) depth = 1.0f;
            else depth = std::pow (1.0f - (float) (t - atk - hld) / (float) rel, 1.5f);

            if (startDepth > 0.01f && t < carryLen)
                depth = jmax (depth, startDepth * (1.0f - (float) t / (float) carryLen));

            if (++t >= totalLen) t = -1;
        }
        depthSm += (depth - depthSm) * smoothCoef;

        // banda grave: all-pass + polaridad + ducking, con retardo del bajo
        float wetPre[2], out[2];
        const float gain = 1.0f - depthSm * amount;
        for (int c = 0; c < 2; ++c)
        {
            const float x = ringIn[c].read (wetDelay);
            const float dry = ringIn[c].read (L);
            float low = x, high = 0.0f;
            if (! full) lr[c].processSample (c, x, low, high);
            float y = low;
            if (! apBypass)
            {
                y = apA * low + apX[c] - apA * apY[c];
                apX[c] = low; apY[c] = y;
            }
            y *= polSign;
            wetPre[c] = y + high;
            out[c] = dry * (1.0f - mix) + (y * gain + high) * mix;
        }
        for (int c = 0; c < nCh; ++c) mainBus.setSample (c, i, out[c]);

        // correlacion kick/bajo en graves
        const float kd = lpKickCorr.process (ringKick.read (L));
        const float bd = lpBassCorr.process (0.5f * (wetPre[0] + wetPre[1]));
        if (! corrIdle)
        {
            if (corrWait > 0) --corrWait;
            else
            {
                sxy += (double) kd * bd; sxx += (double) kd * kd; syy += (double) bd * bd;
                const int zi = jmin (kZoomPoints - 1, zPos * kZoomPoints / jmax (1, winSamples));
                zk[(size_t) zi] = kd; zb[(size_t) zi] = bd;
                ++zPos;
                if (--corrLen <= 0)
                {
                    if (sxx > 1e-9)
                    {
                        const float c = (float) (sxy / std::sqrt (sxx * syy + 1e-20));
                        correlation = 0.5f * correlation.load() + 0.5f * c;
                        corrValid = true;
                        float mk = 1e-6f, mb = 1e-6f;
                        for (int k = 0; k < kZoomPoints; ++k) { mk = jmax (mk, std::abs (zk[(size_t) k])); mb = jmax (mb, std::abs (zb[(size_t) k])); }
                        const float m = jmax (mk, mb);
                        for (int k = 0; k < kZoomPoints; ++k) { zoomKick[(size_t) k] = zk[(size_t) k] / m; zoomBass[(size_t) k] = zb[(size_t) k] / m; }
                    }
                    corrIdle = true;
                }
            }
        }

        // captura para auto-fase
        if (capActive.load())
        {
            if (++capElapsed > (int) (6.0 * sr)) finishCapture();
            else if (capWait > 0 && --capWait == 0) { storeCapturedSegment(); capWait = -1; }
        }

        // scope
        pkKick = jmax (pkKick, a);
        pkBass = jmax (pkBass, std::abs (in[0]));
        pkDepth = jmax (pkDepth, depthSm);
        if (++scopeCount >= spp)
        {
            const int pos = scopePos.load();
            scopeKick[(size_t) pos] = pkKick; scopeBass[(size_t) pos] = pkBass; scopeDepth[(size_t) pos] = pkDepth;
            scopePos = (pos + 1) % kScopePoints;
            scopeCount = 0; pkKick = pkBass = pkDepth = 0.0f;
        }
    }
    kickLevel = blockPeak;
}

void KickDuckProcessor::storeCapturedSegment()
{
    if (capCount >= kMaxSegments) return;
    float mk = 0.0f;
    for (int k = 0; k < capLen; ++k)
    {
        capKick[capCount][(size_t) k] = rawKick.read (capLen - 1 - k);
        capBass[capCount][(size_t) k] = rawBass.read (capLen - 1 - k);
        mk = jmax (mk, std::abs (capKick[capCount][(size_t) k]));
    }
    if (mk < 1.0e-4f) return; // sin kick real: descartar
    if (++capCount >= kMaxSegments) finishCapture();
}

void KickDuckProcessor::finishCapture()
{
    capActive = false;
    autoState = (capCount >= 2) ? 2 : 5;
}

void KickDuckProcessor::requestAutoPhase()
{
    const int s = autoState.load();
    if (s == 1 || s == 3) return;
    capReset = true;
    capActive = true;
    autoState = 1;
}

void KickDuckProcessor::pollAutoPhase()
{
    if (autoState.load() == 2) startAutoPhaseSearch();
}

void KickDuckProcessor::startAutoPhaseSearch()
{
    autoState = 3;

    std::vector<std::vector<float>> K, B;
    for (int k = 0; k < capCount; ++k) { K.push_back (capKick[k]); B.push_back (capBass[k]); }

    const double fs = sr;
    const float fc = pLp->load();
    const int pad = capPad;
    const bool lockPol = apvts.getRawParameterValue ("lockPol")->load() > 0.5f;
    const bool lockPh = apvts.getRawParameterValue ("lockPhase")->load() > 0.5f;
    const bool lockDel = apvts.getRawParameterValue ("lockDelay")->load() > 0.5f;
    const bool curPol = pPol->load() > 0.5f;
    const float curPhase = pPhase->load(), curDelay = pDelay->load();
    const float dMin = ((int) pMode->load() == 0) ? -kLookaheadMs : 0.0f;
    auto aliveCopy = alive;

    Thread::launch ([this, aliveCopy, K, B, fs, fc, pad, lockPol, curPol, lockPh, curPhase, lockDel, curDelay, dMin]
    {
        const auto r = runSearch (K, B, fs, fc, pad, lockPol, curPol, lockPh, curPhase, lockDel, curDelay, dMin, 20.0f);
        MessageManager::callAsync ([this, aliveCopy, r, curPol, curPhase, curDelay]
        {
            if (! aliveCopy->load()) return;
            if (! r.ok) { autoState = 5; return; }
            undo = { true, curPol ? 1.0f : 0.0f, curPhase, curDelay };
            auto setP = [this] (const char* id, float v)
            {
                if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
            };
            setP ("polarity", r.pol ? 1.0f : 0.0f);
            setP ("phase", r.phase);
            setP ("delay", r.delayMs);
            autoScore = r.corr;
            autoState = 4;
        });
    });
}

void KickDuckProcessor::undoAutoPhase()
{
    if (! undo.valid) return;
    auto setP = [this] (const char* id, float v)
    {
        if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    setP ("polarity", undo.pol);
    setP ("phase", undo.phase);
    setP ("delay", undo.delay);
    undo.valid = false;
    autoState = 0;
}

String KickDuckProcessor::getAutoPhaseStatus() const
{
    switch (autoState.load())
    {
        case 1: return U8 ("Capturando kicks… deja sonar kick y bajo");
        case 3: return U8 ("Analizando…");
        case 4: return U8 ("Listo · correlación ") + String (autoScore.load(), 2);
        case 5: return U8 ("Necesito kick y bajo sonando");
        default: return {};
    }
}

void KickDuckProcessor::getStateInformation (MemoryBlock& dest)
{
    auto state = apvts.copyState();
    String c;
    for (auto& v : curve) c << String (v.load(), 4) << " ";
    state.setProperty ("curve", c.trim(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void KickDuckProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = ValueTree::fromXml (*xml);
            apvts.replaceState (state);
            auto tokens = StringArray::fromTokens (state.getProperty ("curve").toString(), " ", "");
            for (int i = 0; i < jmin (kCurvePoints, tokens.size()); ++i)
                curve[(size_t) i] = jlimit (0.0f, 1.0f, tokens[i].getFloatValue());
        }
}

AudioProcessorEditor* KickDuckProcessor::createEditor() { return new KickDuckEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KickDuckProcessor(); }
