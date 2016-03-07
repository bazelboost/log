/*
 *             Copyright Andrey Semashev 2016.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   windows/object_name.cpp
 * \author Andrey Semashev
 * \date   06.03.2016
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/doc/libs/release/libs/log/doc/html/index.html.
 */

#include <boost/log/detail/config.hpp>
#include <cstddef>
#include <string>
#include <algorithm>
#include <boost/memory_order.hpp>
#include <boost/atomic/atomic.hpp>
#include <boost/move/utility.hpp>
#include <boost/log/exceptions.hpp>
#include <boost/log/utility/ipc/object_name.hpp>
#include <boost/detail/winapi/get_last_error.hpp>
#include <windows.h>
#include <lmcons.h>
#include "windows/auto_handle.hpp"
#include <boost/log/detail/header.hpp>

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace ipc {

BOOST_LOG_ANONYMOUS_NAMESPACE {

#if BOOST_USE_WINAPI_VERSION >= BOOST_WINAPI_VERSION_VISTA

class auto_boundary_descriptor
{
private:
    HANDLE m_handle;

public:
    explicit auto_boundary_descriptor(HANDLE h = NULL) BOOST_NOEXCEPT : m_handle(h)
    {
    }

    ~auto_boundary_descriptor() BOOST_NOEXCEPT
    {
        if (m_handle)
            DeleteBoundaryDescriptor(m_handle);
    }

    void init(HANDLE h) BOOST_NOEXCEPT
    {
        BOOST_ASSERT(m_handle == NULL);
        m_handle = h;
    }

    HANDLE get() const BOOST_NOEXCEPT { return m_handle; }
    HANDLE* get_ptr() const BOOST_NOEXCEPT { return &m_handle; }

    void swap(auto_boundary_descriptor& that) BOOST_NOEXCEPT
    {
        HANDLE h = m_handle;
        m_handle = that.m_handle;
        that.m_handle = h;
    }

    BOOST_DELETED_FUNCTION(auto_boundary_descriptor(auto_boundary_descriptor const&))
    BOOST_DELETED_FUNCTION(auto_boundary_descriptor& operator=(auto_boundary_descriptor const&))
};

//! Handle for the private namespace for \c user scope
static boost::atomic< HANDLE > g_user_private_namespace;

//! Attempts to create or open the private namespace
bool init_user_namespace()
{
    HANDLE h = g_user_private_namespace.load(boost::memory_order_acquire);
    if (BOOST_UNLIKELY(!h))
    {
        // Obtain the current user SID
        auto_handle h_process_token;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, h_process_token.get_ptr()))
            return false;

        TOKEN_USER token_user = {};
        DWORD token_user_size = 0;
        if (!GetTokenInformation(h_process_token.get(), TokenUser, &token_user, sizeof(token_user), &token_user_size) || !token_user.User.Sid)
            return false;

        // Create a boindary descriptor with the user's SID
        auto_boundary_descriptor h_boundary(CreateBoundaryDescriptorW(L"User", 0));
        if (!h_boundary.get())
            return false;

        if (!AddSIDToBoundaryDescriptor(h_boundary.get_ptr(), token_user.User.Sid))
            return false;

        // Create or open a namespace for kernel objects
        h = CreatePrivateNamespaceW(NULL, h_boundary.get(), L"User");
        if (!h)
            h = OpenPrivateNamespace(h_boundary.get(), L"User");

        if (h)
        {
            HANDLE expected = NULL;
            if (!g_user_private_namespace.compare_exchange_strong(expected, h, boost::memory_order_acq_rel, boost::memory_order_acquire))
            {
                ClosePrivateNamespace(h, 0);
                h = expected;
            }
        }
    }

    return !!h;
}

#endif // BOOST_USE_WINAPI_VERSION >= BOOST_WINAPI_VERSION_VISTA

//! Returns a prefix string for a shared resource according to the scope
std::string get_scope_prefix(object_name::scope ns)
{
    std::string prefix;
    switch (ns)
    {
    case object_name::process_group:
        {
            // For now consider all processes as members of the common process group. It may change if there is found
            // a way to get a process group id (i.e. id of the closest parent process that was created with the CREATE_NEW_PROCESS_GROUP flag).
            prefix = "Local\\boost.log.process_group";
        }
        break;

    case object_name::session:
        {
            prefix = "Local\\boost.log.session";
        }
        break;

    case object_name::user:
        {
#if BOOST_USE_WINAPI_VERSION >= BOOST_WINAPI_VERSION_VISTA
            if (init_user_namespace())
            {
                prefix = "User\\boost.log.user";
            }
            else
#endif // BOOST_USE_WINAPI_VERSION >= BOOST_WINAPI_VERSION_VISTA
            {
                wchar_t buf[UNLEN + 1u];
                ULONG len = sizeof(buf) / sizeof(*buf);
                if (BOOST_UNLIKELY(!GetUserNameEx(NameSamCompatible, buf, &len)))
                {
                    const boost::detail::winapi::DWORD_ err = boost::detail::winapi::GetLastError();
                    BOOST_LOG_THROW_DESCR_PARAMS(boost::log::system_error, "Failed to obtain the current user name", (err));
                }

                std::replace(buf, buf + len, L'\\', L'.');

                prefix = "Local\\boost.log.user." + utf16_to_utf8(buf);
            }
        }
        break;

    default:
        prefix = "Global\\boost.log.global";
        break;
    }

    prefix.push_back('.');

    return boost::move(prefix);
}

} // namespace

//! Constructor from the object name
BOOST_LOG_API object_name::object_name(scope ns, const char* str) :
    m_name(get_scope_prefix(ns) + str)
{
}

//! Constructor from the object name
BOOST_LOG_API object_name::object_name(scope ns, std::string const& str) :
    m_name(get_scope_prefix(ns) + str)
{
}

} // namespace ipc

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#include <boost/log/detail/footer.hpp>