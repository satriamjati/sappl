#pragma once

#include <windows.h>
#include <dwmapi.h>
#include <tchar.h>
#include <vector>
#include <string>
#include <thread>
#include <cwctype>

#include "resource.h"

class SapplApp {
public:
    SapplApp();
    ~SapplApp();
    void Run();

private:
    enum class State : int {
        Idle,
        Running,
        Error
    };

    enum class Theme : DWORD {
        DARK = 0,
        LIGHT = 1
    };

    struct Settings {
        bool keepAlive = true;
        bool forceUsb = false;
        bool virtualDisplay = true;
        bool alwaysOnTop = true;
        bool flexResolution = true;
    };

    struct DiscoveredApp {
        std::wstring name;
        std::wstring packageName;
        std::wstring type;
        std::wstring searchableName;
        std::wstring searchablePackageName;
        DWORD processId;
        bool isRunning;
    };

    struct Data {
        std::vector<DiscoveredApp> discoveredApps;
        Settings settings;
        State state = State::Idle;

        
    } data_;
    void Refresh(HWND hwnd_);

    
    class ProcessOperations {
    public:
        explicit ProcessOperations(Data& data)
            : data_(data) {};
        ~ProcessOperations();
        std::wstring GetAppDirectory();        
        bool GetRequiredPath();
        std::wstring GetLaunchCommand(const DiscoveredApp& app ={});
        bool Run(const std::wstring& command);
        bool RunAndRead(
            const std::wstring& command,
            std::wstring& output);
        void StopKeepAlive();
        void ResetKeepAlive(HWND hwnd);
        bool ReloadAppList(HWND hwnd);
        void ParseAppListOutput(const std::wstring& output);
        bool SetupWifi(HWND hwnd);
    private:
        Data& data_;
        std::wstring scrcpyPath_;
        std::wstring adbPath_ ;
        std::jthread keepAliveThread_;

        std::wstring GetPhoneIP();
        std::wstring Utf8ToWide(const char* utf8, int size);

    } processOps_;
    
    class UIOperations {
    public:
    
        enum ButtonIndex {
            BTN_MIRROR,
            BTN_REFRESH,
            BTN_KEEP_ALIVE_RESET,
            BTN_KEEP_ALIVE_OFF,
            BTN_FORCE_USB,
            BTN_SETUP_WIFI,
            BTN_SEARCH,
            BTN_SEARCH_CLEAR,
            COUNT_BTNS
        };
        enum TextBoxIndex {
            TXTB_SEARCH,
            COUNT_TEXTBOXES
        };
        enum ListBoxIndex {
            LSTB_APP_LIST,
            COUNT_LISTBOXES
        };

        Theme GetSystemAppearanceMode();
        void InitializeUI();
        void SetWindowTheme(HWND hWnd);
        void ConfigureLayout(HWND hwnd);
        void PaintBackground(HWND hwnd, HDC hdc);
        HBRUSH SetContainerTheme(HDC hdc);
        HBRUSH SetAppListTheme(HDC hdc);
        bool DrawButtons(LPARAM lParam);
        int AutoApplyTheme(HWND hwnd, LPARAM lParam);
        void DynamicResize(HWND hwnd, int width, int height);
        void UpdateStatusDisplay(const Settings& settings, const std::wstring& currentState = READY_STATE);
        void UpdateAppListDisplay(const std::vector<DiscoveredApp>& apps);
        void UpdateAppListDisplay(const std::vector<DiscoveredApp>& apps, const std::wstring searchTerm);
        void EnableButton(int buttonIndex){
            EnableWindow(ui_.buttons[buttonIndex],true);
        };
        void DisableButton(int buttonIndex){
            EnableWindow(ui_.buttons[buttonIndex],false);
        };
        HWND GetSearchBox(){
            return ui_.textBoxes[TextBoxIndex::TXTB_SEARCH];
        }
        HWND GetAppList(){
            return ui_.listBoxes[ListBoxIndex::LSTB_APP_LIST];
        }
        ~UIOperations();

    private:
        struct Entity {
            HWND title;
            HFONT titleFont;
            HWND status;
            HWND buttons[ButtonIndex::COUNT_BTNS];  
            HWND textBoxes[TextBoxIndex::COUNT_TEXTBOXES];
            HWND listBoxes[ListBoxIndex::COUNT_LISTBOXES];
            HBRUSH darkBrush;
        } ui_;

        void ResetAppListDisplay();

        class BuildEntity {
        private:
            HWND CreateChildWindow(
                PCWSTR className, PCWSTR text, DWORD style,
                int x, int y, int width, int height,
                HWND parent, HMENU id, DWORD exStyle = 0)
            {
                return CreateWindowExW(
                    exStyle, className, text,
                    WS_CHILD | WS_VISIBLE | style,
                    x, y, width, height,
                    parent, id,
                    GetModuleHandleW(nullptr),
                    nullptr
                );
            }

