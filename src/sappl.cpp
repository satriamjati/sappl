#include "sappl.h"

SapplApp::SapplApp() : processOps_(data_), hWnd_(NULL) {
    data_.state = State::Idle;
}

SapplApp::~SapplApp() {
    systemOps_.CleanupResources();
    processOps_.StopKeepAlive();
    for (auto& thread : worker_) {
        if (thread.joinable()) {
            thread.request_stop();
        }
    }
    worker_.clear();
}

void SapplApp::Run() {
    hInst = reinterpret_cast<HINSTANCE>(GetModuleHandle(nullptr));
    if (!processOps_.GetRequiredPath()) {
        systemOps_.HandleError(ERROR_FILE_NOT_FOUND);
        return;
    }
    
    if (!InitInstance(hInst, SW_SHOW)) {
        systemOps_.HandleError(GetLastError());
        return;
    }
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

std::wstring SapplApp::ProcessOperations::GetAppDirectory() 
{
    wchar_t buffer[MAX_PATH];

    DWORD length = GetModuleFileNameW(
        nullptr,
        buffer,
        MAX_PATH
    );

    if (length == 0)
        return L"";

    std::wstring path(buffer, length);

    size_t pos = path.find_last_of(L"\\/");

    if (pos == std::wstring::npos)
        return L"";

    return path.substr(0, pos);
}

bool SapplApp::ProcessOperations::GetRequiredPath()
{
    const std::wstring basePath = GetAppDirectory();
    const std::wstring filenames[] = {
        L"scrcpy.exe",
        L"adb.exe"
    };

    std::wstring* requiredPaths[] = {
        &scrcpyPath_,
        &adbPath_
    };

    for (int i = 0; i < 2; ++i) {

        const std::wstring bundled =
            basePath + L"\\scrcpy\\" + filenames[i];

        if (GetFileAttributesW(bundled.c_str()) != INVALID_FILE_ATTRIBUTES) {
            *requiredPaths[i] = bundled;
            continue;
        }

        const std::wstring local =
            basePath + L"\\" + filenames[i];

        if (GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES) {
            *requiredPaths[i] = local;
            continue;
        }

        wchar_t buffer[MAX_PATH];

        const DWORD length = SearchPathW(
            nullptr,
            filenames[i].c_str(),
            nullptr,
            MAX_PATH,
            buffer,
            nullptr
        );

        if (length > 0 && length < MAX_PATH) {
            *requiredPaths[i] = buffer;
            continue;
        }

        return false;
    }

    return true;
}



std::wstring SapplApp::ProcessOperations::GetLaunchCommand(const DiscoveredApp& app) {
    std::wstring command = scrcpyPath_ +
        (data_.settings.forceUsb ? L" -d" : L"") +
        (data_.settings.alwaysOnTop ? L" --always-on-top" : L"");

    if (app.packageName.empty()) {
        command = command + L" --turn-screen-off";
        return command;
    }

    command = command + 
        (data_.settings.virtualDisplay ? L" --new-display" : L"") +
        (data_.settings.flexResolution ? L" -x" : L"") +
        L" --window-title=\"" + app.name + L"\"" +
        L" --start-app=" + app.packageName;
    return command;
}

bool SapplApp::ProcessOperations::Run(
    const std::wstring& command)
{
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo{};

    std::wstring mutableCommand = command;

    if (!CreateProcessW(
            nullptr,
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo)) {
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    return true;
}

bool SapplApp::ProcessOperations::RunAndRead(
    const std::wstring& command,
    std::wstring& output)
{
    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength =
        sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE readHandle = nullptr;
    HANDLE writeHandle = nullptr;

    if (!CreatePipe(
            &readHandle,
            &writeHandle,
            &securityAttributes,
            0)) {
        return false;
    }

    SetHandleInformation(
        readHandle,
        HANDLE_FLAG_INHERIT,
        0);

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdOutput = writeHandle;
    startupInfo.hStdError = writeHandle;

    PROCESS_INFORMATION processInfo{};

    std::wstring mutableCommand = command;

    if (!CreateProcessW(
            nullptr,
            mutableCommand.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo)) {

        CloseHandle(readHandle);
        CloseHandle(writeHandle);
        return false;
    }

    CloseHandle(writeHandle);

    output.clear();

    char buffer[4096];
    DWORD bytesRead = 0;

    while (ReadFile(
        readHandle,
        buffer,
        sizeof(buffer),
        &bytesRead,
        nullptr) &&
        bytesRead > 0) {

        output.append(Utf8ToWide(buffer, bytesRead));
    }

    CloseHandle(readHandle);

    WaitForSingleObject(
        processInfo.hProcess,
        INFINITE);

    DWORD exitCode = 1;

    GetExitCodeProcess(
        processInfo.hProcess,
        &exitCode);

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    return exitCode == 0;
}

void SapplApp::ProcessOperations::ResetKeepAlive(HWND hWnd) {
    StopKeepAlive();
    keepAliveThread_ = std::jthread([this,hWnd](std::stop_token stoken) {
        std::wstring command = scrcpyPath_ +
            L" -Sw --no-window --no-audio --keep-active";
        if (data_.settings.forceUsb) {
            command += L" -d";
        }
        if (Run(command)){
            PostMessage(hWnd, SapplApp::WM_KEEP_ALIVE, TRUE, 0);
        } else {
            PostMessage(hWnd, SapplApp::WM_KEEP_ALIVE, FALSE, 0);
        }
    });
}

void SapplApp::ProcessOperations::StopKeepAlive() {
    if (keepAliveThread_.joinable()) {
        keepAliveThread_.request_stop();
        keepAliveThread_ = {}; 
        data_.settings.keepAlive = false;
    }    
}


void SapplApp::Refresh(HWND hWnd) {
    uiOps_.DisableButton(uiOps_.BTN_REFRESH);
    uiOps_.UpdateStatusDisplay(data_.settings, RUNNING_STATE);
    worker_.emplace_back(std::jthread([this, hWnd]() {
        processOps_.ReloadAppList(hWnd);
        uiOps_.UpdateAppListDisplay(data_.discoveredApps);
        uiOps_.UpdateStatusDisplay(data_.settings);
        uiOps_.EnableButton(uiOps_.BTN_REFRESH);
    }));
}

bool SapplApp::ProcessOperations::ReloadAppList(HWND hWnd) 
{
    for (int i = 0; i < 2; ++i) {
        std::wstring command = scrcpyPath_ + L" --list-apps";
        if (data_.settings.forceUsb) {
            command += L" -d";
        }
        std::wstring output;

        if (!RunAndRead(
                std::wstring(command),
                output)) {
            data_.settings.forceUsb = !data_.settings.forceUsb;
            PostMessage(hWnd, SapplApp::WM_FORCE_USB, data_.settings.forceUsb, 0);
            continue;
        }

        ParseAppListOutput(output);

        if (!data_.discoveredApps.empty()) {
            return 0;
        }
        
    }

    return 0;
}




void SapplApp::ProcessOperations::ParseAppListOutput(const std::wstring& output){
    auto& apps = data_.discoveredApps;
    size_t position = 0;
    while (position < output.size())
    {
        size_t end = output.find('\n', position);

        if (end == std::string::npos)
            end = output.size();

        std::wstring line = output.substr(
            position,
            end - position
        );

        std::wstring type = L"";

        if (line.rfind(L" * ", 0) == 0){
            type = L"System";
        }
        else if (line.rfind(L" - ", 0) == 0){
            type = L"User";
        }

        if (type == L"System" || type == L"User")
        {
            std::wstring appLine = line.substr(3);

            while (!appLine.empty() &&
                    (appLine.back() == '\r' ||
                    appLine.back() == '\n'))
            {
                appLine.pop_back();
            }

            size_t separator =
                appLine.find_last_of(L" \t");

            if (separator != std::string::npos)
            {
                DiscoveredApp app;

                app.name = appLine.substr(
                    0,
                    separator
                );
                while (!app.name.empty() &&
                    (app.name.back() == ' ' ||
                    app.name.back() == '\t'))
                {
                    app.name.pop_back();
                }
                
                app.type = type;
                app.packageName = appLine.substr(
                    separator + 1
                );

                apps.push_back(app);
            }
        }

        position = end + 1;
    }
}

bool SapplApp::ProcessOperations::SetupWifi(HWND hWnd) {
    if (!Run(adbPath_ + L" tcpip 5555 -d"))
        return false;

    std::wstring phoneIp;
    for (int i = 0; i < 10; ++i)
    {
        phoneIp = GetPhoneIP();

        if (!phoneIp.empty())
            break;

        Sleep(500);
    }

    if (phoneIp.empty())
    {
        MessageBoxW(hWnd, L"Failed to get device IP. Connect your device to the same Wi-Fi network", L"Setup Wi-Fi", MB_OK | MB_ICONERROR);
        return false;
    }

    std::wstring command =
        adbPath_ + L" connect -d " + phoneIp + L":5555";

    for (int i = 0; i < 10; ++i)
    {
        if (Run(command))
            return true;

        Sleep(500);
    }

    return false;
};



std::wstring SapplApp::ProcessOperations::GetPhoneIP() {
    std::wstring output;
    RunAndRead(adbPath_ + L" -d wait-for-device shell ip -f inet addr show wlan0", output);
    size_t position = output.find(L"inet ");

    if (position == std::string::npos)
        return L"";

    position += 5; 

    size_t end = output.find(
        '/',
        position
    );

    if (end == std::string::npos)
        return L"";

    std::wstring ip =
        output.substr(
            position,
            end - position
        );

    return ip;
}

std::wstring SapplApp::ProcessOperations::Utf8ToWide(
    const char* data,
    int size)
{
    if (size <= 0)
        return {};

    int length = MultiByteToWideChar(
        CP_UTF8,
        0,
        data,
        size,
        nullptr,
        0);

    std::wstring result(length, L'\0');

    MultiByteToWideChar(
        CP_UTF8,
        0,
        data,
        size,
        result.data(),
        length);

    return result;
}

SapplApp::Theme SapplApp::UIOperations::GetSystemAppearanceMode() {
    HKEY key;
    DWORD value = static_cast<DWORD>(Theme::DARK);
    DWORD size = sizeof(value);
    DWORD type = REG_DWORD;  

    LONG openKey = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0,
        KEY_READ,
        &key
    );

    if (openKey == ERROR_SUCCESS) {
        LONG queryKey = RegQueryValueExW(
            key,
            L"AppsUseLightTheme",
            nullptr,
            &type,
            reinterpret_cast<LPBYTE>(&value),
            &size
        );

        RegCloseKey(key);

        if (queryKey == ERROR_SUCCESS && type == REG_DWORD) {
            return static_cast<Theme>(value);
        }
    }
    
    return Theme::DARK;
}

SapplApp::ProcessOperations::~ProcessOperations() {
    StopKeepAlive();
}


void SapplApp::UIOperations::InitializeUI() {
    ui_.darkBrush = CreateSolidBrush(RGB(32, 32, 32));
}

void SapplApp::UIOperations::SetWindowTheme(HWND hWnd) {
    Theme currentTheme = GetSystemAppearanceMode();
    BOOL dark = (currentTheme == Theme::DARK);

    DwmSetWindowAttribute(
        hWnd,
        DWMWA_USE_IMMERSIVE_DARK_MODE,
        &dark,
        sizeof(dark)
    );

    InvalidateRect(hWnd, nullptr, TRUE);
    UpdateWindow(hWnd);
}

void SapplApp::UIOperations::ConfigureLayout(HWND hwnd) {
    ui_.title = BuildEntity().NewWindow(
        L"STATIC",
        L"SAPPL",
        SS_CENTER,
        MARGIN,
        TITLE_TOP,
        960,
        TITLE_HEIGHT,
        hwnd
    );

    if (ui_.title) {
        ui_.titleFont = CreateFontW(
            32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI"
        );
        SendMessageW(
            ui_.title,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(ui_.titleFont),
            TRUE
        );
    }

    ui_.status = BuildEntity().NewWindow(
        L"STATIC",
        L"",
        SS_CENTER,
        MARGIN,
        STATUS_TOP,
        960,
        STATUS_HEIGHT,
        hwnd
    );

    int x = MARGIN;

    ui_.buttons[ButtonIndex::BTN_KEEP_ALIVE_RESET] = BuildEntity().NewButtonSmall(
        L"KA Reset", x, BUTTON_TOP, hwnd,
        reinterpret_cast<HMENU>(SapplApp::BTN_KEEP_ALIVE_RESET));

    x += SMALL_BUTTON_WIDTH + BUTTON_GAP;
    ui_.buttons[ButtonIndex::BTN_KEEP_ALIVE_OFF] = BuildEntity().NewButtonSmall(
        L"KA Off", x, BUTTON_TOP, hwnd,
        reinterpret_cast<HMENU>(SapplApp::BTN_KEEP_ALIVE_OFF));

    x += SMALL_BUTTON_WIDTH + BUTTON_GAP;

    ui_.buttons[ButtonIndex::BTN_REFRESH] = BuildEntity().NewButtonMedium(
        L"Refresh", x, BUTTON_TOP, hwnd, 
        reinterpret_cast<HMENU>(SapplApp::BTN_REFRESH));

    x += MEDIUM_BUTTON_WIDTH + BUTTON_GAP;

    ui_.buttons[ButtonIndex::BTN_FORCE_USB] = BuildEntity().NewButtonSmall(
        L"fUSB FT", x, BUTTON_TOP, hwnd, 
        reinterpret_cast<HMENU>(SapplApp::BTN_FORCE_USB));

    x += SMALL_BUTTON_WIDTH + BUTTON_GAP;

    ui_.buttons[ButtonIndex::BTN_SETUP_WIFI] = BuildEntity().NewButtonSmall(
        L"Setup WiFi", x, BUTTON_TOP, hwnd,
        reinterpret_cast<HMENU>(SapplApp::BTN_SETUP_WIFI));
    
    x = MARGIN;
    int searchBoxWidth = WINDOW_WIDTH - 2 * (MARGIN + TINY_BUTTON_WIDTH + BUTTON_GAP);
    ui_.textBoxes[TextBoxIndex::TXTB_SEARCH] = BuildEntity().NewWindow(
        L"EDIT", L"",
        WS_BORDER | ES_AUTOHSCROLL,
        x, SEARCH_TOP, searchBoxWidth, SEARCH_BOX_HEIGHT, hwnd
    );

    x += searchBoxWidth + BUTTON_GAP;
    ui_.buttons[ButtonIndex::BTN_SEARCH] = BuildEntity().NewButtonTiny(
        L"Search", x, SEARCH_TOP, hwnd,
        reinterpret_cast<HMENU>(SapplApp::BTN_SEARCH));    
    
    x += TINY_BUTTON_WIDTH + BUTTON_GAP;
    ui_.buttons[ButtonIndex::BTN_MIRROR] = BuildEntity().NewButtonTiny(
        L"Mirror", x, SEARCH_TOP, hwnd,
        reinterpret_cast<HMENU>(SapplApp::BTN_MIRROR));

    ui_.listBoxes[ListBoxIndex::LSTB_APP_LIST] = BuildEntity().NewWindow(
        L"LISTBOX", L"", 
        WS_VSCROLL | WS_BORDER |
                LBS_NOINTEGRALHEIGHT |
                LBS_NOTIFY |
                LBS_USETABSTOPS, 
        MARGIN, LIST_TOP, 0, 0, hwnd
    );
}

void SapplApp::UIOperations::PaintBackground(HWND hwnd, HDC hdc) {
    RECT rect;
    GetClientRect(hwnd, &rect);

    HBRUSH brush = CreateSolidBrush(
        (GetSystemAppearanceMode() == Theme::DARK)
            ? RGB(32, 32, 32)
            : RGB(255, 255, 255)
    );

    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
}

HBRUSH SapplApp::UIOperations::SetContainerTheme(HDC hdc) {
    Theme currentTheme = GetSystemAppearanceMode();
    if (currentTheme == Theme::DARK) {
        SetBkColor(hdc, RGB(32, 32, 32)); 
        SetTextColor(hdc, RGB(255, 255, 255));
        return ui_.darkBrush;

    } else {
        SetBkColor(hdc, RGB(255, 255, 255)); 
        SetTextColor(hdc, RGB(0, 0, 0)); 
        return static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    }
}

HBRUSH SapplApp::UIOperations::SetAppListTheme(HDC hdc) {
    Theme currentTheme = GetSystemAppearanceMode();
    if (currentTheme == Theme::DARK) {
        SetBkColor(hdc, RGB(30, 30, 30)); 
        SetTextColor(hdc, RGB(255, 255, 255)); 
        return ui_.darkBrush;
    } else {
        SetBkColor(hdc, RGB(255, 255, 255)); 
        SetTextColor(hdc, RGB(0, 0, 0)); 
        return static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    }
}
bool SapplApp::UIOperations::DrawButtons(LPARAM lParam) {
    auto* drawItem = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
    bool disabled = (drawItem->itemState & ODS_DISABLED) != 0;
    const wchar_t* text = nullptr;

    switch (drawItem->CtlID) {
        case SapplApp::BTN_REFRESH:
            text = disabled ? L"Refreshing..." : L"Refresh";
            break;
        case SapplApp::BTN_KEEP_ALIVE_RESET:
            text = L"KA Reset";
            break;
        case SapplApp::BTN_KEEP_ALIVE_OFF:
            text = L"KA Off";
            break;
        case SapplApp::BTN_FORCE_USB:
            text = L"fUSB FT";
            break;
        case SapplApp::BTN_SETUP_WIFI:
            text = L"Set WiFi";
            break;
        case SapplApp::BTN_MIRROR:
            text = L"F 11";
            break;
        case SapplApp::BTN_SEARCH:
            text = L"Search";
            break;
        default:
            return false; 
    }

    HDC deviceContext = drawItem->hDC;
    RECT rect = drawItem->rcItem;
    bool darkMode = (GetSystemAppearanceMode() == Theme::DARK);

    COLORREF bgColor = darkMode
        ? (disabled ? RGB(45, 45, 45) : RGB(55, 55, 55))
        : (disabled ? RGB(220, 220, 220) : RGB(245, 245, 245));

    COLORREF textColor = darkMode
        ? (disabled ? RGB(120, 120, 120) : RGB(255, 255, 255))
        : (disabled ? RGB(140, 140, 140) : RGB(0, 0, 0));

    HBRUSH backgroundBrush = CreateSolidBrush(bgColor);
    FillRect(deviceContext, &rect, backgroundBrush);
    DeleteObject(backgroundBrush);

    FrameRect(
        deviceContext,
        &rect,
        static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH))
    );

    SetBkMode(deviceContext, TRANSPARENT);
    SetTextColor(deviceContext, textColor);

    DrawTextW(
        deviceContext,
        text,
        -1,
        &rect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
    );

    return true;
}

