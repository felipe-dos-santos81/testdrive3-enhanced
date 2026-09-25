// launcher.cpp -- the launcher window.
#include "launcher.h"

#include <wx/artprov.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/hyperlink.h>
#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbmp.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#ifdef __WXMSW__
#include <wx/msw/wrapcctl.h>
#include <shellapi.h>
#endif

#include "icon.h"
#include "settings.h"
#include "version.h"

const char* const APP_TITLE = "Test Drive III Enhanced";

namespace {

const char* const WEBSITE = "https://kkania.com";
const char* const SUPPORT = "https://buymeacoffee.com/krzysztofkania";
const char* const SECTION = "Game";

const int MIN_SCALE = 1, MAX_SCALE = 6, DEFAULT_SCALE = 3;
// The game's speed: timer ticks (145.6 a second) per frame while driving (testdrive3-enhanced --frame-ticks).
const int MIN_TICKS = 5, MAX_TICKS = 40, DEFAULT_TICKS = 14;
// The enhanced view: the picture's resolution (320x200 times this), anti-aliasing (samples per pixel along each
// axis) and how far the smooth motion runs behind the game (percent of a game frame; presets).
const int MIN_RES = 1, MAX_RES = 8, DEFAULT_RES = 4;
const int MAX_AA = 4, DEFAULT_AA = 2;
struct MotionPreset { int delay; const char* name; };
const MotionPreset MOTION[] = {
    {100, "Smooth (recommended)"},
    {50, "Balanced: half a frame guessed ahead"},
    {0, "Most direct: a whole frame guessed ahead"},
};
// Distance haze: percent of the horizon's sky colour on what is farthest away (presets; the first is the default).
struct HazePreset { int percent; const char* name; };
const HazePreset HAZE[] = {
    {30, "Normal"},
    {15, "Light"},
    {50, "Strong"},
    {0, "Off (as the original)"},
};

#ifdef __WXMSW__
HRESULT CALLBACK AboutCallback(HWND hwnd, UINT msg, WPARAM, LPARAM lp, LONG_PTR) {
    if (msg == TDN_HYPERLINK_CLICKED)
        ShellExecuteW(hwnd, L"open", reinterpret_cast<LPCWSTR>(lp), nullptr, nullptr, SW_SHOWNORMAL);
    return S_OK;
}
#else
// A label and a link on one line, for the portable About box.
void AddLink(wxWindow* parent, wxSizer* sizer, const wxString& label, const wxString& text, const wxString& url) {
    auto* line = new wxBoxSizer(wxHORIZONTAL);
    line->Add(new wxStaticText(parent, wxID_ANY, label + " "), 0, wxALIGN_CENTER_VERTICAL);
    line->Add(new wxHyperlinkCtrl(parent, wxID_ANY, text, url), 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(line);
}
#endif

wxStaticText* GreyText(wxWindow* parent, const wxString& text = wxEmptyString) {
    auto* label = new wxStaticText(parent, wxID_ANY, text);
    label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    return label;
}

// A label, a text field and a Browse button in a row of a two-column grid.
wxTextCtrl* PathRow(wxWindow* parent, wxFlexGridSizer* grid, const wxString& label, const wxString& tip,
                    wxButton** browse) {
    grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    auto* text = new wxTextCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(parent->FromDIP(300), -1));
    text->SetToolTip(tip);
    *browse = new wxButton(parent, wxID_ANY, "B&rowse...");
    row->Add(text, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, parent->FromDIP(8));
    row->Add(*browse, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(row, 1, wxEXPAND);
    return text;
}

// A label and a choice in a row of a two-column grid.
wxChoice* ChoiceRow(wxWindow* parent, wxFlexGridSizer* grid, const wxString& label, const wxString& tip) {
    grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
    auto* choice = new wxChoice(parent, wxID_ANY, wxDefaultPosition, wxSize(parent->FromDIP(220), -1));
    choice->SetToolTip(tip);
    grid->Add(choice, 0, wxALIGN_CENTER_VERTICAL);
    return choice;
}

// Fills a slot list: "Default (...)" first, then the slots; selects `code` (Default when it isn't
// there). `last` is the game's own choice, shown in the Default entry.
void FillSlotChoice(wxChoice* choice, const std::vector<Slot>& slots, int last, wxString& code) {
    choice->Clear();
    choice->Append(last >= 0 ? wxString::Format("Default (the game's last choice: %s)", slots[last].name)
                             : wxString("Default (the game's last choice)"));
    int pick = 0;
    for (size_t i = 0; i < slots.size(); ++i) {
        choice->Append(slots[i].name);
        if (slots[i].code.CmpNoCase(code) == 0) pick = static_cast<int>(i) + 1;
    }
    choice->SetSelection(pick);
    if (pick == 0) code.clear();
}

}  // namespace

LauncherDialog::LauncherDialog()
    : wxDialog(nullptr, wxID_ANY, APP_TITLE, wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxMINIMIZE_BOX) {
    SetIcons(AppIcons());
    const int margin = FromDIP(12), gap = FromDIP(8), small = FromDIP(4);

    // Game files
    auto* filesBox = new wxStaticBoxSizer(wxVERTICAL, this, "Game files");
    wxWindow* fb = filesBox->GetStaticBox();
    auto* filesGrid = new wxFlexGridSizer(2, gap, gap);
    filesGrid->AddGrowableCol(1);
    wxButton* browseFolder = nullptr;
    wxButton* browseProgram = nullptr;
    folder_ = PathRow(fb, filesGrid, "&Folder:", "The folder with the original game's files (TDIII.EXE and the rest).",
                      &browseFolder);
    program_ = PathRow(fb, filesGrid, "&Program:", "testdrive3-enhanced, the game.", &browseProgram);
    auto* statusRow = new wxBoxSizer(wxHORIZONTAL);
    statusIcon_ = new wxStaticBitmap(fb, wxID_ANY, wxArtProvider::GetBitmapBundle(wxART_WARNING, wxART_MENU));
    statusNote_ = new wxStaticText(fb, wxID_ANY, wxEmptyString);
    statusRow->Add(statusIcon_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, small);
    statusRow->Add(statusNote_, 1, wxALIGN_CENTER_VERTICAL);
    filesBox->Add(filesGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);
    filesBox->Add(statusRow, 0, wxEXPAND | wxALL, gap);
    folder_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
        if (!loading_) Reload();
    });
    program_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
        if (!loading_) UpdateState();
    });
    browseFolder->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { BrowseFolder(); });
    browseProgram->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { BrowseProgram(); });

    // Start
    auto* startBox = new wxStaticBoxSizer(wxVERTICAL, this, "Start");
    wxWindow* sb = startBox->GetStaticBox();
    startBox->Add(GreyText(sb, "As if chosen in the game's menus."), 0,
                  wxLEFT | wxRIGHT | wxTOP, gap);
    auto* startGrid = new wxFlexGridSizer(2, gap, gap);
    car_ = ChoiceRow(sb, startGrid, "&Car:", "The car the game starts with.");
    course_ = ChoiceRow(sb, startGrid, "C&ourse:", "The course (scenery) the game starts with.");
    skill_ = ChoiceRow(sb, startGrid, "&Skill level:",
                       "The game's skill level. 1-3 have an automatic gearbox; from 4 on you shift yourself and "
                       "over-revving damages the engine.");
    startBox->Add(startGrid, 0, wxALL, gap);
    car_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        const int i = car_->GetSelection();
        carCode_ = i > 0 ? catalogue_.cars[i - 1].code : wxString();
    });
    course_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        const int i = course_->GetSelection();
        courseCode_ = i > 0 ? catalogue_.courses[i - 1].code : wxString();
    });

    // Options
    auto* optionsBox = new wxStaticBoxSizer(wxVERTICAL, this, "Options");
    wxWindow* ob = optionsBox->GetStaticBox();
    auto* grid = new wxFlexGridSizer(2, gap, gap);
    grid->Add(new wxStaticText(ob, wxID_ANY, "Game &speed:"), 0, wxALIGN_CENTER_VERTICAL);
    auto* speedRow = new wxBoxSizer(wxHORIZONTAL);
    speed_ = new wxSpinCtrl(ob, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(64), -1),
                            wxSP_ARROW_KEYS, MIN_TICKS, MAX_TICKS, DEFAULT_TICKS);
    speed_->SetToolTip("Timer ticks per frame while driving (145.6 ticks a second). The game moves the cars "
                       "once per frame, so fewer ticks make everything faster. The race clock always counts real "
                       "seconds. 14 is the recommended speed; at 10 the scenery passes about as fast as the "
                       "speedometer says; 23 is the faithful port's default; 5 is the original program's limit.");
    speedRow->Add(speed_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
    speedRow->Add(GreyText(ob, "ticks a frame: 14 recommended (fewer = faster)"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(speedRow, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(ob, wxID_ANY, "So&und:"), 0, wxALIGN_CENTER_VERTICAL);
    sound_ = new wxChoice(ob, wxID_ANY);
    sound_->Append("AdLib / Sound Blaster");
    sound_->Append("PC speaker");
    sound_->SetToolTip("The sound device the game uses. Ctrl+S and Ctrl+Q switch sound and music in the game.");
    grid->Add(sound_, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(ob, wxID_ANY, "&Window size:"), 0, wxALIGN_CENTER_VERTICAL);
    scale_ = new wxChoice(ob, wxID_ANY);
    for (int s = MIN_SCALE; s <= MAX_SCALE; ++s) scale_->Append(wxString::Format(L"%d × %d", 320 * s, 240 * s));
    scale_->SetToolTip("The window's size when the game starts. Alt+Enter switches to full screen.");
    grid->Add(scale_, 0, wxALIGN_CENTER_VERTICAL);
    optionsBox->Add(grid, 0, wxLEFT | wxRIGHT | wxTOP, gap);
    fullscreen_ = new wxCheckBox(ob, wxID_ANY, "Start in f&ull screen (Alt+Enter switches)");
    optionsBox->Add(fullscreen_, 0, wxALL, gap);

    // Enhanced view
    auto* viewBox = new wxStaticBoxSizer(wxVERTICAL, this, "Picture");
    wxWindow* vb = viewBox->GetStaticBox();
    auto* viewGrid = new wxFlexGridSizer(2, gap, gap);
    graphics_ = ChoiceRow(vb, viewGrid, "&Graphics:",
                          "Enhanced: the 3D view and the mirror drawn again at the resolution below, anti-aliased, "
                          "and moving smoothly at the screen's rate between the game's own frames (about 6 a "
                          "second). Original: the game's own 320 x 200 picture, scaled up.");
    graphics_->Append("Enhanced: smooth, high resolution");
    graphics_->Append("Original: 320 x 200, as the game drew it");
    resolution_ = ChoiceRow(vb, viewGrid, "&Resolution:",
                            "The size of the picture the game draws. The window shows it scaled to fit; lower it "
                            "on a slow computer.");
    for (int r = MIN_RES; r <= MAX_RES; ++r)
        resolution_->Append(wxString::Format(L"%d × %d%s", 320 * r, 200 * r, r == 1 ? " (original)" : ""));
    aa_ = ChoiceRow(vb, viewGrid, "&Anti-aliasing:",
                    "Smooth polygon edges: every pixel of the 3D view is the average of several samples.");
    aa_->Append("Off");
    for (int a = 2; a <= MAX_AA; ++a) aa_->Append(wxString::Format(L"%d × %d samples", a, a));
    motion_ = ChoiceRow(vb, viewGrid, "&Motion:",
                        "The game moves everything about 6 times a second; the enhanced view moves the camera and "
                        "the cars smoothly in between. Smooth moves between the game's last two frames (never "
                        "guesses, so small steering corrections stay smooth); Balanced and Most direct guess half "
                        "or a whole frame ahead, which answers sooner but overshoots when the steering changes.");
    for (const auto& m : MOTION) motion_->Append(m.name);
    haze_ = ChoiceRow(vb, viewGrid, "Ha&ze:",
                      "What is far away takes on some of the sky's colour at the horizon, the ground too: the "
                      "distance reads better and the scenery appearing at the edge of the view stands out less.");
    for (const auto& h : HAZE) haze_->Append(h.name);
    viewBox->Add(viewGrid, 0, wxALL, gap);
    graphics_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdateState(); });

    // Keys
    auto* keysBox = new wxStaticBoxSizer(wxVERTICAL, this, "Keys in the game");
    wxWindow* kb = keysBox->GetStaticBox();
    auto* keys = new wxFlexGridSizer(2, small, FromDIP(16));
    const char* const KEYS[][2] = {
        {"Arrows / keypad", "steer, accelerate and brake"},
        {"A / Z", "gear up / down (Enter + Up/Down: the gear gates)"},
        {"R  H  W", "mirror, headlights, wipers"},
        {"C  M", "wheel centring, radio station"},
        {"F1  F2  F3", "window size, detail, steering sensitivity"},
        {"F5  F6", "chase car view, return to the road"},
        {"F7", "mouse steering"},
        {"F10  F9", "instant replay, pause the replay"},
        {"Esc", "leave the race or the game"},
        {"Ctrl+P  Ctrl+S  Ctrl+Q", "pause, sound, music"},
        {"Ctrl+J  Ctrl+K", "joystick (a gamepad) / keyboard"},
        {"Alt+Enter", "full screen"},
    };
    for (const auto& k : KEYS) {
        keys->Add(new wxStaticText(kb, wxID_ANY, k[0]));
        keys->Add(GreyText(kb, k[1]));
    }
    keysBox->Add(keys, 0, wxALL, gap);

    // Buttons
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* about = new wxButton(this, wxID_ABOUT, "&About");
    play_ = new wxButton(this, wxID_ANY, "&Play");
    auto* close = new wxButton(this, wxID_CLOSE, "Close");
    buttons->Add(about);
    buttons->AddStretchSpacer();
    buttons->Add(play_, 0, wxRIGHT, gap);
    buttons->Add(close);
    play_->SetDefault();
    SetEscapeId(wxID_CLOSE);
    about->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { About(); });
    play_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Play(); });
    close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); });

    // Two columns: the files and the start on the left, the options and the keys on the right.
    auto* left = new wxBoxSizer(wxVERTICAL);
    left->Add(filesBox, 0, wxEXPAND);
    left->Add(startBox, 0, wxEXPAND | wxTOP, margin);
    left->Add(optionsBox, 1, wxEXPAND | wxTOP, margin);
    auto* right = new wxBoxSizer(wxVERTICAL);
    right->Add(viewBox, 0, wxEXPAND);
    right->Add(keysBox, 1, wxEXPAND | wxTOP, margin);
    auto* columns = new wxBoxSizer(wxHORIZONTAL);
    columns->Add(left, 0, wxEXPAND);
    columns->Add(right, 0, wxEXPAND | wxLEFT, margin);
    auto* all = new wxBoxSizer(wxVERTICAL);
    all->Add(columns, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, margin);
    all->Add(buttons, 0, wxEXPAND | wxALL, margin);
    SetSizer(all);

    // Settings from the last run.
    wxString dir = settings::GetString(SECTION, "GameFolder", "");
    wxString program = settings::GetString(SECTION, "Program", "");
    folder_->ChangeValue(dir.empty() ? DefaultGameDir() : dir);
    program_->ChangeValue(program.empty() ? DefaultProgram() : program);
    carCode_ = settings::GetString(SECTION, "Car", "");
    courseCode_ = settings::GetString(SECTION, "Course", "");
    skill_->Append("Default (the game's last choice)");
    for (int s = 1; s <= 9; ++s) skill_->Append(wxString::Format(s <= 3 ? "%d (automatic gearbox)" : "%d", s));
    skill_->SetSelection(wxMax(0, wxMin(9, settings::GetInt(SECTION, "Skill", 0))));
    speed_->SetValue(wxMax(MIN_TICKS, wxMin(MAX_TICKS, settings::GetInt(SECTION, "GameSpeed", DEFAULT_TICKS))));
    sound_->SetSelection(settings::GetInt(SECTION, "Speaker", 0) != 0 ? 1 : 0);
    scale_->SetSelection(
        wxMax(MIN_SCALE, wxMin(MAX_SCALE, settings::GetInt(SECTION, "Scale", DEFAULT_SCALE))) - MIN_SCALE);
    fullscreen_->SetValue(settings::GetInt(SECTION, "Fullscreen", 0) != 0);
    graphics_->SetSelection(settings::GetInt(SECTION, "Classic", 0) != 0 ? 1 : 0);
    resolution_->SetSelection(
        wxMax(MIN_RES, wxMin(MAX_RES, settings::GetInt(SECTION, "Resolution", DEFAULT_RES))) - MIN_RES);
    aa_->SetSelection(wxMax(1, wxMin(MAX_AA, settings::GetInt(SECTION, "AntiAliasing", DEFAULT_AA))) - 1);
    const int delay = settings::GetInt(SECTION, "Motion", MOTION[0].delay);
    motion_->SetSelection(0);
    for (size_t i = 0; i < sizeof MOTION / sizeof MOTION[0]; ++i)
        if (MOTION[i].delay == delay) motion_->SetSelection(static_cast<int>(i));
    const int haze = settings::GetInt(SECTION, "Haze", HAZE[0].percent);
    haze_->SetSelection(0);
    for (size_t i = 0; i < sizeof HAZE / sizeof HAZE[0]; ++i)
        if (HAZE[i].percent == haze) haze_->SetSelection(static_cast<int>(i));
    loading_ = false;
    Reload();

    Bind(wxEVT_ACTIVATE, [this](wxActivateEvent& event) {
        if (event.GetActive()) Reload();  // files may have been copied in meanwhile
        event.Skip();
    });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) {
        Save();
        Destroy();
    });

    Fit();
    if (!settings::RestoreWindowPosition(SECTION, this)) Centre();
}

