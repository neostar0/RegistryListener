#pragma once

#include <functional>
#include <memory>
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
        m_Inited(false), m_Started(false), m_Canceled(false)
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
            return true;
        }

        if (!RegisterNotify())
        {
            return false;
        }

        m_Thread = std::make_unique<std::thread>(std::bind(&RegKeyListenerBase::WaitForNofity, this));

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

        NotifyThreadExit();

        if (m_Thread)
        {
            if (m_Thread->joinable())
            {
                m_Thread->join();
            }
            m_Thread.reset();
        }

        m_Canceled = false;
        m_Started = false;
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

        return true;
    }

    bool RegisterNotify()
    {
        LONG lError = ::RegNotifyChangeKeyValue(m_TargetKey, m_WatchSubtree, m_NotifyFilter, m_RegNotifyEvent, TRUE);

        return ERROR_SUCCESS == lError;
    }

    DWORD WaitForNofity()
    {
        DWORD dwRet = 0;
        while (!m_Canceled)
        {
            DWORD dwWait = ::WaitForSingleObject(m_RegNotifyEvent, INFINITE);
            if (WAIT_OBJECT_0 != dwWait)
            {
                // TODO: Log unexpected error.
            }

            if (m_Canceled)
            {
                break;
            }

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

        return dwRet;
    }

    void NotifyThreadExit()
    {
        m_Canceled = true;
        ::SetEvent(m_RegNotifyEvent);
    }

private:
    const HKEY m_RootKey;
    const wchar_t const* m_TargetKeyPath;
    const BOOL m_WatchSubtree;
    const DWORD m_NotifyFilter;

    std::mutex m_Mutex;
    volatile bool m_Canceled;
    bool m_Inited;
    bool m_Started;

    raii::Handle<HKEY, decltype(&RegCloseKey)> m_TargetKey;
    raii::Handle<HANDLE, decltype(&CloseHandle)> m_RegNotifyEvent;

    std::unique_ptr<std::thread> m_Thread;
};
