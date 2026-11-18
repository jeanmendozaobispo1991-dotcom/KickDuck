#pragma once
#include "PluginProcessor.h"

class KickDuckEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KickDuckEditor (KickDuckProcessor&);
    ~KickDuckEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override { editCurve (e); }
    void mouseDrag (const juce::MouseEvent& e) override { editCurve (e); }

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SA> att;
    };

    void timerCallback() override;
    void setTab (int);
    void updateVisibility();
    void editCurve (const juce::MouseEvent&);
    void addKnob (int idx, const char* paramId, const char* name, const char* unit, int dec);
    void drawScope (juce::Graphics&, juce::Rectangle<float>);
    void drawCurveEditor (juce::Graphics&, juce::Rectangle<float>);
    void drawZoom (juce::Graphics&, juce::Rectangle<float>);
    void drawCorrelation (juce::Graphics&, juce::Rectangle<float>);

    KickDuckProcessor& proc;
    int tab = 0;
    bool manualMode = false, noSignal = false;
    int curveIdx = 0;
    juce::uint32 lastSignalMs = 0;
    juce::Rectangle<int> panel, corrPanel, knobRow;

    juce::TextButton tabSide { "Sidechain" }, tabPhase { "Fase" }, autoBtn { "Auto-fase" }, undoBtn { "Deshacer" }, copyBtn { "Copiar del kick" };
    juce::ComboBox modeBox, curveBox;
    juce::ToggleButton fullBtn { "Banda completa" }, polBtn { "Invertir polaridad" },
        lockPol { "Bloq. polaridad" }, lockPh { "Bloq. fase" }, lockDel { "Bloq. delay" };

    enum { kAmount, kRelease, kThr, kMix, kAttack, kHold, kLp, kPhase, kDelay, kSens, kSmooth, kNumKnobs };
    Knob knobs[kNumKnobs];

    std::unique_ptr<CA> modeAtt, curveAtt;
    std::unique_ptr<BA> fullAtt, polAtt, lockPolAtt, lockPhAtt, lockDelAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickDuckEditor)
};