void LauncherDialog::Reload() {
    catalogue_ = ReadCatalogue(folder_->GetValue());
    FillSlotChoice(car_, catalogue_.cars, catalogue_.lastCar, carCode_);
    FillSlotChoice(course_, catalogue_.courses, catalogue_.lastCourse, courseCode_);
    skill_->SetString(0, catalogue_.lastSkill > 0
                             ? wxString::Format("Default (the game's last choice: %d)", catalogue_.lastSkill)
                             : wxString("Default (the game's last choice)"));
    UpdateState();
}

void LauncherDialog::UpdateState() {
    const wxString dir = folder_->GetValue(), program = program_->GetValue();
    const bool haveProgram = wxFileName::FileExists(program);
    const wxString missing = MissingGameFile(dir);
    wxString note;
    if (!missing.empty())
        note = wxString::Format("This folder needs the game's files: %s is missing.", missing);
    else if (catalogue_.cars.empty() || catalogue_.courses.empty())
        note = "PLAYDISK.DAT lists no car or course whose .LST file is in this folder.";
    else if (!haveProgram)
        note = wxString::Format("%s isn't there.", wxFileName(program).GetFullName());
    else
        note = wxString::Format("Found %d %s and %d %s.", static_cast<int>(catalogue_.cars.size()),
                                catalogue_.cars.size() == 1 ? "car" : "cars",
                                static_cast<int>(catalogue_.courses.size()),
                                catalogue_.courses.size() == 1 ? "course" : "courses");
    const bool ok = haveProgram && missing.empty() && !catalogue_.cars.empty() && !catalogue_.courses.empty();
    statusIcon_->Show(!ok);
    statusNote_->SetLabel(note);
    const bool enhanced = graphics_->GetSelection() == 0;
    aa_->Enable(enhanced);
    motion_->Enable(enhanced);
    haze_->Enable(enhanced);
    car_->Enable(!catalogue_.cars.empty());
    course_->Enable(!catalogue_.courses.empty());
    play_->Enable(ok);
    Layout();
}