void SapplApp::UIOperations::DynamicResize(HWND hwnd, int width, int height) {
    int totalWidth = (SMALL_BUTTON_WIDTH+ BUTTON_GAP)*4 + MEDIUM_BUTTON_WIDTH;

    int x =
        (width - totalWidth) / 2;

    MoveWindow(
        ui_.title,
        MARGIN,
        TITLE_TOP,
        width - MARGIN * 2,
        TITLE_HEIGHT,
        TRUE
    );

    MoveWindow(
        ui_.status,
        MARGIN,
        STATUS_TOP,
        width - MARGIN * 2,
        STATUS_HEIGHT,
        TRUE
    );

    MoveWindow(
        ui_.buttons[ButtonIndex::BTN_KEEP_ALIVE_RESET],
        x,
        BUTTON_TOP,
        SMALL_BUTTON_WIDTH,
        BUTTON_HEIGHT,
        TRUE
    );
    x += SMALL_BUTTON_WIDTH + BUTTON_GAP;

    MoveWindow(
        ui_.buttons[ButtonIndex::BTN_KEEP_ALIVE_OFF],
        x,
        BUTTON_TOP,
        SMALL_BUTTON_WIDTH,
        BUTTON_HEIGHT,
        TRUE
    );
    x += SMALL_BUTTON_WIDTH + BUTTON_GAP;

    MoveWindow(
        ui_.buttons[ButtonIndex::BTN_REFRESH],
        x,
        BUTTON_TOP,
        MEDIUM_BUTTON_WIDTH,
        BUTTON_HEIGHT,
        TRUE
    );
    x += MEDIUM_BUTTON_WIDTH + BUTTON_GAP;
    MoveWindow(
        ui_.buttons[ButtonIndex::BTN_FORCE_USB],
        x,
        BUTTON_TOP,
        SMALL_BUTTON_WIDTH,
        BUTTON_HEIGHT,
        TRUE
    );
    x += SMALL_BUTTON_WIDTH + BUTTON_GAP;
    MoveWindow(
        ui_.buttons[ButtonIndex::BTN_SETUP_WIFI],
        x,
        BUTTON_TOP,
        SMALL_BUTTON_WIDTH,
        BUTTON_HEIGHT,
        TRUE
    );

    MoveWindow(
        ui_.listBoxes[ListBoxIndex::LSTB_APP_LIST],
        MARGIN,
        LIST_TOP,
        width - MARGIN * 2,
        height - LIST_TOP - MARGIN,
        TRUE
    );
}

