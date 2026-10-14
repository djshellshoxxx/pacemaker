// Song map (setlist) panel, RS-01 section 6.
#include "Panels.h"
#include "PanelWidgets.h"

namespace pacemaker::ui {

namespace {
constexpr int kRowH = 38;
constexpr int kColGo = 64, kColName = 230, kColBpm = 76, kColMeter = 72, kColPattern = 180, kColCount = 72, kColPc = 72, kGap = 6;
}

struct SongMapPanel::Impl : juce::Timer {
    // One editable row.
    struct Row : juce::Component {
        Impl& im; int index; bool alive = true;
        juce::TextButton go;
        juce::TextEditor name, bpm, meter, countIn, pc;
        juce::ComboBox pattern;
        Row(Impl& i, int idx, const Song& s) : im(i), index(idx)
        {
            go.setButtonText(juce::String(idx + 1) + "  Go");
            go.setTooltip("Select this song now (loads its tempo, meter and pattern)");
            go.onClick = [this] { im.ctx.core.selectSong(index); im.sel = index; im.list.repaint(); };
            for (auto* e : { &name, &bpm, &meter, &countIn, &pc }) {
                e->setFont(sans(15.0f)); e->setIndents(6, 0); e->setMultiLine(false); addAndMakeVisible(e);
                e->onFocusLost = [this] { commit(); };
                e->onReturnKey = [this] { commit(); };
                e->onTextChange = [this] { im.sel = index; };
            }
            bpm.setInputRestrictions(6, "0123456789."); meter.setInputRestrictions(5, "0123456789/");
            countIn.setInputRestrictions(2, "0123456789"); pc.setInputRestrictions(3, "0123456789");
            name.setText(juce::String(s.name), false);
            bpm.setText(s.bpm > 0 ? juce::String(s.bpm, 1) : juce::String(), false); bpm.setTextToShowWhenEmpty("none", colours::dim);
            meter.setText(juce::String(s.meterNum) + "/" + juce::String(s.meterDen), false);
            countIn.setText(juce::String(s.countInBeats), false);
            pc.setText(s.programChange >= 0 ? juce::String(s.programChange) : juce::String(), false); pc.setTextToShowWhenEmpty("none", colours::dim);
            int pi = 1, k = 1;
            for (const auto& p : patternList()) { pattern.addItem(p.label, k); if (p.pattern == s.pattern) pi = k; ++k; }
            pattern.setSelectedId(pi, juce::dontSendNotification);
            pattern.onChange = [this] {
                const int k2 = pattern.getSelectedId() - 1;
                const auto& pl = patternList();
                if (k2 >= 0 && k2 < (int) pl.size() && pl[(size_t) k2].pattern != BarPattern::Learned) {
                    meter.setText(juce::String(pl[(size_t) k2].beats) + "/" + juce::String(pl[(size_t) k2].unit), false);
                }
                commit();
            };
            addAndMakeVisible(go); addAndMakeVisible(pattern);
        }
        void commit()
        {
            if (!alive) return;
            Song s;
            s.name = name.getText().toStdString();
            s.bpm = juce::jlimit(0.0, 300.0, bpm.getText().getDoubleValue());
            const auto mt = meter.getText();
            int mn = mt.upToFirstOccurrenceOf("/", false, false).getIntValue(), md = mt.fromFirstOccurrenceOf("/", false, false).getIntValue();
            if (mn < 1 || mn > 16) mn = 4;
            if (md != 2 && md != 4 && md != 8 && md != 16) md = 4;
            s.meterNum = mn; s.meterDen = md;
            const int k = pattern.getSelectedId() - 1;
            const auto& pl = patternList();
            s.pattern = k >= 0 && k < (int) pl.size() ? pl[(size_t) k].pattern : BarPattern::Rock44;
            s.countInBeats = juce::jlimit(0, 16, countIn.getText().getIntValue());
            s.programChange = pc.getText().isEmpty() ? -1 : juce::jlimit(0, 127, pc.getText().getIntValue());
            meter.setText(juce::String(mn) + "/" + juce::String(md), false);
            if (s.bpm <= 0) bpm.setText({}, false);
            const int idx = index;
            im.ctx.core.editSession([&](SessionSettings& ss) { if (idx < (int) ss.songs.songs.size()) ss.songs.songs[(size_t) idx] = s; });
        }
        void paint(juce::Graphics& g) override
        {
            const bool cur = im.ctx.core.currentSong() == index;
            auto r = getLocalBounds().toFloat().reduced(0.5f);
            g.setColour(cur ? colours::green.withAlpha(0.18f) : index == im.sel ? colours::panel2 : colours::panel);
            g.fillRoundedRectangle(r, 5.0f);
            g.setColour(cur ? colours::green : colours::line);
            g.drawRoundedRectangle(r, 5.0f, cur ? 2.0f : 1.0f);
        }
        void mouseDown(const juce::MouseEvent&) override { im.sel = index; im.list.repaint(); }
        void resized() override
        {
            auto r = getLocalBounds().reduced(6, 4);
            auto take = [&](juce::Component& c, int w) { c.setBounds(r.removeFromLeft(w)); r.removeFromLeft(kGap); };
            take(go, kColGo); take(name, kColName); take(bpm, kColBpm); take(meter, kColMeter); take(pattern, kColPattern);
            take(countIn, kColCount); take(pc, kColPc);
        }
    };

