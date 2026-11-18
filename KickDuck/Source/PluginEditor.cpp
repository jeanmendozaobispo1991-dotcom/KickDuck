#include "PluginEditor.h"

using namespace juce;

namespace
{
const Colour kBg (0xff0e0e12), kPanel (0xff16161c), kOrange (0xffff5a2a), kCream (0xffe9dfc8),
    kMuted (0xff9a9aa6), kGrid (0xff26262e), kGreen (0xff4ade80), kRed (0xffef4444), kAmber (0xfff59e0b);
}

KickDuckEditor::KickDuckEditor (KickDuckProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setSize (1000, 640);

    for (auto* b : { &tabSide, &tabPhase })
    {
        b->setRadioGroupId (1);
        b->setClickingTogglesState (true);
        b->setColour (TextButton::buttonOnColourId, kOrange);
        b->setColour (TextButton::textColourOnId, Colour (0xff16110e));
        addAndMakeVisible (*b);
    }
    tabSide.setToggleState (true, dontSendNotification);
    tabSide.onClick = [this] { setTab (0); };
    tabPhase.onClick = [this] { setTab (1); };

    if (auto* c = dynamic_cast<AudioParameterChoice*> (proc.apvts.getParameter ("mode")))
        modeBox.addItemList (c->choices, 1);
    if (auto* c = dynamic_cast<AudioParameterChoice*> (proc.apvts.getParameter ("curveMode")))
        curveBox.addItemList (c->choices, 1);
    addAndMakeVisible (modeBox);
    addAndMakeVisible (curveBox);
    modeAtt = std::make_unique<CA> (proc.apvts, "mode", modeBox);
    curveAtt = std::make_unique<CA> (proc.apvts, "curveMode", curveBox);

    addKnob (kAmount, "amount", "Amount");
    addKnob (kRelease, "release", "Release");
    addKnob (kThr, "threshold", "Threshold");
    addKnob (kMix, "mix", "Mix");
    addKnob (kAttack, "attack", "Attack");
    addKnob (kHold, "hold", "Hold");
    addKnob (kLp, "lpFreq", "Graves LP");
    addKnob (kPhase, "phase", "Fase");
    addKnob (kDelay, "delay", "Delay");

    for (auto* b : { &fullBtn, &polBtn, &lockPol, &lockPh, &lockDel })
    {
        b->setColour (ToggleButton::textColourId, kCream);
        b->setColour (ToggleButton::tickColourId, kOrange);
        addAndMakeVisible (*b);
    }
    fullAtt = std::make_unique<BA> (proc.apvts, "fullBand", fullBtn);
    polAtt = std::make_unique<BA> (proc.apvts, "polarity", polBtn);
    lockPolAtt = std::make_unique<BA> (proc.apvts, "lockPol", lockPol);
    lockPhAtt = std::make_unique<BA> (proc.apvts, "lockPhase", lockPh);
    lockDelAtt = std::make_unique<BA> (proc.apvts, "lockDelay", lockDel);

    autoBtn.setColour (TextButton::buttonColourId, kOrange);
    autoBtn.setColour (TextButton::textColourOffId, Colour (0xff16110e));
    autoBtn.onClick = [this] { proc.requestAutoPhase(); };
    undoBtn.onClick = [this] { proc.undoAutoPhase(); };
    addAndMakeVisible (autoBtn);
    addAndMakeVisible (undoBtn);

    lastSignalMs = Time::getMillisecondCounter();
    updateVisibility();
    startTimerHz (30);
}

KickDuckEditor::~KickDuckEditor() { stopTimer(); }

void KickDuckEditor::addKnob (int idx, const char* paramId, const char* name)
{
    auto& k = knobs[idx];
    k.slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (Slider::TextBoxBelow, false, 76, 18);
    k.slider.setColour (Slider::rotarySliderFillColourId, kOrange);
    k.slider.setColour (Slider::rotarySliderOutlineColourId, Colour (0xff2a2a33));
    k.slider.setColour (Slider::textBoxTextColourId, Colours::white);
    k.slider.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    k.label.setText (name, dontSendNotification);
    k.label.setJustificationType (Justification::centred);
    k.label.setColour (Label::textColourId, kMuted);
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
    k.att = std::make_unique<SA> (proc.apvts, paramId, k.slider);
}

void KickDuckEditor::setTab (int t)
{
    tab = t;
    updateVisibility();
    resized();
    repaint();
}