void LauncherDialog::BrowseFolder() {
    wxDirDialog dialog(this, "Choose the folder with the game's files", folder_->GetValue(),
                       wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) folder_->SetValue(dialog.GetPath());  // raises wxEVT_TEXT
}

void LauncherDialog::BrowseProgram() {
    wxFileName current(program_->GetValue());
#ifdef __WXMSW__
    const char* const filter = "Programs (*.exe)|*.exe|All files (*.*)|*.*";
#else
    const char* const filter = "All files|*";
#endif
    wxFileDialog dialog(this, "Choose testdrive3-enhanced", current.GetPath(), current.GetFullName(), filter,
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) program_->SetValue(dialog.GetPath());  // raises wxEVT_TEXT
}

void LauncherDialog::Play() {
    Save();
    GameOptions options;
    options.program = wxFileName(program_->GetValue()).GetFullPath();
    options.gameDir = wxFileName(folder_->GetValue()).GetFullPath();
    options.car = carCode_;
    options.course = courseCode_;
    options.skill = skill_->GetSelection();
    options.frameTicks = speed_->GetValue();
    options.speaker = sound_->GetSelection() == 1;
    options.scale = scale_->GetSelection() + MIN_SCALE;
    options.fullscreen = fullscreen_->GetValue();
    options.classic = graphics_->GetSelection() == 1;
    options.resScale = resolution_->GetSelection() + MIN_RES;
    options.aa = aa_->GetSelection() + 1;
    options.motionDelay = MOTION[wxMax(0, motion_->GetSelection())].delay;
    options.haze = HAZE[wxMax(0, haze_->GetSelection())].percent;
    wxString error;
    if (!LaunchGame(options, error)) wxMessageBox(error, APP_TITLE, wxOK | wxICON_ERROR, this);
}

