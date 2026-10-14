// Temporary minimal bodies; replaced by the real panels.
#include "Panels.h"
namespace pacemaker::ui {
struct SettingsPanel::Impl {};
SettingsPanel::SettingsPanel(UiContext& c, int) : PanelBase(c) {}
SettingsPanel::~SettingsPanel() = default;
void SettingsPanel::resized() {}
struct SongMapPanel::Impl {};
SongMapPanel::SongMapPanel(UiContext& c) : PanelBase(c) {}
SongMapPanel::~SongMapPanel() = default;
void SongMapPanel::resized() {}
struct CalibrationWizard::Impl {};
CalibrationWizard::CalibrationWizard(UiContext& c) : PanelBase(c) {}
CalibrationWizard::~CalibrationWizard() = default;
void CalibrationWizard::resized() {}
struct ReportPanel::Impl {};
ReportPanel::ReportPanel(UiContext& c) : PanelBase(c) {}
ReportPanel::~ReportPanel() = default;
void ReportPanel::resized() {}
}
