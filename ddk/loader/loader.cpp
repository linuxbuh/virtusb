#pragma unmanaged
#include "stdafx.h"

#pragma unmanaged
int install(TCHAR* inf);
int update(TCHAR* inf);
int updateNI(TCHAR* inf);
int uninstall();

#pragma managed
int cleanreg();
#pragma unmanaged

typedef BOOL (WINAPI *UpdateDriverForPlugAndPlayDevicesProto)(HWND hwndParent,
                                                              LPCTSTR HardwareId,
                                                              LPCTSTR FullInfPath,
                                                              DWORD InstallFlags,
                                                              PBOOL bRebootRequired OPTIONAL
                                                              );
#ifdef _UNICODE
#define UPDATEDRIVERFORPLUGANDPLAYDEVICES "UpdateDriverForPlugAndPlayDevicesW"
#else
#define UPDATEDRIVERFORPLUGANDPLAYDEVICES "UpdateDriverForPlugAndPlayDevicesA"
#endif

typedef BOOL (WINAPI *SetupSetNonInteractiveModeProto)(IN BOOL NonInteractiveFlag);
#define SETUPSETNONINTERACTIVEMODE "SetupSetNonInteractiveMode"

int _tmain(int argc, TCHAR* argv[])
{
	TCHAR inf[MAX_PATH] = __T("");
	DWORD maxDirSize, len, res, res2;

	maxDirSize = MAX_PATH
	             - 1 // null terminator
	             - (DWORD)_tcslen(__T("\\virtusb.inf"));

	if(argc < 2)
	{
		len = GetCurrentDirectory(maxDirSize + 1, inf);
		if(len == 0)
		{
			_tprintf(__T("failed to get current directory\n"));
			return 1;
		}
		else if(len - 1 > maxDirSize)
		{
			_tprintf(__T("current directory too long\n"));
			return 1;
		}
	}
	else if(argc == 2)
	{
		if(!_tcscmp(argv[1], __T("/unload")))
		{
			return uninstall();
		}
		else if(!_tcscmp(argv[1], __T("/clean")))
		{
			return cleanreg();
		}

		if(_tcslen(argv[1]) > maxDirSize)
		{
			_tprintf(__T("directory too long\n"));
			return 1;
		}
		_tcscat_s(inf, MAX_PATH, argv[1]);
	}
	else
	{
		_tprintf(__T("usage: loader.exe [<location_of_virtusb.inf>]\n"));
		_tprintf(__T("       loader.exe /unload\n"));
		_tprintf(__T("       loader.exe /clean\n"));
		return 1;
	}

	_tcscat_s(inf, MAX_PATH, __T("\\virtusb.inf"));

	_tprintf(__T("Inf Path: %s\n"), inf);
	fflush(stdout);
	fflush(stderr);

	res2 = install(inf);
	if(res2) goto err;
	res2 = update(inf);
	if(res2) goto err;

	_tprintf(__T("- Press Enter to close service -\n"));
	fflush(stdout);
	fflush(stderr);
	(void)getchar();

err:
	res = uninstall();
	return res2 ? res2 : res;
}

// List of hardware ID's must be double zero-terminated
TCHAR hwIdList[LINE_LEN + 4] = __T("ROOT\\VIRTUSB\0");