int SapplApp::UIOperations::AutoApplyTheme(HWND hwnd, LPARAM lParam) {
    if (lParam != 0)
    {
        const wchar_t* setting =
            reinterpret_cast<const wchar_t*>(lParam);

        if (lstrcmpiW(setting, L"ImmersiveColorSet") == 0)
        {
            SetWindowTheme(hwnd);
            return 0;
        }
    }

    return 0;
}

void SapplApp::UIOperations::UpdateStatusDisplay(Settings settings, std::wstring currentState) 
{
    std::wstring text;

    text += L"Keep Alive (KA): ";
    settings.keepAlive
        ? text += L"Enabled"
        : text += L"Disabled";
    
    text += L"    ";

    text += L"Force USB (fUSB): ";
    settings.forceUsb
        ? text += L"Enabled"
        : text += L"Disabled";

    text += L"\n" + currentState;

    SetWindowTextW(ui_.status, text.c_str());

    RedrawWindow(
        ui_.status,
        nullptr,
        nullptr,
        RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE
    );


}

void SapplApp::UIOperations::UpdateAppListDisplay(std::vector<DiscoveredApp> apps) {
    SendMessageW(
        ui_.listBoxes[ListBoxIndex::LSTB_APP_LIST],
        LB_RESETCONTENT,
        0,
        0
    );

    SendMessageW(
        ui_.listBoxes[ListBoxIndex::LSTB_APP_LIST],
        LB_SETTABSTOPS,
        static_cast<WPARAM>(std::size(TAB_STOP)),
        reinterpret_cast<LPARAM>(TAB_STOP)
    );

    for (const auto& app : apps)
    {
        std::wstring name = app.name;
        std::wstring type = app.type;
        std::wstring packageName = app.packageName;

        std::wstring text =
            name + L"\t" + type + L"\t" + packageName;
//
        SendMessageW(
            ui_.listBoxes[ListBoxIndex::LSTB_APP_LIST],
            LB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(text.c_str())
        );
    }
}

