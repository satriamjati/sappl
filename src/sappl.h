#pragma once

#include <windows.h>
#include <dwmapi.h>
#include <tchar.h>
#include <vector>
#include <string>
#include <thread>

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
        std::wstring GetLaunchCommand(const DiscoveredApp& app);
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
        std::jthread keepAliveThread_;
        
        std::jthread wifiSetupThread_;
        std::wstring GetPhoneIP();
        std::wstring Utf8ToWide(const char* utf8, int size);

    } processOps_;
    
    class UIOperations {
    public:
    
        enum ButtonIndex {
            BTN_REFRESH,
            BTN_KEEP_ALIVE_RESET,
            BTN_KEEP_ALIVE_OFF,
            BTN_FORCE_USB,
            BTN_SETUP_WIFI,
            BTN_MIRROR,
            COUNT
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
        void UpdateStatusDisplay(Settings settings, std::wstring currentState = READY_STATE);
        void UpdateAppListDisplay(std::vector<DiscoveredApp> apps);
        void EnableButton(int buttonIndex){
            EnableWindow(ui_.buttons[buttonIndex],true);
        };
        void DisableButton(int buttonIndex){
            EnableWindow(ui_.buttons[buttonIndex],false);
        };
        HWND GetAppList(){
            return ui_.appList;
        }
        ~UIOperations();

    private:
        struct Entity {
            HWND title;
            HFONT titleFont;
            HWND status;
            HWND buttons[ButtonIndex::COUNT];
            HWND appList;
            HBRUSH darkBrush;
        } ui_;

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
                    parent, id, GetModuleHandleW(nullptr), nullptr
                );
            }
        public:

            HWND NewWindow(PCWSTR className, PCWSTR text, DWORD style, int x, int y, int w, int h, HWND parent) {
                return CreateChildWindow(className, text, style, x, y, w, h, parent, nullptr);
            }
            
            HWND NewButtonSmall(PCWSTR text, int x, int y, HWND parent, HMENU id) {
                return CreateChildWindow(L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP,
                                        x, y, SMALL_BUTTON_WIDTH, BUTTON_HEIGHT, parent, id);
            }
            HWND NewButtonMedium(PCWSTR text, int x, int y, HWND parent, HMENU id) {
                return CreateChildWindow(L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP,
                                        x, y, MEDIUM_BUTTON_WIDTH, BUTTON_HEIGHT, parent, id);
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
    
    // Button IDs
    static constexpr int BTN_REFRESH = 1001;
    static constexpr int BTN_KEEP_ALIVE_RESET = 1002;
    static constexpr int BTN_KEEP_ALIVE_OFF = 1003;
    static constexpr int BTN_RESET = 1004;
    static constexpr int BTN_FORCE_USB = 1005;
    static constexpr int BTN_SETUP_WIFI = 1006;
    static constexpr int BTN_MIRROR = 1007;
    
    // Messages
    static constexpr UINT WM_KEEP_ALIVE = WM_APP + 1;
    static constexpr UINT WM_FORCE_USB = WM_APP + 2;
    static constexpr UINT WM_SETUP_WIFI = WM_APP + 3;
    
    // Dimensions
    static constexpr int SMALL_BUTTON_WIDTH = 75;
    static constexpr int MEDIUM_BUTTON_WIDTH = 120;
    static constexpr int BUTTON_HEIGHT = 40;
    static constexpr int BUTTON_GAP = 10;
    static constexpr int MARGIN = 20;
    static constexpr int TITLE_TOP = 25;
    static constexpr int TITLE_HEIGHT = 50;
    static constexpr int STATUS_TOP = 75;
    static constexpr int STATUS_HEIGHT = 40;
    static constexpr int BUTTON_TOP = 115;
    static constexpr int LIST_TOP = 175;
    static constexpr int TAB_STOP = 120;
    
    std::vector<std::jthread> worker_;
    HINSTANCE hInst;
    HWND hWnd_;
    BOOL InitInstance(HINSTANCE hInstance, int nCmdShow);
    static LRESULT CALLBACK WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
};