            HWND CreateButton(
                PCWSTR text, int x, int y, int width, int height,
                HWND parent, HMENU id, DWORD style = 0)
            {
                return CreateChildWindow(
                    L"BUTTON", 
                    text, BS_OWNERDRAW | WS_TABSTOP | style,
                    x, y, width, height,
                    parent, id
                );
            }

        public:
            HWND NewWindow(
                PCWSTR className, PCWSTR text, DWORD style,
                int x, int y, int width, int height,
                HWND parent)
            {
                return CreateChildWindow(
                    className, text, style,
                    x, y, width, height,
                    parent, nullptr
                );
            }

            HWND NewButtonSmall(
                PCWSTR text, int x, int y, HWND parent, HMENU id)
            {
                return CreateButton(
                    text, x, y, SMALL_BUTTON_WIDTH, BUTTON_HEIGHT,
                    parent, id
                );
            }

            HWND NewButtonMedium(
                PCWSTR text, int x, int y, HWND parent, HMENU id)
            {
                return CreateButton(
                    text, x, y, MEDIUM_BUTTON_WIDTH, BUTTON_HEIGHT,
                    parent, id
                );
            }

            HWND NewButtonTiny(
                PCWSTR text, int x, int y, HWND parent, HMENU id,
                DWORD style = 0)
            {
                return CreateButton(
                    text, x, y, TINY_BUTTON_WIDTH, SHORT_BUTTON_HEIGHT,
                    parent, id, style
                );
            }

            HWND NewButtonTinyDefault(
                PCWSTR text, int x, int y, HWND parent, HMENU id)
            {
                return NewButtonTiny(
                    text, x, y, parent, id,
                    BS_DEFPUSHBUTTON
                );
            }

            HWND NewButtonMicro(
                PCWSTR text, int x, int y, HWND parent, HMENU id)
            {
                return CreateButton(
                    text, x, y, MICRO_BUTTON_WIDTH, SHORT_BUTTON_HEIGHT,
                    parent, id
                );
            }
        };




    } uiOps_;
    
    class SystemOperations {
    public:
        void Debug(const std::wstring& message);
        void HandleError(DWORD errorCode);
        void CleanupResources();
        
    } systemOps_;
    
    static constexpr TCHAR szWindowClass[] = L"DesktopApp";
    static constexpr TCHAR szTitle[] = L"SAPPL: scrcpy Application Launcher";
    
    static constexpr TCHAR RUNNING_STATE[] = L"Running...";
    static constexpr TCHAR ERROR_STATE[] = L"ERROR";
    static constexpr TCHAR READY_STATE[] = L"Ready";

    // Window dimensions
    static constexpr int WINDOW_WIDTH = 480;
    static constexpr int WINDOW_HEIGHT = 640;
    
    // Element IDs
    static constexpr int BTN_MIRROR = 1000;
    static constexpr int BTN_REFRESH = 1001;
    static constexpr int BTN_KEEP_ALIVE_RESET = 1002;
    static constexpr int BTN_KEEP_ALIVE_OFF = 1003;
    static constexpr int BTN_RESET = 1004;
    static constexpr int BTN_FORCE_USB = 1005;
    static constexpr int BTN_SETUP_WIFI = 1006;
    static constexpr int TXTB_SEARCH = 1101;
    static constexpr int BTN_SEARCH = 1102;
    static constexpr int BTN_SEARCH_CLEAR = 1103;
    static constexpr int LSTB_APP_LIST = 1201;
    
    // Messages
    static constexpr UINT WM_KEEP_ALIVE = WM_APP + 1;
    static constexpr UINT WM_FORCE_USB = WM_APP + 2;
    static constexpr UINT WM_SETUP_WIFI = WM_APP + 3;
    
    // Dimensions
    static constexpr int MARGIN = 20;
    static constexpr int MICRO_BUTTON_WIDTH = 25;
    static constexpr int TINY_BUTTON_WIDTH = 50;
    static constexpr int SMALL_BUTTON_WIDTH = 75;
    static constexpr int MEDIUM_BUTTON_WIDTH = 120;
    static constexpr int SHORT_BUTTON_HEIGHT = 20;
    static constexpr int BUTTON_HEIGHT = 40;
    static constexpr int BUTTON_GAP = 10;
    static constexpr int SEARCH_BOX_HEIGHT = 20;
    static constexpr int APP_LIST_WIDTH = 400;
    static constexpr int TITLE_TOP = 25;
    static constexpr int TITLE_HEIGHT = 50;
    static constexpr int STATUS_TOP = 75;
    static constexpr int STATUS_HEIGHT = 40;
    static constexpr int BUTTON_TOP = 115;
    static constexpr int SEARCH_TOP = 175;
    static constexpr int LIST_TOP = 215;
    static constexpr int TAB_STOP[] = {100, 140, 280};
    
    std::vector<std::jthread> worker_;
    HINSTANCE hInst;
    HWND hWnd_;
    BOOL InitInstance(HINSTANCE hInstance, int nCmdShow);
    static LRESULT CALLBACK WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
};