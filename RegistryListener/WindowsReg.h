#pragma once

#include <windows.h>
#include <string>
#include <stdint.h>
#include <vector>

class WindowsReg final
{
public:
    WindowsReg() : m_Key(nullptr)
    {
    }
    ~WindowsReg()
    {
        Close();
    }

    WindowsReg(const WindowsReg&) = delete;
    WindowsReg(WindowsReg&&) = delete;
    WindowsReg& operator = (const WindowsReg&) = delete;
    WindowsReg& operator = (WindowsReg&&) = delete;

    LSTATUS Open(HKEY rootKey, std::wstring path, REGSAM samDesired = KEY_READ);
    void Close();

    bool ReadStringValue(const wchar_t* name, std::wstring& result) const;
    bool ReadStringValue(const char* name, std::string& result) const;

    bool ReadIntValue(const wchar_t* name, uint32_t& result) const;

    bool ReadInt64Value(const wchar_t* name, uint64_t& result) const;

    bool ReadBinaryValue(const wchar_t* name, std::vector<std::byte>& result) const;

//private:
    HKEY m_Key;
};