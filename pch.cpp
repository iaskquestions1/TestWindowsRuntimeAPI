#include "pch.h"

SafeString::SafeString()
    : m_hstring(nullptr)
{
}

SafeString::~SafeString()
{
    if (nullptr != m_hstring)
    {
        WindowsDeleteString(m_hstring);
    }
}

SafeString::operator const HSTRING& () const
{
    return m_hstring;
}

HSTRING* SafeString::GetAddressOf()
{
    return &m_hstring;
}

const wchar_t* SafeString::c_str() const
{
    return WindowsGetStringRawBuffer(m_hstring, nullptr);
}