void KickDuckEditor::updateVisibility()
{
    manualMode = proc.apvts.getRawParameterValue ("curveMode")->load() > 0.5f;
    const bool side = (tab == 0);
    auto show = [this] (int i, bool v) { knobs[i].slider.setVisible (v); knobs[i].label.setVisible (v); };
    show (kAmount, side); show (kRelease, side); show (kThr, side);
    show (kAttack, side && ! manualMode); show (kHold, side && ! manualMode);
    show (kMix, true); show (kLp, true);
    show (kPhase, ! side); show (kDelay, ! side);
    curveBox.setVisible (side);
    fullBtn.setVisible (side);
    polBtn.setVisible (! side); lockPol.setVisible (! side); lockPh.setVisible (! side);
    lockDel.setVisible (! side); autoBtn.setVisible (! side); undoBtn.setVisible (! side);
}

void KickDuckEditor::resized()
{
    auto r = getLocalBounds().reduced (24, 20);
    auto top = r.removeFromTop (44);
    tabSide.setBounds (top.removeFromLeft (200).withTrimmedLeft (110).withWidth (90));
    tabPhase.setBounds (tabSide.getRight() + 4, top.getY(), 90, 36);
    modeBox.setBounds (top.removeFromRight (170).withHeight (36));
    top.removeFromRight (12);
    curveBox.setBounds (top.removeFromRight (110).withHeight (36));

    r.removeFromTop (12);
    const int panelH = (tab == 0) ? 300 : 230;
    panel = r.removeFromTop (panelH);
    r.removeFromTop (12);
    if (tab == 1) { corrPanel = r.removeFromTop (84); r.removeFromTop (12); }
    knobRow = r.removeFromTop (150);

    std::vector<int> ids;
    if (tab == 0)
    {
        ids = { kAmount, kRelease, kThr };
        if (! manualMode) { ids.push_back (kAttack); ids.push_back (kHold); }
        ids.push_back (kLp); ids.push_back (kMix);
    }
    else ids = { kPhase, kDelay, kLp, kMix };

    const int cw = 96;
    const int total = (int) ids.size() * cw;
    const int gapX = ids.size() > 1 ? jmax (0, (knobRow.getWidth() - 220 - total) / ((int) ids.size() - 1)) : 0;
    int x = knobRow.getX() + 8;
    for (int id : ids)
    {
        auto& k = knobs[id];
        k.slider.setBounds (x, knobRow.getY() + 26, cw, 112);
        k.label.setBounds (x, knobRow.getY() + 4, cw, 18);
        x += cw + gapX;
    }

    auto side = Rectangle<int> (knobRow.getRight() - 210, knobRow.getY() + 8, 200, knobRow.getHeight() - 16);
    if (tab == 0) fullBtn.setBounds (side.removeFromTop (30));
    else
    {
        autoBtn.setBounds (side.removeFromTop (34).removeFromLeft (110));
        undoBtn.setBounds (autoBtn.getRight () + 6, autoBtn.getY(), 80, 34);
        polBtn.setBounds (side.removeFromTop (24));
        lockPol.setBounds (side.removeFromTop (22));
        lockPh.setBounds (side.removeFromTop (22));
        lockDel.setBounds (side.removeFromTop (22));
    }
}

void KickDuckEditor::timerCallback()
{
    proc.pollAutoPhase();
    const auto now = Time::getMillisecondCounter();
    if (proc.kickLevel.load() > 1.0e-4f) lastSignalMs = now;
    noSignal = (now - lastSignalMs) > 1500;

    const bool m = proc.apvts.getRawParameterValue ("curveMode")->load() > 0.5f;
    if (m != manualMode) { updateVisibility(); resized(); }
    repaint();
}

void KickDuckEditor::editCurve (const MouseEvent& e)
{
    if (tab != 0 || ! manualMode) return;
    auto b = panel.toFloat().reduced (14.0f);
    if (! b.contains (e.position)) return;
    const int idx = jlimit (0, KickDuckProcessor::kCurvePoints - 1,
                            roundToInt ((e.position.x - b.getX()) / b.getWidth() * (KickDuckProcessor::kCurvePoints - 1)));
    proc.curve[(size_t) idx] = jlimit (0.0f, 1.0f, (e.position.y - b.getY()) / b.getHeight());
}

