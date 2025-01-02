#include <Windows.h>
#include <iostream>
#include <string>

#pragma comment(lib, "advapi32.lib")

LSTATUS RestartService()
{
    SERVICE_STATUS serviceStatus = {};
    LSTATUS result = ERROR_SUCCESS;

    SC_HANDLE scmHandle = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scmHandle)
    {
        result = GetLastError();
        std::cerr << "error: " << result << std::endl;
        return result;
    }

    SC_HANDLE serviceHandle = OpenServiceW(scmHandle, L"SysMain", SERVICE_ALL_ACCESS);
    if (!serviceHandle)
    {
        result = GetLastError();
        std::cerr << "error: " << result << std::endl;
        CloseServiceHandle(scmHandle);
        return result;
    }

    if (ControlService(serviceHandle, SERVICE_CONTROL_STOP, &serviceStatus))
    {
        std::cout << "stopping service..." << std::endl;
        Sleep(2000);
    }
    else
    {
        DWORD error = GetLastError();
        if (error != ERROR_SERVICE_NOT_ACTIVE)
        {
            std::cerr << "error: " << error << std::endl;
            result = error;
            goto Cleanup;
        }
    }

    std::cout << "starting service..." << std::endl;
    if (StartServiceW(serviceHandle, 0, nullptr))
    {
        std::cout << "restarted" << std::endl;
    }
    else
    {
        result = GetLastError();
        std::cerr << "error: " << result << std::endl;
    }

Cleanup:
    if (serviceHandle)
        CloseServiceHandle(serviceHandle);
    if (scmHandle)
        CloseServiceHandle(scmHandle);

    return result;
}

LSTATUS GetSuperfetchRegistryKey(HKEY* registryKey)
{
    HKEY mainKey = nullptr;
    HKEY staticConfigKey = nullptr;
    HKEY serviceKey = nullptr;
    WCHAR subKeyPath[264] = { 0 };

    *registryKey = nullptr;

    LSTATUS result = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Superfetch",
        0,
        nullptr,
        0,
        KEY_READ | KEY_WRITE,
        nullptr,
        &mainKey,
        nullptr);

    if (result != ERROR_SUCCESS)
        return result;

    result = RegCreateKeyExW(
        mainKey,
        L"StaticConfig",
        0,
        nullptr,
        0,
        KEY_READ | KEY_WRITE,
        nullptr,
        &staticConfigKey,
        nullptr);

    if (result != ERROR_SUCCESS)
    {
        RegCloseKey(mainKey);
        return result;
    }

    DWORD valueType = REG_SZ;
    DWORD valueSize = sizeof(subKeyPath);
    result = RegQueryValueExW(
        staticConfigKey,
        L"ServiceKeyPath",
        nullptr,
        &valueType,
        reinterpret_cast<BYTE*>(subKeyPath),
        &valueSize);

    if (result == ERROR_FILE_NOT_FOUND)
    {
        *registryKey = mainKey;
        RegCloseKey(staticConfigKey);
        return ERROR_SUCCESS;
    }

    if (result != ERROR_SUCCESS)
    {
        RegCloseKey(mainKey);
        RegCloseKey(staticConfigKey);
        return result;
    }

    result = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE,
        subKeyPath,
        0,
        nullptr,
        0,
        KEY_READ | KEY_WRITE,
        nullptr,
        &serviceKey,
        nullptr);

    if (result == ERROR_SUCCESS)
        *registryKey = serviceKey;

    RegCloseKey(mainKey);
    RegCloseKey(staticConfigKey);
    return result;
}

LSTATUS ConfigureMemoryCompression(bool enable)
{
    HKEY registryKey = nullptr;

    LSTATUS result = GetSuperfetchRegistryKey(&registryKey);
    if (result != ERROR_SUCCESS)
    {
        std::cerr << "error: " << result << std::endl;
        return result;
    }

    DWORD adminEnableValue = 0;
    DWORD adminDisableValue = 0;

    DWORD valueSize = sizeof(DWORD);
    result = RegQueryValueExW(registryKey, L"AdminEnable", nullptr, nullptr, reinterpret_cast<BYTE*>(&adminEnableValue), &valueSize);
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND)
    {
        std::cerr << "error: " << result << std::endl;
        RegCloseKey(registryKey);
        return result;
    }

    valueSize = sizeof(DWORD);
    result = RegQueryValueExW(registryKey, L"AdminDisable", nullptr, nullptr, reinterpret_cast<BYTE*>(&adminDisableValue), &valueSize);
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND)
    {
        std::cerr << "error: " << result << std::endl;
        RegCloseKey(registryKey);
        return result;
    }

    if (enable)
    {
        adminEnableValue |= 0x200;
        adminDisableValue &= ~0x200;
    }
    else
    {
        adminEnableValue &= ~0x200;
        adminDisableValue |= 0x200;
    }

    result = RegSetValueExW(
        registryKey,
        L"AdminEnable",
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(&adminEnableValue),
        sizeof(adminEnableValue));

    if (result != ERROR_SUCCESS)
    {
        std::cerr << "error: " << result << std::endl;
        RegCloseKey(registryKey);
        return result;
    }

    result = RegSetValueExW(
        registryKey,
        L"AdminDisable",
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(&adminDisableValue),
        sizeof(adminDisableValue));

    result = RegFlushKey(registryKey);

    if (result != ERROR_SUCCESS)
        std::cerr << "error: " << result << std::endl;
    else
        std::cout << (enable ? "enabled" : "disabled") << std::endl;

    RegCloseKey(registryKey);
    return result;
}

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "usage: disable-memory-compression.exe <enable|disable>" << std::endl;
        return 1;
    }

    const std::string command = argv[1];
    bool enableMemoryCompression;

    if (command == "enable")
        enableMemoryCompression = true;
    else if (command == "disable")
        enableMemoryCompression = false;
    else
    {
        std::cerr << "invalid argument" << std::endl;
        return 1;
    }

    LSTATUS status = ConfigureMemoryCompression(enableMemoryCompression);
    if (status != ERROR_SUCCESS)
        return 1;

    status = RestartService();
    if (status != ERROR_SUCCESS)
        return 1;

    return 0;
}