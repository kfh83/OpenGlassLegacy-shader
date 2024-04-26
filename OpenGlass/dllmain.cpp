﻿// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"
#include "resource.h"
#include "Utils.hpp"
#include "OpenGlass.hpp"
#include "ServiceApi.hpp"

BOOL APIENTRY DllMain(
	HMODULE hModule,
	DWORD  dwReason,
	LPVOID lpReserved
)
{
	switch (dwReason)
	{
		case DLL_PROCESS_ATTACH:
		{
			DisableThreadLibraryCalls(hModule);
			OpenGlass::Startup();
			break;
		}
		case DLL_PROCESS_DETACH:
		{
			OpenGlass::Shutdown();
			break;
		}
	}
	return TRUE;
}

EXTERN_C __declspec(dllexport) HRESULT StartupService()
{
	if (!OpenGlass::Utils::IsRunAsLocalSystem())
	{
		auto cleanUp{ wil::CoInitializeEx() };
		wil::com_ptr<ITaskService> taskService{ wil::CoCreateInstance<ITaskService>(CLSID_TaskScheduler) };
		RETURN_IF_FAILED(taskService->Connect(_variant_t{}, _variant_t{}, _variant_t{}, _variant_t{}));

		wil::com_ptr<ITaskFolder> rootFolder{ nullptr };
		RETURN_IF_FAILED(taskService->GetFolder(_bstr_t{"\\"}, &rootFolder));

		wil::com_ptr<IRegisteredTask> registeredTask{ nullptr };
		HRESULT hr
		{
			rootFolder->GetTask(
				_bstr_t{L"OpenGlass Host"},
				&registeredTask
			)
		};
		RETURN_IF_FAILED(hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ? HRESULT_FROM_WIN32(ERROR_SERVICE_DOES_NOT_EXIST) : hr);

		VARIANT_BOOL enabled{ FALSE };
		RETURN_IF_FAILED(registeredTask->get_Enabled(&enabled));
		RETURN_HR_IF(SCHED_E_TASK_DISABLED, !enabled);

		[[maybe_unused]] wil::com_ptr<IRunningTask> runningTask{ nullptr };
		RETURN_IF_FAILED(registeredTask->RunEx(_variant_t{}, TASK_RUN_IGNORE_CONSTRAINTS, 0, _bstr_t{}, &runningTask));

		return S_OK;
	}

	return OpenGlass::Server::Run();
}
EXTERN_C __declspec(dllexport) HRESULT ShutdownService()
{
	OpenGlass::PipeContent content{ static_cast<DWORD>(-1), nullptr };
	return OpenGlass::Client::RequestUserRegistryKey(content);
}
EXTERN_C __declspec(dllexport) HRESULT InstallApp() try
{
	auto cleanUp{ wil::CoInitializeEx() };
	wil::com_ptr<ITaskService> taskService{ wil::CoCreateInstance<ITaskService>(CLSID_TaskScheduler) };
	THROW_IF_FAILED(taskService->Connect(_variant_t{}, _variant_t{}, _variant_t{}, _variant_t{}));

	wil::com_ptr<ITaskFolder> rootFolder{ nullptr };
	THROW_IF_FAILED(taskService->GetFolder(_bstr_t{"\\"}, &rootFolder));

	wil::com_ptr<ITaskDefinition> taskDefinition{ nullptr };
	THROW_IF_FAILED(taskService->NewTask(0, &taskDefinition));

	wil::com_ptr<IRegistrationInfo> regInfo{ nullptr };
	THROW_IF_FAILED(taskDefinition->get_RegistrationInfo(&regInfo));
	THROW_IF_FAILED(regInfo->put_Author(_bstr_t{L"ALTaleX"}));
	THROW_IF_FAILED(regInfo->put_Description(_bstr_t(OpenGlass::Utils::GetResWString<IDS_STRING104>().c_str())));

	{
		wil::com_ptr<IPrincipal> principal{ nullptr };
		THROW_IF_FAILED(taskDefinition->get_Principal(&principal));

		THROW_IF_FAILED(principal->put_UserId(_bstr_t{L"SYSTEM"}));
		THROW_IF_FAILED(principal->put_LogonType(TASK_LOGON_SERVICE_ACCOUNT));
		THROW_IF_FAILED(principal->put_DisplayName(_bstr_t{ L"SYSTEM" }));
		THROW_IF_FAILED(principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST));
	}

	{
		wil::com_ptr<ITaskSettings> setting{ nullptr };
		THROW_IF_FAILED(taskDefinition->get_Settings(&setting));

		THROW_IF_FAILED(setting->put_StopIfGoingOnBatteries(VARIANT_FALSE));
		THROW_IF_FAILED(setting->put_DisallowStartIfOnBatteries(VARIANT_FALSE));
		THROW_IF_FAILED(setting->put_AllowDemandStart(VARIANT_TRUE));
		THROW_IF_FAILED(setting->put_AllowHardTerminate(VARIANT_TRUE));
		THROW_IF_FAILED(setting->put_StartWhenAvailable(VARIANT_FALSE));
		THROW_IF_FAILED(setting->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW));
	}

	{
		wil::com_ptr<IExecAction> execAction{ nullptr };
		{
			wil::com_ptr<IAction> action{ nullptr };
			{
				wil::com_ptr<IActionCollection> actionColl{ nullptr };
				THROW_IF_FAILED(taskDefinition->get_Actions(&actionColl));
				THROW_IF_FAILED(actionColl->Create(TASK_ACTION_EXEC, &action));
			}
			action.query_to(&execAction);
		}

		WCHAR modulePath[MAX_PATH + 1]{};
		RETURN_LAST_ERROR_IF(!GetModuleFileName(wil::GetModuleInstanceHandle(), modulePath, MAX_PATH));

		THROW_IF_FAILED(
			execAction->put_Path(
				_bstr_t{L"Rundll32"}
			)
		);

		THROW_IF_FAILED(
			execAction->put_Arguments(
				_bstr_t
				{
					std::format(L"\"{}\",Main /startup", modulePath).c_str()
				}
			)
		);
	}

	wil::com_ptr<ITriggerCollection> triggerColl{ nullptr };
	THROW_IF_FAILED(taskDefinition->get_Triggers(&triggerColl));

	wil::com_ptr<ITrigger> trigger{ nullptr };
	THROW_IF_FAILED(triggerColl->Create(TASK_TRIGGER_BOOT, &trigger));

	wil::com_ptr<IRegisteredTask> registeredTask{ nullptr };
	THROW_IF_FAILED(
		rootFolder->RegisterTaskDefinition(
			_bstr_t{L"OpenGlass Host"},
			taskDefinition.get(),
			TASK_CREATE_OR_UPDATE,
			_variant_t{ L"SYSTEM" },
			_variant_t{},
			TASK_LOGON_SERVICE_ACCOUNT,
			_variant_t{},
			&registeredTask
		)
	);

	return S_OK;
}
CATCH_RETURN()
EXTERN_C __declspec(dllexport) HRESULT UninstallApp() try
{
	auto cleanUp{ wil::CoInitializeEx() };
	wil::com_ptr<ITaskService> taskService{ wil::CoCreateInstance<ITaskService>(CLSID_TaskScheduler) };
	THROW_IF_FAILED(taskService->Connect(_variant_t{}, _variant_t{}, _variant_t{}, _variant_t{}));

	wil::com_ptr<ITaskFolder> rootFolder{ nullptr };
	THROW_IF_FAILED(taskService->GetFolder(_bstr_t{"\\"}, &rootFolder));

	HRESULT hr
	{
		rootFolder->DeleteTask(
			_bstr_t{L"OpenGlass Host"},
			0
		)
	};
	THROW_IF_FAILED(hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ? HRESULT_FROM_WIN32(ERROR_SERVICE_DOES_NOT_EXIST) : hr);

	return S_OK;
}
CATCH_RETURN()