void KickDuckEditor::drawScope (Graphics& g, Rectangle<float> b)
{
    const int N = KickDuckProcessor::kScopePoints;
    const int pos = proc.scopePos.load();
    float mk = 1.0e-4f, mb = 1.0e-4f;
    for (int i = 0; i < N; ++i) { mk = jmax (mk, proc.scopeKick[(size_t) i].load()); mb = jmax (mb, proc.scopeBass[(size_t) i].load()); }
    const float cy = b.getCentreY(), hh = b.getHeight() * 0.5f;
    Path depth;
    for (int k = 0; k < N; ++k)
    {
        const int i = (pos + k) % N;
        const float x = b.getX() + b.getWidth() * (float) k / (float) (N - 1);
        const float ak = proc.scopeKick[(size_t) i].load() / mk * hh;
        const float ab = proc.scopeBass[(size_t) i].load() / mb * hh * 0.8f;
        g.setColour (kCream.withAlpha (0.8f)); g.drawVerticalLine ((int) x, cy - ab, cy + ab);
        g.setColour (kOrange.withAlpha (0.85f)); g.drawVerticalLine ((int) x, cy - ak, cy + ak);
        const float y = b.getY() + proc.scopeDepth[(size_t) i].load() * b.getHeight();
        if (k == 0) depth.startNewSubPath (x, y); else depth.lineTo (x, y);
    }
    g.setColour (Colours::white);
    g.strokePath (depth, PathStrokeType (2.0f));
}

