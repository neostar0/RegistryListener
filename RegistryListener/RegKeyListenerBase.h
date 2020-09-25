#pragma once

#include <mutex>
#include <utility>

#include <windows.h>

namespace raii
{
    template <class THandle, class TCloser> class Handle;
}
template <class THandle, class TCloser>
void swap(raii::Handle<THandle, TCloser>& lhs, raii::Handle<THandle, TCloser>& rhs) noexcept;

namespace raii
{
    template <class THandle, class TCloser> class Handle
    {
    public:
        friend void ::swap(Handle& lhs, Handle& rhs) noexcept;

        Handle() = default;
        Handle(const THandle& handle, TCloser closer);

        template <class TOpener, class... TArgs> Handle(TOpener opener, TCloser closer, TArgs&&... args);

        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;

        Handle(Handle&& other) noexcept;
        Handle& operator=(Handle&& other) noexcept;

        ~Handle();

        operator THandle() const;

        bool operator==(const THandle& other) const;
        bool operator!=(const THandle& other) const;
        operator bool() const;

    private:
        THandle m_Handle{};
        TCloser m_Closer{};
    }; // class Handle

    template <class TOpener, class TCloser, class... TArgs>
    Handle(TOpener opener, TCloser closer, TArgs&&... args)
        ->Handle<decltype(opener(std::forward<TArgs>(args)...)), TCloser>;

    template <class THandle, class TCloser>
    Handle<THandle, TCloser>::Handle(const THandle& handle, TCloser closer) : m_Handle{ handle }, m_Closer{ closer }
    {
    }

    template <class THandle, class TCloser>
    template <class TOpener, class... TArgs>
    Handle<THandle, TCloser>::Handle(TOpener opener, TCloser closer, TArgs&&... args)
        : m_Handle{ opener(std::forward<TArgs>(args)...) }, m_Closer{ closer }
    {
    }

    template <class THandle, class TCloser> Handle<THandle, TCloser>::Handle(Handle&& other) noexcept
    {
        ::swap(*this, other);
    }

    template <class THandle, class TCloser> auto Handle<THandle, TCloser>::operator=(Handle&& other) noexcept -> Handle&
    {
        ::swap(*this, other);
        return *this;
    }

    template <class THandle, class TCloser> Handle<THandle, TCloser>::~Handle()
    {
        if (m_Handle)
        {
            m_Closer(m_Handle);
        }
    }

    template <class THandle, class TCloser> Handle<THandle, TCloser>::operator THandle() const
    {
        return m_Handle;
    }

    template <class THandle, class TCloser> bool Handle<THandle, TCloser>::operator==(const THandle& other) const
    {
        return m_Handle == other;
    }

    template <class THandle, class TCloser> bool Handle<THandle, TCloser>::operator!=(const THandle& other) const
    {
        return m_Handle != other;
    }

    template <class THandle, class TCloser> Handle<THandle, TCloser>::operator bool() const
    {
        return m_Handle != nullptr;
    }
} // namespace raii

template <class THandle, class TCloser>
void swap(raii::Handle<THandle, TCloser>& lhs, raii::Handle<THandle, TCloser>& rhs) noexcept
{
    std::swap(lhs.m_Handle, rhs.m_Handle);
    std::swap(lhs.m_Closer, rhs.m_Closer);
}

class RegKeyListenerBase
{
public:
    RegKeyListenerBase(HKEY rootKey, const wchar_t* targetKeyPath, BOOL watchSubtree, DWORD notifyFilter)
        : m_RootKey(rootKey), m_TargetKeyPath(targetKeyPath), m_WatchSubtree(watchSubtree), m_NotifyFilter(notifyFilter),
        m_Inited(false), m_Started(false), m_Continue(true)
    {
        m_Inited = Init();
    }

    virtual ~RegKeyListenerBase()
    {
        Stop();
    }

