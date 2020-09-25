#include <iostream>
#include <chrono>
#include "RegKeyListenerBase.h"
#include "WindowsReg.h"
using namespace std;

int main()
{
    WindowsReg reg;
    uint32_t v = 0;
    auto status = reg.Open(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft");
    DWORD disp;
    HKEY h;
    // Create or open exist.
    auto ret = RegCreateKeyEx(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft",
        0, // reserved,
        NULL,
        REG_OPTION_NON_VOLATILE, // options
        KEY_ALL_ACCESS,          // samDesired
        NULL,                    // security attrs
        &h,
        &disp);

    if (ret == ERROR_SUCCESS)
    {
        // Try changing the registry value for testing the listener.
        raii::Handle hkey{ h, ::RegCloseKey };

        std::thread([h = std::move(hkey)]() {
            DWORD data = 0;
            for (;;)
            {
                auto ret = RegSetValueEx(
                    h,
                    L"TestDWORD",
                    0, // reserved
                    REG_DWORD,
                    reinterpret_cast<const BYTE*>(&data),
                    sizeof(data));
                using namespace std::chrono_literals;
                std::this_thread::sleep_for(1s);
                data++;
            }

        }).detach();
    }

    class RegKeyListener final : public RegKeyListenerBase
    {
    public:
        // Key should be existing first.
        RegKeyListener() : RegKeyListenerBase(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft", FALSE, REG_NOTIFY_CHANGE_LAST_SET)
        {
            WindowsReg reg;
            uint32_t v = 0;
            auto ok = reg.Open(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft") == ERROR_SUCCESS && reg.ReadIntValue(L"TestDWORD", v);
            m_LastValue = ok ? v : 0;
        }
        void OnKeyChanged() override
        {
            WindowsReg reg;
            uint32_t v = 0;
            auto status = reg.Open(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft") == ERROR_SUCCESS && reg.ReadIntValue(L"TestDWORD", v);
            if (status)
            {
                cout << "m_LastValue change from " << m_LastValue << " to  " << v << endl;
                m_LastValue = v;
            }
            else
            {
                // When the reg is deleted, it will go here since we cannot read it.
                cout << "cannot read the value" << endl;
            }
        }
        uint32_t m_LastValue;
    };

    RegKeyListener listener;
    listener.Start();
    system("PAUSE");
    // getchar will make the event never be signaled by my testing. Why? Use system("PAUSE"); instead.
    //getchar();
    return 0;
}