void KickDuckEditor::drawCurveEditor (Graphics& g, Rectangle<float> b)
{
    Path p;
    const int N = KickDuckProcessor::kCurvePoints;
    for (int i = 0; i < N; ++i)
    {
        const float x = b.getX() + b.getWidth() * (float) i / (float) (N - 1);
        const float y = b.getY() + proc.curve[(size_t) i].load() * b.getHeight();
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (Colours::white);
    g.strokePath (p, PathStrokeType (2.5f));
    for (int i = 0; i < N; ++i)
    {
        const float x = b.getX() + b.getWidth() * (float) i / (float) (N - 1);
        const float y = b.getY() + proc.curve[(size_t) i].load() * b.getHeight();
        g.setColour (kBg); g.fillEllipse (x - 5, y - 5, 10, 10);
        g.setColour (Colours::white); g.drawEllipse (x - 5, y - 5, 10, 10, 2.0f);
    }
    g.setColour (kMuted);
    g.setFont (12.0f);
    g.drawText (U8 ("Dibuja la curva arrastrando · arriba = sin ducking, abajo = ducking máximo"),
                b.toNearestInt().removeFromBottom (16), Justification::centredLeft);
}

void KickDuckEditor::drawZoom (Graphics& g, Rectangle<float> b)
{
    const int N = KickDuckProcessor::kZoomPoints;
    const float cy = b.getCentreY(), hh = b.getHeight() * 0.45f;
    Path pk, pb;
    for (int i = 0; i < N; ++i)
    {
        const float x = b.getX() + b.getWidth() * (float) i / (float) (N - 1);
        const float yk = cy - proc.zoomKick[(size_t) i].load() * hh;
        const float yb = cy - proc.zoomBass[(size_t) i].load() * hh;
        if (i == 0) { pk.startNewSubPath (x, yk); pb.startNewSubPath (x, yb); }
        else { pk.lineTo (x, yk); pb.lineTo (x, yb); }
    }
    g.setColour (kOrange); g.strokePath (pk, PathStrokeType (2.5f));
    g.setColour (kCream); g.strokePath (pb, PathStrokeType (2.5f));
}

void KickDuckEditor::drawCorrelation (Graphics& g, Rectangle<float> b)
{
    const bool valid = proc.corrValid.load();
    const float c = proc.correlation.load();
    String txt = U8 ("Esperando kick…");
    Colour col = kMuted;
    if (valid)
    {
        if (c >= 0.6f) { txt = U8 ("● En fase · ") + String (c, 2); col = kGreen; }
        else if (c >= 0.2f) { txt = U8 ("● Parcial · ") + String (c, 2); col = kAmber; }
        else { txt = U8 ("● Fuera de fase · ") + String (c, 2); col = kRed; }
    }
    g.setColour (kMuted); g.setFont (Font (FontOptions (12.0f)));
    g.drawText ("CORRELACION", b.toNearestInt().removeFromTop (18).withTrimmedLeft (14), Justification::centredLeft);
    g.setColour (col); g.setFont (Font (FontOptions (14.0f, Font::bold)));
    g.drawText (txt, b.toNearestInt().removeFromTop (18).withTrimmedRight (14), Justification::centredRight);

    auto bar = b.reduced (14.0f, 0.0f).withTrimmedTop (30.0f).withHeight (14.0f);
    ColourGradient grad (kRed, bar.getX(), 0, kGreen, bar.getRight(), 0, false);
    grad.addColour (0.5, kAmber);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (bar, 7.0f);
    if (valid)
    {
        const float x = bar.getX() + bar.getWidth() * (jlimit (-1.0f, 1.0f, c) * 0.5f + 0.5f);
        g.setColour (Colours::white);
        g.fillRoundedRectangle (x - 2, bar.getY() - 4, 4, bar.getHeight() + 8, 2.0f);
    }
    g.setColour (kMuted); g.setFont (Font (FontOptions (11.0f)));
    auto lab = b.toNearestInt().withTrimmedLeft (14).withTrimmedRight (14).removeFromBottom (16);
    g.drawText (U8 ("-1 se cancelan"), lab, Justification::centredLeft);
    g.drawText ("0", lab, Justification::centred);
    g.drawText (U8 ("+1 en fase"), lab, Justification::centredRight);
}

void KickDuckEditor::paint (Graphics& g)
{
    g.fillAll (kBg);

    g.setColour (kOrange);
    g.setFont (Font (FontOptions (20.0f, Font::bold)));
    g.drawText ("KICKDUCK", 24, 20, 150, 44, Justification::centredLeft);

    const float ms = (float) proc.getLatencySamples() / (float) jmax (1.0, proc.getSampleRate()) * 1000.0f;
    g.setColour (kMuted); g.setFont (Font (FontOptions (12.0f)));
    g.drawText (U8 ("LATENCIA ") + String (ms, 0) + " ms", getWidth() / 2 - 40, 20, 160, 44, Justification::centredLeft);

    g.setColour (kPanel);
    g.fillRoundedRectangle (panel.toFloat(), 12.0f);
    auto inner = panel.toFloat().reduced (14.0f);

    g.setColour (kGrid);
    g.drawHorizontalLine ((int) inner.getCentreY(), inner.getX(), inner.getRight());
    for (int i = 1; i < 4; ++i) g.drawVerticalLine ((int) (inner.getX() + inner.getWidth() * (float) i / 4.0f), inner.getY(), inner.getBottom());

    if (tab == 0)
    {
        if (manualMode) drawCurveEditor (g, inner);
        else drawScope (g, inner);
        g.setFont (Font (FontOptions (12.0f)));
        g.setColour (kOrange);  g.drawText (U8 ("● Kick"), panel.getX() + 14, panel.getY() + 4, 80, 16, Justification::left);
        g.setColour (kCream);   g.drawText (U8 ("● Bajo"), panel.getX() + 84, panel.getY() + 4, 80, 16, Justification::left);
        g.setColour (Colours::white); g.drawText (U8 ("— Curva"), panel.getX() + 154, panel.getY() + 4, 80, 16, Justification::left);
    }
    else
    {
        drawZoom (g, inner);
        g.setFont (Font (FontOptions (12.0f)));
        g.setColour (kOrange); g.drawText (U8 ("● Kick (graves)"), panel.getX() + 14, panel.getY() + 4, 120, 16, Justification::left);
        g.setColour (kCream);  g.drawText (U8 ("● Bajo (graves)"), panel.getX() + 134, panel.getY() + 4, 120, 16, Justification::left);
        g.setColour (kMuted);  g.drawText ("Zoom 0-40 ms", panel.getRight() - 134, panel.getY() + 4, 120, 16, Justification::right);

        g.setColour (kPanel);
        g.fillRoundedRectangle (corrPanel.toFloat(), 12.0f);
        drawCorrelation (g, corrPanel.toFloat().reduced (0.0f, 8.0f));

        const auto st = proc.getAutoPhaseStatus();
        if (st.isNotEmpty())
        {
            g.setColour (kCream); g.setFont (Font (FontOptions (12.0f)));
            g.drawText (st, knobRow.getRight() - 210, knobRow.getBottom() - 22, 210, 18, Justification::centredRight);
        }
    }

    g.setColour (kPanel);
    g.fillRoundedRectangle (knobRow.toFloat(), 12.0f);

    g.setFont (Font (FontOptions (12.0f)));
    if (noSignal && (int) proc.apvts.getRawParameterValue ("mode")->load() != 2)
    {
        g.setColour (kRed);
        g.drawText (U8 ("Sin señal: rutea el kick al sidechain del plugin"), getLocalBounds().removeFromBottom (24),
                    Justification::centred);
    }
    else
    {
        g.setColour (kMuted);
        g.drawText (U8 ("Rutea el kick con un envío al sidechain del plugin"), getLocalBounds().removeFromBottom (24),
                    Justification::centred);
    }
}
