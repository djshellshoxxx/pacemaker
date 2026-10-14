// House style: dark, high contrast, medical (RS-01 section 4). All text is at least 14 px.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace pacemaker::ui {

namespace colours {
inline const juce::Colour bg { 0xff0b0f12 }, panel { 0xff11181d }, panel2 { 0xff16212a }, line { 0xff24333e };
inline const juce::Colour text { 0xffd7e3ea }, dim { 0xff8aa3b1 }, green { 0xff3ddc97 }, amber { 0xffffb020 };
inline const juce::Colour red { 0xffff4d5e }, cyan { 0xff4cc9f0 }, grid { 0xff1c2a34 };
}

inline juce::Font mono(float h, bool bold = false)
{
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain));
}
inline juce::Font sans(float h, bool bold = false)
{
    return juce::Font(juce::FontOptions(juce::Font::getDefaultSansSerifFontName(), h, bold ? juce::Font::bold : juce::Font::plain));
}

class PmLookAndFeel : public juce::LookAndFeel_V4 {
public:
    PmLookAndFeel()
    {
        using namespace colours;
        setColour(juce::ResizableWindow::backgroundColourId, bg);
        setColour(juce::Label::textColourId, text);
        setColour(juce::TextButton::buttonColourId, panel2);
        setColour(juce::TextButton::buttonOnColourId, green.darker(0.4f));
        setColour(juce::TextButton::textColourOffId, text);
        setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        setColour(juce::ComboBox::backgroundColourId, panel2);
        setColour(juce::ComboBox::textColourId, text);
        setColour(juce::ComboBox::outlineColourId, line);
        setColour(juce::ComboBox::arrowColourId, dim);
        setColour(juce::PopupMenu::backgroundColourId, panel);
        setColour(juce::PopupMenu::textColourId, text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, green.darker(0.5f));
        setColour(juce::TextEditor::backgroundColourId, panel);
        setColour(juce::TextEditor::textColourId, text);
        setColour(juce::TextEditor::outlineColourId, line);
        setColour(juce::TextEditor::focusedOutlineColourId, green);
        setColour(juce::ToggleButton::textColourId, text);
        setColour(juce::ToggleButton::tickColourId, green);
        setColour(juce::ToggleButton::tickDisabledColourId, dim);
        setColour(juce::Slider::thumbColourId, green);
        setColour(juce::Slider::trackColourId, green.darker(0.5f));
        setColour(juce::Slider::backgroundColourId, panel2);
        setColour(juce::TabbedButtonBar::tabOutlineColourId, line);
        setColour(juce::TabbedComponent::backgroundColourId, bg);
        setColour(juce::ListBox::backgroundColourId, panel);
        setColour(juce::ScrollBar::thumbColourId, line.brighter(0.3f));
        setColour(juce::TooltipWindow::backgroundColourId, panel2);
        setColour(juce::TooltipWindow::textColourId, text);
        setColour(juce::AlertWindow::backgroundColourId, panel);
        setColour(juce::AlertWindow::textColourId, text);
        setDefaultSansSerifTypefaceName(juce::Font::getDefaultSansSerifFontName());
    }
    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return sans(juce::jlimit(14.0f, 22.0f, (float) h * 0.45f), true); }
    juce::Font getLabelFont(juce::Label& l) override { return l.getFont().getHeight() < 14.0f ? sans(14.0f) : l.getFont(); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return sans(15.0f); }
    juce::Font getPopupMenuFont() override { return sans(15.0f); }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& bc, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced(0.5f);
        auto c = bc;
        if (b.getToggleState()) c = colours::green.darker(0.45f);
        if (over) c = c.brighter(0.12f);
        if (down) c = c.brighter(0.25f);
        g.setColour(c);
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(b.hasKeyboardFocus(false) ? colours::cyan : (b.getToggleState() ? colours::green : colours::line));
        g.drawRoundedRectangle(r, 5.0f, b.hasKeyboardFocus(false) ? 2.0f : 1.0f);
    }
};

} // namespace pacemaker::ui