int install(TCHAR* inf)
{
	HDEVINFO DeviceInfoSet = INVALID_HANDLE_VALUE;
	SP_DEVINFO_DATA DeviceInfoData;
	GUID ClassGUID;
	TCHAR ClassName[MAX_CLASS_NAME_LEN];
	TCHAR InfPath[MAX_PATH] = __T("");
	int failcode = 1;
	DWORD err;

	// Inf must be a full pathname
	if(GetFullPathName(inf, MAX_PATH, InfPath, NULL) >= MAX_PATH)
	{
		_tprintf(__T("inf path to long\n"));
		return 1;
	}

	_tprintf(__T("Full Inf Path: %s\n"), InfPath);

	// Use the INF File to extract the Class GUID.
	if(!SetupDiGetINFClass(InfPath, &ClassGUID, ClassName, sizeof(ClassName) / sizeof(ClassName[0]), 0))
	{
		err = GetLastError();
		_tprintf(__T("extracting ClassGUID from INF failed\n"));
		_tprintf(__T("error code: 0x%08x\n"), err);
		switch(err)
		{
		case ERROR_FILE_NOT_FOUND:
			_tprintf(__T("The path that was specified for InfPath does not exist.\n"));
			break;
		}
		goto final;
	}

	// Create the container for the to-be-created Device Information Element.
	DeviceInfoSet = SetupDiCreateDeviceInfoList(&ClassGUID, 0);
	if(DeviceInfoSet == INVALID_HANDLE_VALUE)
	{
		_tprintf(__T("SetupDiCreateDeviceInfoList failed\n"));
		goto final;
	}

	// Now create the element.
	// Use the Class GUID and Name from the INF file.
	DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
	if(!SetupDiCreateDeviceInfo(DeviceInfoSet,
	                            ClassName,
	                            &ClassGUID,
	                            NULL,
	                            0,
	                            DICD_GENERATE_ID,
	                            &DeviceInfoData))
	{
		err = GetLastError();
		_tprintf(__T("SetupDiCreateDeviceInfo failed\n"));
		_tprintf(__T("error code: 0x%08x\n"), err);
		goto final;
	}

	// Add the HardwareID to the Device's HardwareID property.
	if(!SetupDiSetDeviceRegistryProperty(DeviceInfoSet,
	                                     &DeviceInfoData,
	                                     SPDRP_HARDWAREID,
	                                     (LPBYTE)hwIdList,
	                                     (lstrlen(hwIdList) + 2) * sizeof(TCHAR)))
	{
		_tprintf(__T("SetupDiSetDeviceRegistryProperty failed\n"));
		goto final;
	}

	// Transform the registry element into an actual devnode
	// in the PnP HW tree.
	if(!SetupDiCallClassInstaller(DIF_REGISTERDEVICE,
	                              DeviceInfoSet,
	                              &DeviceInfoData))
	{
		_tprintf(__T("SetupDiCallClassInstaller failed\n"));
		goto final;
	}

	failcode = 0;

final:
	if(DeviceInfoSet != INVALID_HANDLE_VALUE)
	{
		SetupDiDestroyDeviceInfoList(DeviceInfoSet);
	}

	return failcode;
}

int update(TCHAR* inf)
{
	HMODULE newdevMod = NULL;
	UpdateDriverForPlugAndPlayDevicesProto UpdateFn;
	BOOL reboot = FALSE;
	DWORD flags = 0;
	DWORD res, err;
	TCHAR InfPath[MAX_PATH];
	int failcode = 1;

	// Inf must be a full pathname
	res = GetFullPathName(inf, MAX_PATH, InfPath, NULL);
	if((res >= MAX_PATH) || (res == 0))
	{
		// inf pathname too long
		return 1;
	}
	if(GetFileAttributes(InfPath) == (DWORD)(-1))
	{
		// inf doesn't exist
		return 1;
	}
	inf = InfPath;
	flags |= INSTALLFLAG_FORCE;

	// make use of UpdateDriverForPlugAndPlayDevices
	newdevMod = LoadLibrary(TEXT("newdev.dll"));
	if(!newdevMod)
	{
		_tprintf(__T("failed to load newdev.dll\n"));
		goto final;
	}
	UpdateFn = (UpdateDriverForPlugAndPlayDevicesProto)GetProcAddress(newdevMod,
		UPDATEDRIVERFORPLUGANDPLAYDEVICES);
	if(!UpdateFn)
	{
		_tprintf(__T("failed to get address of UpdateDriverForPlugAndPlayDevices in newdev.dll\n"));
		goto final;
	}

	if(!UpdateFn(NULL, hwIdList, inf, flags, &reboot))
	{
		err = GetLastError();
		_tprintf(__T("UpdateDriverForPlugAndPlayDevices failed\n"));
		_tprintf(__T("error code: 0x%08x\n"), err);
		switch(err)
		{
		case ERROR_FILE_NOT_FOUND:
			_tprintf(__T("The path that was specified for FullInfPath does not exist.\n"));
			break;
		case ERROR_IN_WOW64:
			_tprintf(__T("The calling application is a 32-bit application attempting to execute in a 64-bit environment, which is not allowed.\n"));
			break;
		case ERROR_INVALID_FLAGS:
			_tprintf(__T("The value specified for InstallFlags is invalid.\n"));
			break;
		case ERROR_NO_SUCH_DEVINST:
			_tprintf(__T("The value specified for HardwareId does not match any device on the system. That is, the device is not plugged in.\n"));
			break;
		case ERROR_NO_MORE_ITEMS:
			_tprintf(__T("The function found a match for the HardwareId value, but the specified driver was not a better match than the current driver and the caller did not specify the INSTALLFLAG_FORCE flag.\n"));
			break;
		}
		goto final;
	}

	if(reboot)
	{
		_tprintf(__T("YOU NEED TO REBOOT\n"));
	}
	failcode = 0;

final:
	if(newdevMod) {
		FreeLibrary(newdevMod);
	}

	return failcode;
}