SapplApp::UIOperations::~UIOperations() {
    if (ui_.titleFont) {
        DeleteObject(ui_.titleFont);
    }
}

void SapplApp::SystemOperations::Debug(const std::wstring& message) {
    MessageBoxW(NULL, message.c_str(), L"Debug", MB_OK | MB_ICONINFORMATION);
}

void SapplApp::SystemOperations::HandleError(DWORD errorCode) {
    TCHAR errorMsg[256];
    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, errorCode,
                  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                  errorMsg, sizeof(errorMsg)/sizeof(TCHAR), NULL);
    
    MessageBoxW(NULL, errorMsg, L"Error", MB_ICONERROR | MB_OK);
}

void SapplApp::SystemOperations::CleanupResources() {
}

BOOL SapplApp::InitInstance(HINSTANCE hInstance, int nCmdShow) {
    WNDCLASSEXW wcex;

    wcex.cbSize         = sizeof(WNDCLASSEXW);
    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProcStatic; 
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = (HICON)LoadImageW(hInstance, MAKEINTRESOURCE(IDI_SAPPL_APP),
                            IMAGE_ICON, 
                            GetSystemMetrics(SM_CXSMICON), 
                            GetSystemMetrics(SM_CYSMICON), 
                            LR_SHARED); 
    wcex.hIconSm        = (HICON)LoadImageW(hInstance, MAKEINTRESOURCE(IDI_SAPPL_APP),
                            IMAGE_ICON, 
                            GetSystemMetrics(SM_CXSMICON), 
                            GetSystemMetrics(SM_CYSMICON), 
                            LR_SHARED); 
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = nullptr;
    wcex.lpszMenuName   = NULL;
    wcex.lpszClassName  = szWindowClass;

    if (!RegisterClassEx(&wcex)) {
        systemOps_.HandleError(GetLastError());
        return FALSE;
    }

    RECT windowRect{
        0,
        0,
        WINDOW_WIDTH,
        WINDOW_HEIGHT
    };

    AdjustWindowRectEx(
        &windowRect,
        WS_OVERLAPPEDWINDOW,
        FALSE,
        0
    );

    hInst = hInstance;

    hWnd_ = CreateWindowExW(
        0,
        szWindowClass,
        szTitle,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr, nullptr,
        hInstance,
        this
    );

    if (!hWnd_) {
        systemOps_.HandleError(GetLastError());
        return FALSE;
    }

    ShowWindow(hWnd_, nCmdShow);
    UpdateWindow(hWnd_);

    // Auto start here. WM_CREATE is too early.
    Refresh(hWnd_);
    processOps_.ResetKeepAlive(hWnd_);
    uiOps_.UpdateStatusDisplay(data_.settings);
    
    return TRUE;
}

