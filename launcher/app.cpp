// app.cpp -- the Test Drive III Enhanced launcher: the game folder, the start choices
// and the options, then testdrive3-enhanced.
#include <wx/app.h>

#include "launcher.h"

class LauncherApp : public wxApp {
public:
    bool OnInit() override {
        SetAppName("Test Drive III Enhanced");
        SetVendorName("Krzysztof Kania");
        if (!wxApp::OnInit()) return false;
        auto* frame = new LauncherFrame;
        SetTopWindow(frame);
        frame->Show();
        return true;
    }
};

wxIMPLEMENT_APP(LauncherApp);