int updateNI(TCHAR* inf)
{
	// turn off interactive mode while doing the update
	HMODULE setupapiMod = NULL;
	SetupSetNonInteractiveModeProto SetNIFn;
	int res;
	BOOL prev;

	setupapiMod = LoadLibrary(TEXT("setupapi.dll"));
	if(!setupapiMod)
	{
		_tprintf(__T("failed to load setupapi.dll\n -> Sorry, non-interactive mode not available.\n"));
		return update(inf);
	}
	SetNIFn = (SetupSetNonInteractiveModeProto)GetProcAddress(setupapiMod, SETUPSETNONINTERACTIVEMODE);
	if(!SetNIFn)
	{
		FreeLibrary(setupapiMod);
		_tprintf(__T("failed to get address of SetupSetNonInteractiveMode\n -> Sorry, non-interactive mode not available.\n"));
		return update(inf);
	}
	prev = SetNIFn(TRUE);
	_tprintf(__T("using non-interactive mode\n"));
	res = update(inf);
	SetNIFn(prev);
	FreeLibrary(setupapiMod);
	return res;
}

int RemoveCallback(HDEVINFO Devs, PSP_DEVINFO_DATA DevInfo)
{
	SP_REMOVEDEVICE_PARAMS rmdParams;
	SP_DEVINSTALL_PARAMS devParams;

	// need hardware ID before trying to remove, as we wont have it after
	TCHAR devID[MAX_DEVICE_ID_LEN];
	SP_DEVINFO_LIST_DETAIL_DATA devInfoListDetail;

	devInfoListDetail.cbSize = sizeof(devInfoListDetail);
	if((!SetupDiGetDeviceInfoListDetail(Devs, &devInfoListDetail)) ||
			(CM_Get_Device_ID_Ex(DevInfo->DevInst, devID, MAX_DEVICE_ID_LEN, 0, devInfoListDetail.RemoteMachineHandle) != CR_SUCCESS))
	{
		// skip this
		return 0;
	}

	rmdParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
	rmdParams.ClassInstallHeader.InstallFunction = DIF_REMOVE;
	rmdParams.Scope = DI_REMOVEDEVICE_GLOBAL;
	rmdParams.HwProfile = 0;
	if(!SetupDiSetClassInstallParams(Devs, DevInfo, &rmdParams.ClassInstallHeader, sizeof(rmdParams)) ||
	   !SetupDiCallClassInstaller(DIF_REMOVE, Devs, DevInfo))
	{
		_tprintf(__T("failed to invoke DIF_REMOVE\n"));
	}
	else
	{
		// see if device needs reboot
		devParams.cbSize = sizeof(devParams);
		if(SetupDiGetDeviceInstallParams(Devs, DevInfo, &devParams) && (devParams.Flags & (DI_NEEDRESTART | DI_NEEDREBOOT)))
		{
			// reboot required
			_tprintf(__T("YOU NEED TO REBOOT\n"));
		}
		else
		{
			// appears to have succeeded
			_tprintf(__T("device removed\n"));
		}
	}
	_tprintf(TEXT("%-60s\n"), devID);

	return 0;
}

LPTSTR* GetMultiSzIndexArray(LPTSTR MultiSz)
{
	LPTSTR scan;
	LPTSTR* array;
	int elements;

	for(scan = MultiSz, elements = 0; scan[0]; elements++)
	{
		scan += lstrlen(scan) + 1;
	}
	array = new LPTSTR[elements + 2];
	if(!array)
	{
		return NULL;
	}
	array[0] = MultiSz;
	array++;
	if(elements)
	{
		for(scan = MultiSz, elements = 0; scan[0]; elements++)
		{
			array[elements] = scan;
			scan += lstrlen(scan) + 1;
		}
	}
	array[elements] = NULL;
	return array;
}