// Bridge pointer from static WndProc to non-static WndProc
LRESULT CALLBACK SapplApp::WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    SapplApp* app = reinterpret_cast<SapplApp*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    switch (message) {
        case WM_NCCREATE:
        {
            CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
            app = reinterpret_cast<SapplApp*>(pCreate->lpCreateParams);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            break;
        }
    }   
    return app
        ? app->WndProc(hWnd, message, wParam, lParam)
        : DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT CALLBACK SapplApp::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) 
{  
    switch (message) {
        case WM_NCCREATE:
        {
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        
        case WM_CREATE:
        {
            // This is initialized before the window open, why? 
            // Suspect: static to non-static pointer bridging
            // Alternative: use InitInstance    

            uiOps_.InitializeUI();
            uiOps_.SetWindowTheme(hWnd);
            uiOps_.ConfigureLayout(hWnd);        
             
            return 0;
        }

        case WM_SIZE:
        {
            uiOps_.DynamicResize(hWnd, LOWORD(lParam), HIWORD(lParam));

            return 0;
        }

        case WM_SETTINGCHANGE:
        {
            uiOps_.AutoApplyTheme(hWnd, lParam);
            return 0;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            uiOps_.PaintBackground(hWnd, hdc);
            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_CTLCOLORSTATIC:
        {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            return reinterpret_cast<LRESULT>(uiOps_.SetContainerTheme(hdc));
        }

        case WM_CTLCOLORLISTBOX:
        {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            return reinterpret_cast<LRESULT>(uiOps_.SetAppListTheme(hdc));
        }
        
        case WM_DRAWITEM:
        {
            if (uiOps_.DrawButtons(lParam)) {
                return TRUE;
            }
            break; 
        }

        case WM_COMMAND:
        {
            int lo = LOWORD(wParam);
            switch (lo) {
                case BTN_REFRESH:
                    Refresh(hWnd_);
                    return 0;

                case BTN_KEEP_ALIVE_RESET:
                    uiOps_.DisableButton(uiOps_.BTN_KEEP_ALIVE_RESET);
                    uiOps_.UpdateStatusDisplay(data_.settings, RUNNING_STATE);
                    processOps_.ResetKeepAlive(hWnd_);
                    return 0;

                case BTN_KEEP_ALIVE_OFF:
                    uiOps_.DisableButton(uiOps_.BTN_KEEP_ALIVE_OFF);
                    uiOps_.UpdateStatusDisplay(data_.settings, RUNNING_STATE);
                    processOps_.StopKeepAlive();
                    uiOps_.UpdateStatusDisplay(data_.settings,L"Keep Alive has stopped.");
                    return 0;

                case BTN_FORCE_USB:
                    uiOps_.DisableButton(uiOps_.BTN_FORCE_USB);
                    uiOps_.UpdateStatusDisplay(data_.settings, RUNNING_STATE);
                    data_.settings.forceUsb = !data_.settings.forceUsb;
                    if (data_.settings.keepAlive) {
                        processOps_.ResetKeepAlive(hWnd_);
                    }
                    uiOps_.UpdateStatusDisplay(data_.settings);
                    uiOps_.EnableButton(uiOps_.BTN_FORCE_USB);
                    return 0;

                case BTN_SETUP_WIFI:
                    uiOps_.DisableButton(uiOps_.BTN_SETUP_WIFI);
                    uiOps_.UpdateStatusDisplay(data_.settings, RUNNING_STATE);
                    worker_.emplace_back(std::jthread([this]() {
                        if (!processOps_.SetupWifi(hWnd_)) {
                            PostMessage(hWnd_, SapplApp::WM_SETUP_WIFI, FALSE, 0);
                        } else {
                            PostMessage(hWnd_, SapplApp::WM_SETUP_WIFI, TRUE, 0);
                        }
                    }));
                    return 0;
                case BTN_MIRROR:
                    uiOps_.DisableButton(uiOps_.BTN_MIRROR);
                    uiOps_.UpdateStatusDisplay(data_.settings, RUNNING_STATE);
                    processOps_.Run(processOps_.GetLaunchCommand({}));
                    uiOps_.EnableButton(uiOps_.BTN_MIRROR);
                    return 0;
            }

            int hi = HIWORD(wParam);
            switch (hi) {
                case LBN_DBLCLK:
                    if(reinterpret_cast<HWND>(lParam) == uiOps_.GetAppList())
                    {
                        LRESULT index = SendMessageW(
                            uiOps_.GetAppList(),
                            LB_GETCURSEL,
                            0,
                            0
                        );
                        
                        std::vector<DiscoveredApp> apps = data_.discoveredApps;

                        if (index != LB_ERR &&
                            static_cast<size_t>(index) < apps.size())                        
                        {   
                            
                            uiOps_.UpdateStatusDisplay(data_.settings, L"Launching...");
                            std::wstring result;
                            if (!processOps_.Run(processOps_.GetLaunchCommand(apps[index]))) {
                                result = L"Failed to launch: ";
                            } else {
                                result = L"Launched: ";
                            }
                            result += apps[index].name;;
                            uiOps_.UpdateStatusDisplay(data_.settings, result);
                        }

                        return 0;
                    }
            }
            break;
        }
        case WM_KEEP_ALIVE:
            data_.settings.keepAlive = static_cast<bool>(wParam);
            uiOps_.UpdateStatusDisplay(data_.settings,
                data_.settings.keepAlive ? L"Keep Alive is running." : L"Keep Alive has stopped.");
            data_.settings.keepAlive
                ? uiOps_.EnableButton(uiOps_.BTN_KEEP_ALIVE_OFF)
                : uiOps_.DisableButton(uiOps_.BTN_KEEP_ALIVE_OFF);
            uiOps_.EnableButton(uiOps_.BTN_KEEP_ALIVE_RESET); 
            return 0;

        case WM_FORCE_USB:
            data_.settings.forceUsb = static_cast<bool>(wParam);
            uiOps_.UpdateStatusDisplay(data_.settings);
            return 0;
        case WM_SETUP_WIFI:
        {
            bool result = static_cast<bool>(wParam);
            uiOps_.EnableButton(uiOps_.BTN_SETUP_WIFI);
            if (result)
            {
                uiOps_.UpdateStatusDisplay(
                    data_.settings,
                    L"Setup WiFi success."
                );
            }
            else
            {
                uiOps_.UpdateStatusDisplay(
                    data_.settings,
                    L"Setup WiFi failed."
                );

                MessageBoxW(
                    hWnd_,
                    L"Make sure your phone is connected via USB and try again.",
                    L"Error",
                    MB_OK | MB_ICONERROR
                );
            }
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(
        hWnd,
        message,
        wParam,
        lParam
    );
}

// WinMain function
int WINAPI wWinMain(
   _In_ HINSTANCE hInstance,
   _In_opt_ HINSTANCE hPrevInstance,
   _In_ LPWSTR     lpCmdLine,
   _In_ int       nCmdShow
)
{
    SapplApp app;
    app.Run();
    return 0;
}