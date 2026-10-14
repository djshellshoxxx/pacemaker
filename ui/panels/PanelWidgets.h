// Small form toolkit shared by the overlay panels: label + control rows, headings, wrapped notes, scrolling.
#pragma once
#include "PmLookAndFeel.h"
#include <functional>
#include <memory>
#include <vector>

namespace pacemaker::ui {

// Wrapped, dim explanatory text. Height depends on the width it is laid out in.
class NoteText : public juce::Component {
public:
    explicit NoteText(const juce::String& t, juce::Colour c = colours::dim, float size = 14.0f) : text_(t), colour_(c), size_(size)
    {
        setInterceptsMouseClicks(false, false);
    }
    void setText(const juce::String& t) { text_ = t; repaint(); }
    int heightFor(int width) const
    {
        juce::TextLayout l; build(l, (float) juce::jmax(40, width));
        return (int) std::ceil(l.getHeight()) + 6;
    }
    void paint(juce::Graphics& g) override
    {
        juce::TextLayout l; build(l, (float) getWidth());
        l.draw(g, getLocalBounds().toFloat().reduced(0.0f, 2.0f));
    }
private:
    void build(juce::TextLayout& l, float w) const
    {
        juce::AttributedString s;
        s.setJustification(juce::Justification::topLeft);
        s.append(text_, sans(size_), colour_);
        l.createLayout(s, w);
    }
    juce::String text_;
    juce::Colour colour_;
    float size_;
};

// A vertical list of rows. Call layoutFor(width) after changing the rows or the width.
class Form : public juce::Component {
public:
    static constexpr int kRowH = 32, kLabelW = 236, kGap = 8;

    juce::Label& heading(const juce::String& t)
    {
        auto* l = own<juce::Label>(); l->setText(t, juce::dontSendNotification);
        l->setFont(sans(18.0f, true)); l->setColour(juce::Label::textColourId, colours::green);
        Item it; it.kind = Heading; it.comps = { l }; it.height = 34; items_.push_back(it);
        addAndMakeVisible(l); return *l;
    }
    NoteText& note(const juce::String& t, juce::Colour c = colours::dim)
    {
        auto* n = new NoteText(t, c); owned_.add(n);
        Item it; it.kind = Note; it.comps = { n }; items_.push_back(it);
        addAndMakeVisible(n); return *n;
    }
    // Row with a left label and any number of controls (width < 0: take the remaining width).
    void row(const juce::String& label, std::vector<std::pair<juce::Component*, int>> controls, int height = kRowH)
    {
        Item it; it.kind = Row; it.height = height;
        if (label.isNotEmpty()) {
            auto* l = own<juce::Label>(); l->setText(label, juce::dontSendNotification); l->setFont(sans(15.0f));
            l->setMinimumHorizontalScale(0.8f);
            it.label = l; addAndMakeVisible(l);
        }
        for (auto& c : controls) { it.comps.push_back(c.first); it.widths.push_back(c.second); addAndMakeVisible(c.first); }
        items_.push_back(it);
    }
    void custom(juce::Component& c, int height)
    {
        Item it; it.kind = Custom; it.comps = { &c }; it.height = height; items_.push_back(it); addAndMakeVisible(c);
    }
    void gap(int h = 10) { Item it; it.kind = Gap; it.height = h; items_.push_back(it); }