void DelMultiSz(LPTSTR* Array)
{
	if(Array)
	{
		Array--;
		if(Array[0])
		{
			delete [] Array[0];
		}
		delete [] Array;
	}
}

LPTSTR* GetDevMultiSz(HDEVINFO Devs, PSP_DEVINFO_DATA DevInfo, DWORD Prop)
{
	LPTSTR buffer;
	DWORD size;
	DWORD reqSize;
	DWORD dataType;
	LPTSTR* array;
	DWORD szChars;

	size = 8192; // initial guess, nothing magic about this
	buffer = new TCHAR[(size / sizeof(TCHAR)) + 2];
	if(!buffer)
	{
		return NULL;
	}
	while(!SetupDiGetDeviceRegistryProperty(Devs, DevInfo, Prop, &dataType, (LPBYTE)buffer, size, &reqSize))
	{
		if(GetLastError() != ERROR_INSUFFICIENT_BUFFER)
		{
			goto failed;
		}
		if(dataType != REG_MULTI_SZ)
		{
			goto failed;
		}
		size = reqSize;
		delete [] buffer;
		buffer = new TCHAR[(size / sizeof(TCHAR)) + 2];
		if(!buffer)
		{
			goto failed;
		}
	}
	szChars = reqSize / sizeof(TCHAR);
	buffer[szChars] = TEXT('\0');
	buffer[szChars + 1] = TEXT('\0');
	array = GetMultiSzIndexArray(buffer);
	if(array)
	{
		return array;
	}

failed:
	if(buffer)
	{
		delete [] buffer;
	}
	return NULL;
}

typedef int (*CallbackFunc)(HDEVINFO Devs, PSP_DEVINFO_DATA DevInfo);

int EnumerateDevices(CallbackFunc Callback)
{
	HDEVINFO devs = INVALID_HANDLE_VALUE;
	int failcode = 1;
	int retcode;
	DWORD devIndex;
	SP_DEVINFO_DATA devInfo;
	SP_DEVINFO_LIST_DETAIL_DATA devInfoListDetail;
	BOOL match;
	GUID cls;
	DWORD numClass = 0;

	if(!SetupDiClassGuidsFromNameEx(__T("System"), &cls, 1, &numClass, NULL, NULL) &&
	   GetLastError() != ERROR_INSUFFICIENT_BUFFER)
	{
		goto final;
	}
	if(!numClass)
	{
		goto final;
	}

	// add all id's to list
	// filter on class
	devs = SetupDiGetClassDevsEx(&cls,
	                             NULL,
	                             NULL,
	                             DIGCF_PRESENT,
	                             NULL,
	                             NULL,
	                             NULL);

	if(devs == INVALID_HANDLE_VALUE)
	{
		goto final;
	}

	devInfoListDetail.cbSize = sizeof(devInfoListDetail);
	if(!SetupDiGetDeviceInfoListDetail(devs, &devInfoListDetail))
	{
		goto final;
	}

	devInfo.cbSize = sizeof(devInfo);
	for(devIndex = 0; SetupDiEnumDeviceInfo(devs, devIndex, &devInfo); devIndex++)
	{
		match = FALSE;
		TCHAR devID[MAX_DEVICE_ID_LEN];
		LPTSTR* hwIds = NULL;
		LPTSTR* compatIds = NULL;
		LPTSTR* ids;

		// determine instance ID
		if(CM_Get_Device_ID_Ex(devInfo.DevInst, devID, MAX_DEVICE_ID_LEN, 0, devInfoListDetail.RemoteMachineHandle) != CR_SUCCESS)
		{
			devID[0] = TEXT('\0');
		}

		// determine hardware ID's
		// and search for match
		hwIds = GetDevMultiSz(devs, &devInfo, SPDRP_HARDWAREID);
		compatIds = GetDevMultiSz(devs, &devInfo, SPDRP_COMPATIBLEIDS);

		ids = hwIds;
		if(ids)
		{
			while(ids[0])
			{
				if(!_tcscmp(ids[0], __T("ROOT\\VIRTUSB")) ||
				   !_tcscmp(ids[0], __T("root\\virtusb")))
				{
					_tprintf(__T("%s\n"), ids[0]);
					match = TRUE;
				}
				ids++;
			}
		}

		ids = compatIds;
		if(!match && ids)
		{
			while(ids[0])
			{
				if(!_tcscmp(ids[0], __T("ROOT\\VIRTUSB")) ||
				   !_tcscmp(ids[0], __T("root\\virtusb")))
				{
					_tprintf(__T("%s\n"), ids[0]);
					match = TRUE;
				}
				ids++;
			}
		}

		DelMultiSz(hwIds);
		DelMultiSz(compatIds);

		if(match)
		{
			retcode = Callback(devs, &devInfo);
			if(retcode)
			{
				failcode = retcode;
				goto final;
			}
		}
	}

	failcode = 0;

final:
	if(devs != INVALID_HANDLE_VALUE)
	{
		SetupDiDestroyDeviceInfoList(devs);
	}
	return failcode;
}

