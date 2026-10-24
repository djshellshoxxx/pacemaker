#include "AutoStart.h"

namespace pacemaker {

static juce::File exeFile() { return juce::File::getSpecialLocation(juce::File::currentExecutableFile); }

#if JUCE_WINDOWS
static const char* kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\\Pacemaker";
bool setAutoStart(bool enable)
{
    if (enable) return juce::WindowsRegistry::setValue(kRunKey, "\"" + exeFile().getFullPathName() + "\"", juce::WindowsRegistry::WoW64_Default);
    return juce::WindowsRegistry::deleteValue(kRunKey, juce::WindowsRegistry::WoW64_Default);
}
bool isAutoStartEnabled() { return juce::WindowsRegistry::valueExists(kRunKey, juce::WindowsRegistry::WoW64_Default); }
#elif JUCE_MAC
static juce::File agentFile() { return juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/LaunchAgents/com.pacemaker.standalone.plist"); }
bool setAutoStart(bool enable)
{
    if (!enable) return agentFile().deleteFile();
    juce::String plist = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                         "<plist version=\"1.0\"><dict><key>Label</key><string>com.pacemaker.standalone</string><key>ProgramArguments</key><array><string>"
                         + exeFile().getFullPathName() + "</string></array><key>RunAtLoad</key><true/></dict></plist>\n";
    agentFile().getParentDirectory().createDirectory();
    return agentFile().replaceWithText(plist);
}
bool isAutoStartEnabled() { return agentFile().existsAsFile(); }
#else
static juce::File desktopFile() { return juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".config/autostart/pacemaker.desktop"); }
bool setAutoStart(bool enable)
{
    if (!enable) return desktopFile().deleteFile();
    desktopFile().getParentDirectory().createDirectory();
    return desktopFile().replaceWithText("[Desktop Entry]\nType=Application\nName=Pacemaker\nExec=\"" + exeFile().getFullPathName() + "\"\nX-GNOME-Autostart-enabled=true\n");
}
bool isAutoStartEnabled() { return desktopFile().existsAsFile(); }
#endif

} // namespace pacemaker