    // Container for the header and the rows.
    struct List : juce::Component {
        Impl& im;
        explicit List(Impl& i) : im(i) {}
        void paint(juce::Graphics& g) override
        {
            g.setColour(colours::dim); g.setFont(sans(14.0f, true));
            int x = 6 + 6;
            auto col = [&](const char* t, int w) { g.drawText(t, x, 0, w, 24, juce::Justification::centredLeft); x += w + kGap; };
            col("Song", kColGo); col("Name", kColName); col("BPM", kColBpm); col("Meter", kColMeter); col("Pattern", kColPattern);
            col("Count-in", kColCount); col("Prog chg", kColPc);
            if (im.rows.isEmpty()) {
                g.setColour(colours::dim); g.setFont(sans(15.0f));
                g.drawText("No songs yet. Add one, or import a CSV.", getLocalBounds().withTrimmedTop(30), juce::Justification::centred);
            }
        }
        void resized() override
        {
            int y = 28;
            for (auto* r : im.rows) { r->setBounds(0, y, getWidth(), kRowH); y += kRowH + 4; }
        }
    };

    UiContext& ctx;
    SongMapPanel& owner;
    List list { *this };
    juce::Viewport view;
    juce::OwnedArray<Row> rows;
    juce::OwnedArray<juce::TextButton> buttons;
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
    int sel = -1;
    int lastCurrent = -2;

    Impl(SongMapPanel& o, UiContext& c) : ctx(c), owner(o)
    {
        auto add = [this](const char* t, std::function<void()> fn, const char* tip) {
            auto* b = buttons.add(new juce::TextButton(t)); b->onClick = std::move(fn); b->setTooltip(tip); owner.addAndMakeVisible(b);
        };
        add("Add", [this] { addSong(); }, "Add a song at the end");
        add("Duplicate", [this] { duplicateSong(); }, "Duplicate the selected row");
        add("Remove", [this] { removeSong(); }, "Remove the selected row");
        add("Up", [this] { moveSong(-1); }, "Move the selected row up");
        add("Down", [this] { moveSong(1); }, "Move the selected row down");
        add("Import CSV", [this] { importCsv(); }, "Import a song list (this project's CSV or an AbleSet setlist export)");
        add("Export CSV", [this] { exportCsv(); }, "Save the song list as CSV");
        status.setFont(sans(14.0f)); status.setColour(juce::Label::textColourId, colours::dim);
        owner.addAndMakeVisible(status);
        view.setViewedComponent(&list, false); view.setScrollBarsShown(true, false);
        owner.addAndMakeVisible(view);
        rebuild();
        startTimerHz(5);
    }
    ~Impl() override { stopTimer(); }

    void timerCallback() override
    {
        const int cur = ctx.core.currentSong();
        if (cur != lastCurrent) { lastCurrent = cur; for (auto* r : rows) r->repaint(); }
    }

    int songCount() const { return (int) ctx.core.session().songs.songs.size(); }

    void rebuild()
    {
        for (auto* r : rows) r->alive = false;
        const auto s = ctx.core.session();
        rows.clear();
        for (int i = 0; i < (int) s.songs.songs.size(); ++i) {
            auto* r = rows.add(new Row(*this, i, s.songs.songs[(size_t) i]));
            list.addAndMakeVisible(r);
        }
        if (sel >= (int) rows.size()) sel = (int) rows.size() - 1;
        layoutList();
        list.repaint();
    }
    // Rebuilding destroys the row that triggered a click, so structural changes are deferred.
    void rebuildSoon()
    {
        for (auto* r : rows) r->alive = false;
        juce::MessageManager::callAsync([sp = juce::Component::SafePointer<SongMapPanel>(&owner)] { if (sp) sp->impl_->rebuild(); });
    }
    void layoutList()
    {
        list.setSize(juce::jmax(100, view.getMaximumVisibleWidth()), 28 + (int) rows.size() * (kRowH + 4) + 8);
        list.resized();
    }
    void say(const juce::String& t, bool bad = false)
    {
        status.setText(t, juce::dontSendNotification);
        status.setColour(juce::Label::textColourId, bad ? colours::amber : colours::dim);
    }

