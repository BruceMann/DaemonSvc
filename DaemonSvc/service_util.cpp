#pragma once
#include <Windows.h>
#include <boost/algorithm/string.hpp>
#include <boost/smart_ptr.hpp>
#include "logger.h"
#include "service_util.h"


#pragma comment(lib, "Advapi32.lib")



static const tchar* service_status_str(const DWORD status)
{
    const tchar *p = TSTR("");
    switch (status)
    {
    case SERVICE_STOPPED:
        p = TSTR("SERVICE_STOPPED");
        break;

    case SERVICE_START_PENDING:
        p = TSTR("SERVICE_START_PENDING");
        break;

    case SERVICE_STOP_PENDING:
        p = TSTR("SERVICE_STOP_PENDING");
        break;

    case SERVICE_RUNNING:
        p = TSTR("SERVICE_RUNNING");
        break;

    case SERVICE_CONTINUE_PENDING:
        p = TSTR("SERVICE_CONTINUE_PENDING");
        break;

    case SERVICE_PAUSE_PENDING:
        p = TSTR("SERVICE_PAUSE_PENDING");
        break;

    case SERVICE_PAUSED:
        p = TSTR("SERVICE_PAUSED");
        break;

    default:
        p = TSTR("unknown??");
        break;
    }

    return p;
}


class CScopedSCHandle
{
public:
    CScopedSCHandle(const DWORD access)
        : m_h(NULL)
    {
        m_h = OpenSCManager(NULL, NULL, access);
        if (NULL == m_h)
        {
            ErrorLogLastErr(CLastError(), TSTR("OpenSCManager fail"));
        }
    }

    ~CScopedSCHandle()
    {
        if (m_h)
        {
            CloseServiceHandle(m_h);
            m_h = NULL;
        }
    }

public:
    SC_HANDLE& get()
    {
        return m_h;
    }

private:
    SC_HANDLE m_h;
};


class CScopedSvcHandle
{
public:
    CScopedSvcHandle(const tstring& name, const DWORD sc_access, const DWORD svc_access)
        : m_sc(sc_access)
        , m_h(NULL)
    {
        if (m_sc.get())
        {
            m_h = OpenService(m_sc.get(), name.c_str(), svc_access);
            if (NULL == m_h)
            {
                ErrorLogLastErr(CLastError(), TSTR("OpenService[%s] fail"), name.c_str());
            }
        }
    }

    ~CScopedSvcHandle()
    {
        if (m_h)
        {
            CloseServiceHandle(m_h);
            m_h = NULL;
        }
    }

public:
    SC_HANDLE& get()
    {
        return m_h;
    }

private:
    CScopedSCHandle m_sc;
    SC_HANDLE m_h;
};



bool ServiceUtil::IsServiceExist(const tstring& name)
{
    bool bReturn = false;

    do 
    {
        CScopedSCHandle hSCMgr(GENERIC_READ);
        if (NULL == hSCMgr.get())
        {
            break;
        }

        DWORD dwNeededBytes = 0;
        DWORD dwReturnedSerivice = 0;
        DWORD dwResumeEntryNum = 0;
        if (EnumServicesStatusEx(hSCMgr.get(), SC_ENUM_PROCESS_INFO, SERVICE_DRIVER | SERVICE_WIN32, 
            SERVICE_STATE_ALL, NULL, 0, &dwNeededBytes, &dwReturnedSerivice, &dwResumeEntryNum, NULL))
        {
            ErrorLogA("EnumServicesStatusEx success while query needed bytes");
            break;
        }

        CLastError e;
        if (ERROR_MORE_DATA != e.code())
        {
            ErrorLogLastErr(e, TSTR("EnumServicesStatusEx error code is not ERROR_MORE_DATA while query needed bytes"));
            break;
        }

        boost::scoped_array<BYTE> lpData(new BYTE[dwNeededBytes]);
        memset(lpData.get(), 0, dwNeededBytes);
        if (!EnumServicesStatusEx(hSCMgr.get(), SC_ENUM_PROCESS_INFO, SERVICE_DRIVER | SERVICE_WIN32, 
            SERVICE_STATE_ALL, lpData.get(), dwNeededBytes, &dwNeededBytes, &dwReturnedSerivice, &dwResumeEntryNum, NULL))
        {
            ErrorLogLastErr(CLastError(), TSTR("EnumServicesStatusEx fail"));
            break;
        }

        ENUM_SERVICE_STATUS_PROCESS *lpServiceStatus = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESS *>(lpData.get());
        for (DWORD dwServiceIndex = 0; dwServiceIndex != dwReturnedSerivice; ++dwServiceIndex)
        {
            tstring cur_name = lpServiceStatus[dwServiceIndex].lpServiceName;
            if (boost::algorithm::iequals(cur_name, name))
            {
                bReturn = true;
                break;
            }
        }

    } while (false);

    return bReturn;
}