int uninstall()
{
	return EnumerateDevices(RemoveCallback);
}

#pragma managed
using namespace System;
using namespace System::IO;
using namespace System::Security::AccessControl;
using namespace System::Security::Principal;
using namespace System::Security;
using namespace Microsoft::Win32;

int cleanreg()
{
	int res = uninstall();

	DirectoryInfo^ dir = gcnew DirectoryInfo(System::Environment::SystemDirectory + L"\\..\\inf");
	for each(FileInfo^ file in dir->GetFiles(L"oem*.inf"))
	{
		StreamReader^ sr = file->OpenText();
		String^ data = sr->ReadToEnd();
		sr->Close();
		if(data->Contains(L"VIRTUSB") || data->Contains(L"VUSBVHCI"))
		{
			String^ fname = file->FullName;
			String^ pnf = fname->Substring(0, fname->Length - 3) + L"PNF";
			Console::WriteLine(L"deleting " + fname);
			try { file->Delete(); } catch(Exception^) { Console::WriteLine(L"FAILED: " + fname); }
			Console::WriteLine(L"deleting " + pnf);
			try { File::Delete(pnf); } catch(Exception^) { Console::WriteLine(L"FAILED: " + pnf); }
		}
	}

	//Console::WriteLine(L"opening " + Registry::LocalMachine->Name + L"\\SYSTEM\\CurrentControlSet");
	RegistryKey^ cs = Registry::LocalMachine->OpenSubKey(L"SYSTEM\\CurrentControlSet");

	RegistryKey^ k = nullptr;
	array<String^>^ subs;

	try
	{
		//Console::WriteLine(L"opening " + cs->Name + L"\\Control\\Class\\{36FC9E60-C465-11CF-8056-444553540000}");
		k = cs->OpenSubKey(L"Control\\Class\\{36FC9E60-C465-11CF-8056-444553540000}", true);
		subs = k->GetSubKeyNames();
		Array::Sort<String^>(subs);
		bool match = false;
		for each(String^ sub in subs)
		{
			if(sub != L"Properties")
			{
				try
				{
					//Console::WriteLine(L"opening " + k->Name + L"\\" + sub);
					RegistryKey^ sk = k->OpenSubKey(sub);
					String^ inf = dynamic_cast<String^>(sk->GetValue(L"InfSection"));
					sk->Close();
					if(inf &&
						(inf->ToLower()->StartsWith(L"virtusb") || inf->ToLower()->StartsWith(L"vusbvhci") ||
					   (match && inf->ToLower()->StartsWith(L"roothub"))))
					{
						match = true;
						Console::WriteLine(L"deleting " + k->Name + L"\\" + sub);
						k->DeleteSubKeyTree(sub);
					}
					else
					{
						match = false;
					}
				}
				catch(Exception^) { Console::WriteLine(L"FAILED: " + k->Name + L"\\" + sub); }
			}
		}
	}
	catch(Exception^)
	{
		Console::WriteLine(L"FAILED: " + cs->Name + L"\\Control\\Class\\{36FC9E60-C465-11CF-8056-444553540000}");
		Console::WriteLine(L"Please make sure you are Administrator.");
	}
	finally
	{
		if(k)
			k->Close();
		k = nullptr;
	}

	try
	{
		//Console::WriteLine(L"opening " + cs->Name + L"\\Control\\Class\\{4D36E97D-E325-11CE-BFC1-08002BE10318}");
		k = cs->OpenSubKey(L"Control\\Class\\{4D36E97D-E325-11CE-BFC1-08002BE10318}", true);
		subs = k->GetSubKeyNames();
		for each(String^ sub in subs)
		{
			if(sub != L"Properties")
			{
				try
				{
					//Console::WriteLine(L"opening " + k->Name + L"\\" + sub);
					RegistryKey^ sk = k->OpenSubKey(sub);
					String^ inf = dynamic_cast<String^>(sk->GetValue(L"InfSection"));
					sk->Close();
					if(inf && (inf->ToLower()->StartsWith(L"virtusb") || inf->ToLower()->StartsWith(L"vusbvhci")))
					{
						Console::WriteLine(L"deleting " + k->Name + L"\\" + sub);
						k->DeleteSubKeyTree(sub);
					}
				}
				catch(Exception^) { Console::WriteLine(L"FAILED: " + k->Name + L"\\" + sub); }
			}
		}
	}
	catch(Exception^)
	{
		Console::WriteLine(L"FAILED: " + cs->Name + L"\\Control\\Class\\{4D36E97D-E325-11CE-BFC1-08002BE10318}");
		Console::WriteLine(L"Please make sure you are Administrator.");
	}
	finally
	{
		if(k)
			k->Close();
		k = nullptr;
	}

	//Console::WriteLine(L"opening " + cs->Name + L"\\Enum");
	RegistryKey^ kperm;
	try
	{
		kperm = cs->OpenSubKey(L"Enum", RegistryKeyPermissionCheck::ReadWriteSubTree, RegistryRights::ReadPermissions | RegistryRights::ChangePermissions);
	}
	catch(Exception^)
	{
		Console::WriteLine(L"FAILED: " + cs->Name + L"\\Enum");
		kperm = nullptr;
	}
	if(kperm)
	{
		RegistrySecurity^ acl = kperm->GetAccessControl();
		RegistryAccessRule^ ace = gcnew RegistryAccessRule(gcnew SecurityIdentifier(WellKnownSidType::BuiltinAdministratorsSid, nullptr),
														   RegistryRights::FullControl,
														   InheritanceFlags::ContainerInherit,
														   PropagationFlags::None,
														   AccessControlType::Allow);
		acl->AddAccessRule(ace);
		try
		{
			kperm->SetAccessControl(acl);
			k = cs->OpenSubKey(L"Enum\\USB\\ROOT_HUB20", true);
			subs = k->GetSubKeyNames();
			for each(String^ sub in subs)
			{
				try
				{
					//Console::WriteLine(L"opening " + k->Name + L"\\" + sub);
					RegistryKey^ sk = k->OpenSubKey(sub);
					array<String^>^ ids = dynamic_cast<array<String^>^>(sk->GetValue(L"HardwareID"));
					sk->Close();
					if(ids && ids->Length && ids[0]->ToLower()->Contains(L"vid1138"))
					{
						Console::WriteLine(L"deleting " + k->Name + L"\\" + sub);
						k->DeleteSubKeyTree(sub);
					}
				}
				catch(Exception^) { Console::WriteLine(L"FAILED: " + k->Name + L"\\" + sub); }
			}
			k->Close();

			//Console::WriteLine(L"checking for " + cs->Name + L"\\Enum\\VIRTUSB");
			k = cs->OpenSubKey(L"Enum\\VIRTUSB");
			if(k)
			{
				k->Close();
				//Console::WriteLine(L"opening " + cs->Name + L"\\Enum");
				k = cs->OpenSubKey(L"Enum", true);
				Console::WriteLine(L"deleting " + k->Name + L"\\VIRTUSB");
				k->DeleteSubKeyTree(L"VIRTUSB");
				k->Close();
			}
		}
		finally
		{
			acl->RemoveAccessRuleSpecific(ace);
			kperm->SetAccessControl(acl);
		}
		kperm->Close();
	}

	//Console::WriteLine(L"checking for " + cs->Name + L"\\Services\\virtusb");
	k = cs->OpenSubKey(L"Services\\virtusb");
	if(k)
	{
		k->Close();
		k = nullptr;
		try
		{
			//Console::WriteLine(L"opening " + cs->Name + L"\\Services");
			k = cs->OpenSubKey(L"Services", true);
			Console::WriteLine(L"deleting " + k->Name + L"\\virtusb");
			k->DeleteSubKeyTree(L"virtusb");
		}
		catch(Exception^)
		{
			Console::WriteLine(L"FAILED: " + cs->Name + L"\\Services");
			Console::WriteLine(L"Please make sure you are Administrator.");
		}
		finally
		{
			if(k)
				k->Close();
			k = nullptr;
		}
	}

	//Console::WriteLine(L"checking for " + cs->Name + L"\\Services\\vusbvhci");
	k = cs->OpenSubKey(L"Services\\vusbvhci");
	if(k)
	{
		k->Close();
		k = nullptr;
		try
		{
			//Console::WriteLine(L"opening " + cs->Name + L"\\Services");
			k = cs->OpenSubKey(L"Services", true);
			Console::WriteLine(L"deleting " + k->Name + L"\\vusbvhci");
			k->DeleteSubKeyTree(L"vusbvhci");
		}
		catch(Exception^)
		{
			Console::WriteLine(L"FAILED: " + cs->Name + L"\\Services");
			Console::WriteLine(L"Please make sure you are Administrator.");
		}
		finally
		{
			if(k)
				k->Close();
			k = nullptr;
		}
	}

	//Console::WriteLine(L"checking for " + cs->Name + L"\\Control\\CriticalDeviceDatabase\\root#virtusb");
	k = cs->OpenSubKey(L"Control\\CriticalDeviceDatabase\\root#virtusb");
	if(k)
	{
		k->Close();
		k = nullptr;
		try
		{
			//Console::WriteLine(L"opening " + cs->Name + L"\\Control\\CriticalDeviceDatabase");
			k = cs->OpenSubKey(L"Control\\CriticalDeviceDatabase", true);
			Console::WriteLine(L"deleting " + k->Name + L"\\root#virtusb");
			k->DeleteSubKeyTree(L"root#virtusb");
		}
		catch(Exception^)
		{
			Console::WriteLine(L"FAILED: " + cs->Name + L"\\Control\\CriticalDeviceDatabase");
			Console::WriteLine(L"Please make sure you are Administrator.");
		}
		finally
		{
			if(k)
				k->Close();
			k = nullptr;
		}
	}

	try
	{
		//Console::WriteLine(L"opening " + cs->Name + L"\\Control\\DeviceClasses\\{3abf6f2d-71c4-462a-8a92-1e6861e6af27}");
		k = cs->OpenSubKey(L"Control\\DeviceClasses\\{3abf6f2d-71c4-462a-8a92-1e6861e6af27}", true);
		subs = k->GetSubKeyNames();
		for each(String^ sub in subs)
		{
			if(sub->ToLower()->Contains(L"virtusb"))
			{
				try
				{
					Console::WriteLine(L"deleting " + k->Name + L"\\" + sub);
					k->DeleteSubKeyTree(sub);
				}
				catch(Exception^) { Console::WriteLine(L"FAILED: " + k->Name + L"\\" + sub); }
			}
		}
	}
	catch(Exception^)
	{
		Console::WriteLine(L"FAILED: " + cs->Name + L"\\Control\\DeviceClasses\\{3abf6f2d-71c4-462a-8a92-1e6861e6af27}");
		Console::WriteLine(L"Please make sure you are Administrator.");
	}
	finally
	{
		if(k)
			k->Close();
		k = nullptr;
	}

	cs->Close();

	try
	{
		//Console::WriteLine(L"opening " + Registry::LocalMachine->Name + L"\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Reinstall");
		k = Registry::LocalMachine->OpenSubKey(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Reinstall", true);
		if(k)
		{
			subs = k->GetSubKeyNames();
			for each(String^ sub in subs)
			{
				try
				{
					//Console::WriteLine(L"opening " + k->Name + L"\\" + sub);
					RegistryKey^ sk = k->OpenSubKey(sub);
					String^ mfg = dynamic_cast<String^>(sk->GetValue(L"Mfg"));
					sk->Close();
					if(mfg && (mfg->ToLower()->Contains(L"virtusb") || mfg->ToLower()->Contains(L"vusbvhci")))
					{
						Console::WriteLine(L"deleting " + k->Name + L"\\" + sub);
						k->DeleteSubKeyTree(sub);
					}
				}
				catch(Exception^) { Console::WriteLine(L"FAILED: " + k->Name + L"\\" + sub); }
			}
		}
	}
	catch(Exception^)
	{
		Console::WriteLine(L"FAILED: " + Registry::LocalMachine->Name + L"\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Reinstall");
		Console::WriteLine(L"Please make sure you are Administrator.");
	}
	finally
	{
		if(k)
			k->Close();
	}

	return res;
}