    void commitAll() { for (auto* r : rows) r->commit(); }

    void addSong()
    {
        commitAll();
        Song n; n.name = "Song " + std::to_string(songCount() + 1);
        ctx.core.editSession([&](SessionSettings& s) { s.songs.songs.push_back(n); });
        sel = songCount() - 1; rebuildSoon();
    }
    void duplicateSong()
    {
        commitAll();
        if (sel < 0 || sel >= songCount()) { say("Select a row first (click it).", true); return; }
        ctx.core.editSession([&](SessionSettings& s) {
            Song c = s.songs.songs[(size_t) sel]; c.name += " copy";
            s.songs.songs.insert(s.songs.songs.begin() + sel + 1, c);
            if (s.songs.current > sel) ++s.songs.current;
        });
        ++sel; rebuildSoon();
    }
    void removeSong()
    {
        commitAll();
        if (sel < 0 || sel >= songCount()) { say("Select a row first (click it).", true); return; }
        ctx.core.editSession([&](SessionSettings& s) {
            s.songs.songs.erase(s.songs.songs.begin() + sel);
            if (s.songs.current == sel) s.songs.current = -1; else if (s.songs.current > sel) --s.songs.current;
        });
        rebuildSoon();
    }
    void moveSong(int d)
    {
        commitAll();
        const int to = sel + d;
        if (sel < 0 || sel >= songCount() || to < 0 || to >= songCount()) return;
        ctx.core.editSession([&](SessionSettings& s) {
            std::swap(s.songs.songs[(size_t) sel], s.songs.songs[(size_t) to]);
            if (s.songs.current == sel) s.songs.current = to; else if (s.songs.current == to) s.songs.current = sel;
        });
        sel = to; rebuildSoon();
    }

    void importCsv()
    {
        chooser = std::make_unique<juce::FileChooser>("Import song list", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.csv;*.txt");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [sp = juce::Component::SafePointer<SongMapPanel>(&owner)](const juce::FileChooser& fc) {
                if (!sp) return;
                auto& im = *sp->impl_;
                const auto f = fc.getResult();
                if (f == juce::File()) return;
                SongMap m; std::string err;
                if (!SongMap::fromCsv(f.loadFileAsString().toStdString(), m, &err)) {
                    im.say("Import failed: " + (err.empty() ? juce::String("unrecognised format") : juce::String(err)), true); return;
                }
                const int n = (int) m.songs.size();
                im.ctx.core.editSession([&](SessionSettings& s) { s.songs.songs = m.songs; s.songs.current = -1; });
                im.sel = -1; im.say("Imported " + juce::String(n) + " songs from " + f.getFileName());
                im.rebuild();
            });
    }
    void exportCsv()
    {
        commitAll();
        chooser = std::make_unique<juce::FileChooser>("Export song list", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("songs.csv"), "*.csv");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [sp = juce::Component::SafePointer<SongMapPanel>(&owner)](const juce::FileChooser& fc) {
                if (!sp) return;
                auto& im = *sp->impl_;
                auto f = fc.getResult();
                if (f == juce::File()) return;
                if (f.getFileExtension().isEmpty()) f = f.withFileExtension("csv");
                const bool ok = f.replaceWithText(juce::String(im.ctx.core.session().songs.toCsv()));
                im.say(ok ? "Exported to " + f.getFullPathName() : "Could not write " + f.getFullPathName(), !ok);
            });
    }

    void layout()
    {
        auto r = owner.getLocalBounds().reduced(8, 4);
        auto bar = r.removeFromTop(38);
        const int widths[] = { 70, 100, 90, 60, 70, 120, 120 };
        for (int i = 0; i < buttons.size(); ++i) { buttons[i]->setBounds(bar.removeFromLeft(widths[i]).reduced(2, 2)); }
        status.setBounds(r.removeFromBottom(26));
        r.removeFromTop(4);
        view.setBounds(r);
        layoutList();
    }
};

SongMapPanel::SongMapPanel(UiContext& c) : PanelBase(c), impl_(std::make_unique<Impl>(*this, c)) {}
SongMapPanel::~SongMapPanel() = default;
void SongMapPanel::resized() { impl_->layout(); }

} // namespace pacemaker::ui