bool ServiceUtil::IsServiceRunning(const tstring& name)
{
    bool bReturn = false;

    do 
    {
        CScopedSvcHandle hService(name, GENERIC_READ, GENERIC_READ);
        if (NULL == hService.get())
        {
            break;
        }

        SERVICE_STATUS status = {0};
        if (!QueryServiceStatus(hService.get(), &status))
        {
            ErrorLogLastErr(CLastError(), TSTR("QueryServiceStatus[%s] fail"), name.c_str());
            break;
        }

        if (SERVICE_RUNNING != status.dwCurrentState)
        {
            DebugLog(TSTR("service[%s] is not running. status: %d, %s"),
                name.c_str(), status.dwCurrentState, service_status_str(status.dwCurrentState));
            break;
        }

        bReturn = true;

    } while (false);

    return bReturn;
}

bool ServiceUtil::InstallService(const ServiceInfo& info, const tstring& binary_file)
{
    bool bReturn = false;

    do 
    {
        CScopedSCHandle hSCMgr(GENERIC_ALL);
        if (NULL == hSCMgr.get())
        {
            break;
        }

        DWORD tag_id = info.tag_id;
        SC_HANDLE hService = CreateService(hSCMgr.get(),
            info.name.c_str(),
            info.display_name.c_str(),
            SERVICE_ALL_ACCESS,
            info.service_type,
            info.start_type,
            info.error_control,
            binary_file.c_str(),
            info.load_order_group.c_str(),
            info.use_tagid ? &tag_id : NULL,
            info.dependencies.c_str(),
            info.use_startname ? info.start_name.c_str() : NULL,
            info.use_password ? info.password.c_str() : NULL);
        if (NULL == hService)
        {
            ErrorLogLastErr(CLastError(), TSTR("CreateService[%s] fail"), info.name.c_str());
            break;
        }

        CloseServiceHandle(hService);
        hService = NULL;

        bReturn = true;

    } while (false);

    return bReturn;
}

bool ServiceUtil::RemoveService(const tstring& name)
{
    bool bReturn = false;

    do 
    {
        CScopedSvcHandle hService(name, GENERIC_ALL, SERVICE_ALL_ACCESS);
        if (NULL == hService.get())
        {
            break;
        }

        if (!DeleteService(hService.get()))
        {
            ErrorLogLastErr(CLastError(), TSTR("DeleteService[%s] fail"), name.c_str());
            break;
        }

        bReturn = true;

    } while (false);

    return bReturn;
}