void LauncherDialog::Save() {
    // A folder or program left at its default is stored empty, so it follows the launcher if it moves.
    const wxString dir = folder_->GetValue(), program = program_->GetValue();
    settings::SetString(SECTION, "GameFolder",
                        wxFileName(dir).SameAs(wxFileName(DefaultGameDir())) ? wxString() : dir);
    settings::SetString(SECTION, "Program",
                        wxFileName(program).SameAs(wxFileName(DefaultProgram())) ? wxString() : program);
    settings::SetString(SECTION, "Car", carCode_);
    settings::SetString(SECTION, "Course", courseCode_);
    settings::SetInt(SECTION, "Skill", skill_->GetSelection());
    settings::SetInt(SECTION, "GameSpeed", speed_->GetValue());
    settings::SetInt(SECTION, "Speaker", sound_->GetSelection() == 1 ? 1 : 0);
    settings::SetInt(SECTION, "Scale", scale_->GetSelection() + MIN_SCALE);
    settings::SetInt(SECTION, "Fullscreen", fullscreen_->GetValue() ? 1 : 0);
    settings::SetInt(SECTION, "Classic", graphics_->GetSelection() == 1 ? 1 : 0);
    settings::SetInt(SECTION, "Resolution", resolution_->GetSelection() + MIN_RES);
    settings::SetInt(SECTION, "AntiAliasing", aa_->GetSelection() + 1);
    settings::SetInt(SECTION, "Motion", MOTION[wxMax(0, motion_->GetSelection())].delay);
    settings::SetInt(SECTION, "Haze", HAZE[wxMax(0, haze_->GetSelection())].percent);
    settings::SaveWindowPosition(SECTION, this);
}