    RegKeyListenerBase(const RegKeyListenerBase&) = delete;
    RegKeyListenerBase(RegKeyListenerBase&&) = delete;
    RegKeyListenerBase& operator=(const RegKeyListenerBase&) = delete;
    RegKeyListenerBase& operator=(RegKeyListenerBase&&) = delete;

public:
    bool Start()
    {
        std::scoped_lock locker{ m_Mutex };
        if (!m_Inited)
        {
            return false;
        }

        if (m_Started)
        {
            return false;
        }
        if (!RegisterNotify())
        {
            return false;
        }

        raii::Handle thread{ ::CreateThread(NULL, 0, &RegKeyListenerBase::WaitForNofityThreadProc, this, 0, nullptr),
                            CloseHandle };
        if (!thread)
        {
            return false;
        }

        m_Started = true;
        return true;
    }
    void Stop()
    {
        std::scoped_lock locker{ m_Mutex };

        if (!m_Started)
        {
            return;
        }

        m_Continue = false;
        ::SetEvent(m_RegNotifyEvent); // trigger event manually to stop wait thread
        constexpr DWORD TimeOutOneSec = 1000;
        ::WaitForSingleObject(m_WaitThreadCompleteEvent, TimeOutOneSec); // wait for complete of wait thread
    }

protected:
    // override to handle key changed
    virtual void OnKeyChanged() = 0;

private:
    bool Init()
    {
        HKEY targetKey = NULL;
        if (ERROR_SUCCESS != ::RegOpenKeyExW(m_RootKey, m_TargetKeyPath, 0, KEY_READ, &targetKey))
        {
            return false;
        }
        m_TargetKey = raii::Handle{ targetKey, RegCloseKey };

        raii::Handle regNotify{ ::CreateEventW(NULL, FALSE, FALSE, NULL), CloseHandle };
        if (!regNotify)
        {
            return false;
        }
        m_RegNotifyEvent = std::move(regNotify);

        raii::Handle waitThread{ ::CreateEventW(NULL, FALSE, FALSE, NULL), CloseHandle };
        if (!waitThread)
        {
            return false;
        }
        m_WaitThreadCompleteEvent = std::move(waitThread);

        return true;
    }
    bool RegisterNotify()
    {
        LONG lError = ::RegNotifyChangeKeyValue(m_TargetKey, m_WatchSubtree, m_NotifyFilter, m_RegNotifyEvent, TRUE);
        if (ERROR_SUCCESS != lError)
        {
            return false;
        }
        return true;
    }

    static DWORD WINAPI WaitForNofityThreadProc(LPVOID threadParam)
    {
        RegKeyListenerBase* pThis = reinterpret_cast<RegKeyListenerBase*>(threadParam);
        if (nullptr == pThis)
        {
            return 1;
        }

        return pThis->WaitForNofity();
    }
    DWORD WaitForNofity()
    {
        DWORD dwRet = 0;
        while (m_Continue)
        {
            DWORD dwWait = ::WaitForSingleObject(m_RegNotifyEvent, INFINITE);
            if (WAIT_OBJECT_0 != dwWait)
            {
                // TODO: Log unexpected error.
            }

            if (m_Continue)
            {
                // notify key changed
                {
                    std::scoped_lock locker{ m_Mutex };
                    OnKeyChanged();
                }

                // re-register
                if (!RegisterNotify())
                {
                    dwRet = 2;
                    break;
                }
            }
            else
                break;
        }
        ::SetEvent(m_WaitThreadCompleteEvent);

        return dwRet;
    }

private:
    const HKEY m_RootKey;
    const wchar_t const* m_TargetKeyPath;
    const BOOL m_WatchSubtree;
    const DWORD m_NotifyFilter;

    bool m_Inited;
    bool m_Started;
    volatile bool m_Continue;
    std::mutex m_Mutex;

    raii::Handle<HKEY, decltype(&RegCloseKey)> m_TargetKey;
    raii::Handle<HANDLE, decltype(&CloseHandle)> m_RegNotifyEvent;
    raii::Handle<HANDLE, decltype(&CloseHandle)> m_WaitThreadCompleteEvent;
};