struct ExecutionParameters
{
	enum class CommandType : UCHAR
	{
		Unknown,
		Startup,
		Shutdown,
		Install,
		Uninstall
	} type{ CommandType::Unknown };
	bool reserved{ false };
};
ExecutionParameters AnalyseCommandLine(LPCWSTR lpCmdLine)
{
	int args{ 0 };
	auto argv{ CommandLineToArgvW(lpCmdLine, &args) };
	ExecutionParameters params{};

	for (int i = 0; i < args; i++)
	{
		if (!_wcsicmp(argv[i], L"/startup") || !_wcsicmp(argv[i], L"/startup"))
		{
			params.type = ExecutionParameters::CommandType::Startup;
		}
		if (!_wcsicmp(argv[i], L"/shutdown") || !_wcsicmp(argv[i], L"/shutdown"))
		{
			params.type = ExecutionParameters::CommandType::Shutdown;
		}
		if (!_wcsicmp(argv[i], L"/install") || !_wcsicmp(argv[i], L"/i") || !_wcsicmp(argv[i], L"-install") || !_wcsicmp(argv[i], L"-i"))
		{
			params.type = ExecutionParameters::CommandType::Install;
		}
		if (!_wcsicmp(argv[i], L"/uninstall") || !_wcsicmp(argv[i], L"/u") || !_wcsicmp(argv[i], L"-uninstall") || !_wcsicmp(argv[i], L"-u"))
		{
			params.type = ExecutionParameters::CommandType::Uninstall;
		}
	}

	return params;
}
EXTERN_C __declspec(dllexport) int WINAPI Main(
	HWND hWnd,
	HINSTANCE hInstance,
	LPCSTR    lpCmdLine,
	int       /*nCmdShow*/
)
{
	using namespace OpenGlass;
	// Until now, We only support Chinese and English...
	if (PRIMARYLANGID(GetUserDefaultUILanguage()) != LANG_CHINESE)
	{
		SetThreadUILanguage(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));
	}
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	RETURN_IF_FAILED(SetCurrentProcessExplicitAppUserModelID(L"OpenGlass.Serivce"));

	// Convert the ansi string back to unicode string
	HRESULT hr{ S_OK };
	auto length{ MultiByteToWideChar(CP_ACP, 0, lpCmdLine, -1, nullptr, 0) };
	THROW_LAST_ERROR_IF(length == 0);
	wil::unique_cotaskmem_string convertedCommandLine{ reinterpret_cast<PWSTR>(CoTaskMemAlloc(sizeof(WCHAR) * length)) };
	THROW_LAST_ERROR_IF_NULL(convertedCommandLine);
	THROW_LAST_ERROR_IF(MultiByteToWideChar(CP_ACP, 0, lpCmdLine, -1, convertedCommandLine.get(), length) == 0);

	auto params{ AnalyseCommandLine(convertedCommandLine.get()) };
	if (params.type == ExecutionParameters::CommandType::Startup)
	{
		hr = StartupService();
		if (FAILED(hr))
		{
			ShellMessageBoxW(
				hInstance,
				nullptr,
				Utils::to_error_wstring(hr).c_str(),
				nullptr,
				MB_ICONERROR
			);
		}
	}
	else if (params.type == ExecutionParameters::CommandType::Shutdown)
	{
		hr = ShutdownService();
		ShellMessageBoxW(
			hInstance,
			nullptr,
			Utils::to_error_wstring(hr).c_str(),
			nullptr,
			FAILED(hr) ? MB_ICONERROR : MB_ICONINFORMATION
		);
	}
	else if (params.type == ExecutionParameters::CommandType::Install)
	{
		hr = InstallApp();
		ShellMessageBoxW(
			hInstance,
			nullptr,
			Utils::to_error_wstring(hr).c_str(),
			nullptr,
			FAILED(hr) ? MB_ICONERROR : MB_ICONINFORMATION
		);
	}
	else if (params.type == ExecutionParameters::CommandType::Uninstall)
	{
		hr = UninstallApp();
		ShellMessageBoxW(
			hInstance,
			nullptr,
			Utils::to_error_wstring(hr).c_str(),
			nullptr,
			FAILED(hr) ? MB_ICONERROR : MB_ICONINFORMATION
		);
	}
	else
	{
		hr = HRESULT_FROM_WIN32(ERROR_BAD_DRIVER_LEVEL);
		ShellMessageBoxW(
			hInstance,
			nullptr,
			Utils::to_error_wstring(hr).c_str(),
			nullptr,
			MB_ICONERROR
		);
	}

	return hr;
}