bool ServiceUtil::StartupService(const tstring& name, const DWORD timeout_ms)
{
    InfoLog(TSTR("StartupService[%s] begin"), name.c_str());
    bool bReturn = false;

    do 
    {
        CScopedSvcHandle hService(name, GENERIC_ALL, GENERIC_EXECUTE | GENERIC_READ);
        if (NULL == hService.get())
        {
            break;
        }

        SERVICE_STATUS status = {0};
        if (!QueryServiceStatus(hService.get(), &status))
        {
            ErrorLogLastErr(CLastError(), TSTR("QueryServiceStatus[%s] fail"), name.c_str());
            break;
        }

        if (SERVICE_RUNNING == status.dwCurrentState)
        {
            bReturn = true;
            break;
        }

        if (SERVICE_START_PENDING != status.dwCurrentState)
        {
            if (!StartService(hService.get(), 0, NULL))
            {
                const CLastError e;
                if (ERROR_SERVICE_ALREADY_RUNNING == e.code())
                {
                    DebugLogA("service is already running");
                    bReturn = true;
                }
                else
                {
                    ErrorLogLastErr(e, TSTR("StartService[%s] fail"), name.c_str());
                }
                break;
            }
        }

        const DWORD interval_ms = 500;//ÿ500������һ��
        DWORD total_ms = timeout_ms > interval_ms ? timeout_ms : interval_ms;
        DWORD total_count = total_ms / interval_ms;
        for (DWORD index = 0; index != total_count; ++index)
        {
            if (!QueryServiceStatus(hService.get(), &status))
            {
                ErrorLogLastErr(CLastError(), TSTR("QueryServiceStatus[%s] fail"), name.c_str());
                break;
            }

            if (SERVICE_START_PENDING == status.dwCurrentState)
            {
                DebugLogA("SERVICE_START_PENDING");
                Sleep(interval_ms);
            }
            else
            {
                break;
            }
        }

        if (SERVICE_RUNNING != status.dwCurrentState)
        {
            ErrorLog(TSTR("start service[%s] fail, current status: %d, %s"),
                name.c_str(), status.dwCurrentState, service_status_str(status.dwCurrentState));
            break;
        }

        bReturn = true;

    } while (false);

    InfoLog(TSTR("StartupService[%s] end"), name.c_str());
    return bReturn;
}

bool ServiceUtil::StopService(const tstring& name, const DWORD timeout_ms)
{
    InfoLog(TSTR("StopService[%s] begin"), name.c_str());
    bool bReturn = false;

    do 
    {
        CScopedSvcHandle hService(name, GENERIC_ALL, GENERIC_EXECUTE | GENERIC_READ);
        if (NULL == hService.get())
        {
            break;
        }

        SERVICE_STATUS status = {0};
        if (!QueryServiceStatus(hService.get(), &status))
        {
            ErrorLogLastErr(CLastError(), TSTR("QueryServiceStatus[%s] fail"), name.c_str());
            break;
        }

        if (SERVICE_STOPPED == status.dwCurrentState)
        {
            bReturn = true;
            break;
        }

        if (SERVICE_STOP_PENDING != status.dwCurrentState)
        {
            if (!ControlService(hService.get(), SERVICE_CONTROL_STOP, &status))
            {
                ErrorLogLastErr(CLastError(), TSTR("ControlService[%s] for stopping fail"), name.c_str());
                break;
            }
        }

        const DWORD interval_ms = 500;//ÿ500������һ��
        DWORD total_ms = timeout_ms > interval_ms ? timeout_ms : interval_ms;
        DWORD total_count = total_ms / interval_ms;
        for (DWORD index = 0; index != total_count; ++index)
        {
            if (!QueryServiceStatus(hService.get(), &status))
            {
                ErrorLogLastErr(CLastError(), TSTR("QueryServiceStatus[%s] fail"), name.c_str());
                break;
            }

            if (SERVICE_RUNNING == status.dwCurrentState)
            {
                DebugLogA("SERVICE_RUNNING");
                Sleep(interval_ms);
            }
            else if (SERVICE_STOP_PENDING == status.dwCurrentState)
            {
                DebugLogA("SERVICE_STOP_PENDING");
                Sleep(interval_ms);
            }
            else
            {
                break;
            }
        }

        if (SERVICE_STOPPED != status.dwCurrentState)
        {
            ErrorLog(TSTR("stop service[%s] fail, current status: %d, %s"),
                name.c_str(), status.dwCurrentState, service_status_str(status.dwCurrentState));
            break;
        }

        bReturn = true;

    } while (false);

    InfoLog(TSTR("StopService[%s] end"), name.c_str());
    return bReturn;
}

bool ServiceUtil::SendControlCode2Service(const tstring& name, const DWORD code)
{
    bool bReturn = false;

    do 
    {
        CScopedSvcHandle hService(name, GENERIC_WRITE | GENERIC_EXECUTE, GENERIC_WRITE | GENERIC_EXECUTE);
        if (NULL == hService.get())
        {
            break;
        }

        SERVICE_STATUS status = {0};
        if (!ControlService(hService.get(), code, &status))
        {
            ErrorLogLastErr(CLastError(), TSTR("ControlService[%s:%d] fail"), name.c_str(), code);
            break;
        }

        bReturn = true;

    } while (false);

    return bReturn;
}