void LauncherDialog::About() {
    const wxString title = wxString("About ") + APP_TITLE;
    const wxString heading = wxString(APP_TITLE) + " " + APP_VERSION_TEXT;
    const wxString blurb = "Starts testdrive3-enhanced, Test Drive III: The Passion (Accolade, 1990) on SDL3 with a smooth, high-resolution view.";
#ifdef __WXMSW__
    // The Windows task dialog.
    const wxString content = wxString::Format(
        "%s\n\n"
        "Author: Krzysztof Kania\n"
        "Website: <a href=\"%s\">kkania.com</a>\n"
        "Support: <a href=\"%s\">buymeacoffee.com/krzysztofkania</a>",
        blurb, WEBSITE, SUPPORT);
    wxIcon icon;
    icon.CopyFromBitmap(AppBitmap(FromDIP(32)));
    TASKDIALOGCONFIG dialog{};
    dialog.cbSize = sizeof dialog;
    dialog.hwndParent = static_cast<HWND>(GetHWND());
    dialog.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_USE_HICON_MAIN | TDF_ALLOW_DIALOG_CANCELLATION |
                     TDF_POSITION_RELATIVE_TO_WINDOW;
    dialog.dwCommonButtons = TDCBF_OK_BUTTON;
    dialog.pszWindowTitle = title.wc_str();
    dialog.hMainIcon = static_cast<HICON>(icon.GetHICON());
    dialog.pszMainInstruction = heading.wc_str();
    dialog.pszContent = content.wc_str();
    dialog.pfCallback = AboutCallback;
    TaskDialogIndirect(&dialog, nullptr, nullptr, nullptr);
#else
    wxDialog dialog(this, wxID_ANY, title);
    auto* body = new wxBoxSizer(wxHORIZONTAL);
    body->Add(new wxStaticBitmap(&dialog, wxID_ANY, AppBitmap(dialog.FromDIP(48))), 0, wxALL, dialog.FromDIP(12));

    auto* text = new wxBoxSizer(wxVERTICAL);
    auto* headingText = new wxStaticText(&dialog, wxID_ANY, heading);
    headingText->SetFont(dialog.GetFont().Bold().Scaled(1.3f));
    text->Add(headingText, 0, wxBOTTOM, dialog.FromDIP(8));
    text->Add(new wxStaticText(&dialog, wxID_ANY, blurb), 0, wxBOTTOM, dialog.FromDIP(12));
    text->Add(new wxStaticText(&dialog, wxID_ANY, "Author: Krzysztof Kania"));
    AddLink(&dialog, text, "Website:", "kkania.com", WEBSITE);
    AddLink(&dialog, text, "Support:", "buymeacoffee.com/krzysztofkania", SUPPORT);
    body->Add(text, 1, wxTOP | wxRIGHT | wxBOTTOM, dialog.FromDIP(12));

    auto* all = new wxBoxSizer(wxVERTICAL);
    all->Add(body, 1, wxEXPAND);
    all->Add(dialog.CreateStdDialogButtonSizer(wxOK), 0, wxEXPAND | wxALL, dialog.FromDIP(8));
    dialog.SetSizerAndFit(all);
    dialog.CentreOnParent();
    dialog.ShowModal();
#endif
}