    // ---- ready-made rows
    juce::ToggleButton& toggle(const juce::String& label, bool init, std::function<void(bool)> fn, const juce::String& text = {})
    {
        auto* b = own<juce::ToggleButton>(); b->setButtonText(text); b->setToggleState(init, juce::dontSendNotification);
        b->onClick = [b, fn] { fn(b->getToggleState()); };
        row(label, { { b, text.isEmpty() ? 34 : 480 } }); return *b;
    }
    juce::Slider& slider(const juce::String& label, double lo, double hi, double step, double init, const juce::String& suffix,
                         std::function<void(double)> fn, int width = 360)
    {
        auto* s = own<juce::Slider>(); s->setSliderStyle(juce::Slider::LinearHorizontal);
        s->setRange(lo, hi, step); s->setValue(init, juce::dontSendNotification);
        s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 86, 26); s->setTextValueSuffix(suffix);
        s->setDoubleClickReturnValue(false, init);
        s->onValueChange = [s, fn] { fn(s->getValue()); };
        row(label, { { s, width } }); return *s;
    }
    juce::TextEditor& text(const juce::String& label, const juce::String& init, std::function<void(const juce::String&)> fn,
                           int width = 260, const juce::String& allowed = {}, int maxLen = 0)
    {
        auto* e = own<juce::TextEditor>(); e->setFont(sans(15.0f)); e->setMultiLine(false);
        if (allowed.isNotEmpty() || maxLen > 0) e->setInputRestrictions(maxLen, allowed);
        e->setText(init, false); e->setIndents(6, 6);
        auto commit = [e, fn] { fn(e->getText()); };
        e->onReturnKey = commit; e->onFocusLost = commit;
        row(label, { { e, width } }); return *e;
    }
    juce::ComboBox& combo(const juce::String& label, const juce::StringArray& items, int selected, std::function<void(int)> fn, int width = 240)
    {
        auto* c = own<juce::ComboBox>();
        for (int i = 0; i < items.size(); ++i) c->addItem(items[i], i + 1);
        c->setSelectedId(selected + 1, juce::dontSendNotification);
        c->onChange = [c, fn] { fn(c->getSelectedId() - 1); };
        row(label, { { c, width } }); return *c;
    }
    juce::Label& info(const juce::String& label, const juce::String& value, juce::Colour c = colours::text)
    {
        auto* l = own<juce::Label>(); l->setText(value, juce::dontSendNotification); l->setFont(sans(15.0f, true));
        l->setColour(juce::Label::textColourId, c);
        row(label, { { l, -1 } }); return *l;
    }
    juce::TextButton& button(const juce::String& label, const juce::String& caption, std::function<void()> fn, int width = 200)
    {
        auto* b = own<juce::TextButton>(caption); b->onClick = std::move(fn);
        row(label, { { b, width } }); return *b;
    }
    juce::TextButton* makeButton(const juce::String& caption, std::function<void()> fn)
    {
        auto* b = own<juce::TextButton>(caption); b->onClick = std::move(fn); return b;
    }

    template <class T, class... A> T* own(A&&... a) { auto* p = new T(std::forward<A>(a)...); owned_.add(p); return p; }

    // Positions everything for the given width and sizes this component to fit. Returns the height.
    int layoutFor(int width)
    {
        int y = 8;
        const int x0 = 4, innerW = juce::jmax(200, width - 8);
        for (auto& it : items_) {
            int h = it.height;
            if (it.kind == Note) {
                auto* n = static_cast<NoteText*>(it.comps[0]);
                h = n->heightFor(innerW); n->setBounds(x0, y, innerW, h);
            } else if (it.kind == Heading) {
                it.comps[0]->setBounds(x0, y + 4, innerW, h - 4);
            } else if (it.kind == Custom) {
                it.comps[0]->setBounds(x0, y, innerW, h);
            } else if (it.kind == Row) {
                int x = x0;
                if (it.label != nullptr) { it.label->setBounds(x, y, kLabelW - kGap, h); x += kLabelW; }
                const int avail = innerW - (x - x0);
                int fixed = 0, flex = 0;
                for (int w : it.widths) { if (w < 0) ++flex; else fixed += w; }
                fixed += kGap * ((int) it.comps.size() - 1);
                const int flexW = flex > 0 ? juce::jmax(60, (avail - fixed) / flex) : 0;
                for (size_t i = 0; i < it.comps.size(); ++i) {
                    int w = it.widths[i] < 0 ? flexW : it.widths[i];
                    w = juce::jmin(w, juce::jmax(20, innerW - (x - x0)));
                    it.comps[i]->setBounds(x, y + 1, w, h - 2);
                    x += w + kGap;
                }
            }
            y += h + (it.kind == Gap ? 0 : 4);
        }
        y += 12;
        setSize(width, y);
        return y;
    }
    void resized() override {}   // sized by layoutFor
    void clear()
    {
        for (auto& it : items_) { if (it.label) removeChildComponent(it.label); for (auto* c : it.comps) removeChildComponent(c); }
        items_.clear(); owned_.clear();
    }

private:
    enum Kind { Heading, Note, Row, Custom, Gap };
    struct Item { Kind kind = Row; juce::Label* label = nullptr; std::vector<juce::Component*> comps; std::vector<int> widths; int height = kRowH; };
    std::vector<Item> items_;
    juce::OwnedArray<juce::Component> owned_;
};

// Viewport hosting a Form that follows the viewport width.
class FormPage : public juce::Viewport {
public:
    FormPage() { setViewedComponent(&form, false); setScrollBarsShown(true, false); setScrollBarThickness(10); }
    Form form;
    void relayout()
    {
        const int w = getMaximumVisibleWidth();
        form.layoutFor(w);
    }
    void resized() override { juce::Viewport::resized(); relayout(); }
};

inline const char* roleLabel(InputRole r)
{
    switch (r) {
    case InputRole::Any: return "Any";
    case InputRole::Kick: return "Kick";
    case InputRole::Snare: return "Snare";
    case InputRole::HiHat: return "Hi-hat";
    case InputRole::Overhead: return "Overhead";
    case InputRole::Trigger: return "Trigger";
    }
    return "Any";
}

} // namespace pacemaker::ui
