#include <initguid.h>
#include <windows.h>
#include <winuser.h>
#include <wincon.h>
#include "crtdbg.h"
#define _FAR_NO_NAMELESS_UNIONS
#include "plugin.hpp"
#include "farcolor.hpp"
#include "guid.hpp"
#include "lng.hpp"
#include "version.hpp"
#define MCHKHEAP //_ASSERT(HeapValidate(GetProcessHeap(),0,nullptr))


static const DWORD MODIFIER_PRESSED =
RIGHT_ALT_PRESSED |
LEFT_ALT_PRESSED |
RIGHT_CTRL_PRESSED |
LEFT_CTRL_PRESSED |
SHIFT_PRESSED;

static const DWORD ALT_PRESSED =
RIGHT_ALT_PRESSED |
LEFT_ALT_PRESSED;

static const DWORD CTRL_PRESSED =
RIGHT_CTRL_PRESSED |
LEFT_CTRL_PRESSED;

static const int64_t KiloByte = 1024ULL;
static const int64_t MegaByte = KiloByte * 1024ULL;
static const int64_t GigaByte = MegaByte * 1024ULL;
static const int64_t TeraByte = GigaByte * 1024ULL;
static const int64_t PetaByte = TeraByte * 1024ULL;

static struct PluginStartupInfo StartupInfo;

struct DirSize
{
	uint64_t Size;
	uint64_t Persent;
	wchar_t *Dir;
	wchar_t *Text;
	wchar_t *Graph;
	DirSize *NextDir;
	DirSize *PrevDir;
	wchar_t Dimension[20];
};

struct InsidePluginData
{
	HANDLE hDialog;
	HANDLE hDialogThread;
	DirSize *FirstDir;
	DirSize *FirstDrawElement;
	DirSize *LastDrawElement;
	int CountItem;
	BOOL Network;
	BOOL Signal;
	BOOL IgnoreSymLinks;
	BOOL DoubleCall;
	BOOL Restart;
	int Called;
	size_t SizeofCurDir;
	wchar_t *PanelCurDir;
	PanelInfo PInfo;
};

uint64_t CalcSizeRecursive(wchar_t *Dir, InsidePluginData *InData);
DWORD WINAPI DDialogThread(void *lpData);
intptr_t WINAPI DialogProc(HANDLE hDlg, intptr_t Msg, intptr_t Param1, void *Param2);
intptr_t WINAPI DialogProc1(HANDLE hDlg, intptr_t Msg, intptr_t Param1, void *Param2);
void ReSetItem(InsidePluginData *InData);

const wchar_t *GetMsg(int MsgId) {
	return StartupInfo.GetMsg(&MainGuid, MsgId);
}

HANDLE WINAPI OpenW(const OpenInfo *OInfo)
{
	if (OInfo->StructSize < sizeof(OpenInfo))
		return nullptr;

	InsidePluginData *InData = nullptr;
	DirSize *CurrentDir = nullptr;
	InData = (InsidePluginData *)GlobalLock(GlobalAlloc(GHND, sizeof(InsidePluginData)));
	do
	{
		InData->Called = 0;
		InData->Signal = TRUE;
		InData->Network = TRUE;
		InData->Restart = FALSE;

		size_t bufSize = StartupInfo.PanelControl(PANEL_ACTIVE, FCTL_GETPANELDIRECTORY, 0, nullptr);
		HGLOBAL hDirInfo = GlobalAlloc(GHND, bufSize);
		FarPanelDirectory* dirInfo = static_cast<FarPanelDirectory*>(GlobalLock(hDirInfo));
		dirInfo->StructSize = sizeof(FarPanelDirectory);
		StartupInfo.PanelControl(PANEL_ACTIVE, FCTL_GETPANELDIRECTORY, bufSize, dirInfo);

		InData->SizeofCurDir = wcslen(dirInfo->Name) + 1 + 8;
		InData->PanelCurDir = static_cast<wchar_t *>(GlobalLock(GlobalAlloc(GHND,
			InData->SizeofCurDir * sizeof(wchar_t))));
		wcscpy(InData->PanelCurDir, dirInfo->Name);

		if (InData->PanelCurDir[0] != L'\\')
		{
			InData->Network = FALSE;
			wcscpy(InData->PanelCurDir, L"\\\\?\\");
			wcscpy(&InData->PanelCurDir[4], dirInfo->Name);
		}

		if (wcscmp(&InData->PanelCurDir[wcslen(InData->PanelCurDir) - 1], L"\\") == 0)
			wcscat(InData->PanelCurDir, L"*");
		else
			wcscat(InData->PanelCurDir, L"\\*");

		GlobalUnlock(hDirInfo);
		GlobalFree(hDirInfo);

		InData->PInfo.StructSize = sizeof(PanelInfo);
		StartupInfo.PanelControl(PANEL_PASSIVE, FCTL_GETPANELINFO, 0, &InData->PInfo);

		DWORD d;
		InData->hDialogThread = CreateThread(nullptr, 0, DDialogThread, InData, 0, &d);

		FarDialogItem DItem[] = {
		/*      Type         X1 Y1  X2 Y2  Sel    Hist     Mask     Flags        Data               */
		/* 0*/{ DI_DOUBLEBOX, 0, 0, 52, 2, { 0 }, nullptr, nullptr, DIF_FOCUS  , GetMsg(MCaption)    },
		/* 1*/{ DI_TEXT     , 2, 1, 50, 1, { 0 }, nullptr, nullptr, 0          , GetMsg(MProcessing) },
		/* 2*/{ DI_TEXT     , 2, 2, 50, 2, { 0 }, nullptr, nullptr, 0          , nullptr             },
		};
		DItem[2].Data = (InData->IgnoreSymLinks) ? GetMsg(MProcessLinks) : GetMsg(MSkipLinks);
		DItem[2].X2 = wcslen(DItem[2].Data) - 1;

		InData->hDialog = StartupInfo.DialogInit(&MainGuid, &Dlg1Guid,
			-1, -1, 53, 3,
			nullptr,
			DItem, _countof(DItem),
			0, FDLG_SMALLDIALOG, DialogProc1, InData);

		StartupInfo.DialogRun(InData->hDialog);
		StartupInfo.DialogFree(InData->hDialog);

		GlobalUnlock(GlobalHandle(InData->PanelCurDir));
		GlobalFree(GlobalHandle(InData->PanelCurDir));
		if (InData->Signal && !InData->Restart)
		{
			InData->hDialog = 0;
			int PanelHeight = InData->PInfo.PanelRect.bottom - InData->PInfo.PanelRect.top;
			int PanelWidth = InData->PInfo.PanelRect.right - InData->PInfo.PanelRect.left - 4;
			int Count = (PanelHeight - 2) / 2;
			int i = 0;
			InData->FirstDrawElement = InData->FirstDir;
			CurrentDir = InData->FirstDir;
			while (CurrentDir != nullptr)
			{
				i++;
				if (i == Count) break;
				if (CurrentDir->NextDir == nullptr) break;
				else CurrentDir = CurrentDir->NextDir;
			}
			InData->CountItem = i;
			InData->LastDrawElement = CurrentDir;

			HGLOBAL hDlgItem = GlobalAlloc(GHND, sizeof(FarDialogItem)*((InData->CountItem * 2) + 2));
			FarDialogItem *DlgItems = (FarDialogItem *)GlobalLock(hDlgItem);

			CurrentDir = InData->FirstDrawElement;
			for (i = 2; i < (InData->CountItem * 2) + 1; i += 2)
			{
				DlgItems[i].Type = DI_TEXT;
				DlgItems[i].X1 = 2;
				DlgItems[i].Y1 = i - 1;
				DlgItems[i].X2 = wcslen(CurrentDir->Text) + 1;
				DlgItems[i].Y2 = i - 1;
				DlgItems[i].Flags = 0;
				DlgItems[i].Data = CurrentDir->Text;

				DlgItems[i + 1].Type = DI_TEXT;
				DlgItems[i + 1].X1 = 2;
				DlgItems[i + 1].Y1 = i;
				DlgItems[i + 1].X2 = wcslen(CurrentDir->Graph) + 1;
				DlgItems[i + 1].Y2 = i;
				DlgItems[i + 1].Flags = 0;
				DlgItems[i + 1].Data = CurrentDir->Graph;

				CurrentDir = CurrentDir->NextDir;
			}

			DlgItems[0].Type = DI_DOUBLEBOX;
			DlgItems[0].X1 = 0;
			DlgItems[0].X2 = PanelWidth + 4;
			DlgItems[0].Y1 = 0;
			DlgItems[0].Y2 = PanelHeight;
			DlgItems[0].Flags = DIF_FOCUS;
			DlgItems[0].Data = GetMsg(MCaption);

			DlgItems[1].Type = DI_TEXT;
			DlgItems[1].Data = (InData->IgnoreSymLinks) ? GetMsg(MUpdateWithLinks) : GetMsg(MUpdateNoLinks);
			DlgItems[1].X1 = 2;
			DlgItems[1].Y1 = PanelHeight;
			DlgItems[1].X2 = wcslen(DlgItems[1].Data) - 1;
			DlgItems[1].Y2 = PanelHeight;

			InData->hDialog = StartupInfo.DialogInit(&MainGuid, &DlgGuid,
				InData->PInfo.PanelRect.left, InData->PInfo.PanelRect.top,
				InData->PInfo.PanelRect.right, InData->PInfo.PanelRect.bottom,
				nullptr,
				DlgItems, (InData->CountItem * 2) + 2,
				0,
				FDLG_NODRAWSHADOW | FDLG_SMALLDIALOG, DialogProc, InData);
			StartupInfo.DialogRun(InData->hDialog);
			StartupInfo.DialogFree(InData->hDialog);
			GlobalUnlock(hDlgItem);
			GlobalFree(hDlgItem);
		}
	} while (InData->Restart);
	GlobalUnlock(GlobalHandle(InData));
	GlobalFree(GlobalHandle(InData));
	InData = nullptr;
	return nullptr;
}

void WINAPI GetGlobalInfoW(GlobalInfo *Info)
{
	Info->StructSize = sizeof(GlobalInfo);
	Info->MinFarVersion = MAKEFARVERSION(3, 0, 0, 2927, VS_RELEASE);
	Info->Version = MAKEFARVERSION(PLUGIN_MAJOR, PLUGIN_MINOR, 0, 0, VS_RELEASE);
	Info->Guid = MainGuid;
	Info->Title = PLUGIN_NAME;
	Info->Description = PLUGIN_DESC;
	Info->Author = PLUGIN_AUTHOR;
}

void WINAPI GetPluginInfoW(PluginInfo *Info)
{
	static const wchar_t *DiskMenuStrings[1] = { GetMsg(MPluginsMenuString) };
	static const GUID DiskMenuGuids[1] = { DiskMenuGuid };
	static const wchar_t *PluginMenuStrings[1] = { GetMsg(MPluginsMenuString) };
	static const GUID PluginMenuGuids[1] = { PluginMenuGuid };

	Info->StructSize = sizeof(PluginInfo);

	Info->DiskMenu.Guids = DiskMenuGuids;
	Info->DiskMenu.Strings = DiskMenuStrings;
	Info->DiskMenu.Count = 1;

	Info->PluginMenu.Guids = PluginMenuGuids;
	Info->PluginMenu.Strings = PluginMenuStrings;
	Info->PluginMenu.Count = 1;
}

void WINAPI SetStartupInfoW(const PluginStartupInfo *StartInfo)
{
	StartupInfo = *StartInfo;
}

intptr_t WINAPI ProcessSynchroEventW(const ProcessSynchroEventInfo *Info)
{
	if (Info->StructSize < sizeof(ProcessSynchroEventInfo))
		return 0;

	if (Info->Event == SE_COMMONSYNCHRO)
	{
		InsidePluginData *InData = static_cast<InsidePluginData *>(Info->Param);
		if (InData->Called == 1)
			StartupInfo.SendDlgMessage(InData->hDialog, DM_CLOSE, 555, 0);
	}
	return 0;
}


uint64_t CalcSizeRecursive(wchar_t *Dir, InsidePluginData *InData)
{
	WIN32_FIND_DATAW Data;
	uint64_t Size = 0;
	size_t StrLen = wcslen(Dir);
	HGLOBAL hDir = GlobalAlloc(GHND, (StrLen + 3) * sizeof(wchar_t));
	wchar_t *tDir = (wchar_t *)GlobalLock(hDir);
	wcscpy(tDir, Dir);
	wcscat(tDir, L"\\*");
	if (!InData->Signal)
	{
		GlobalUnlock(hDir);
		GlobalFree(hDir);
		return 0;
	}
	HANDLE File = FindFirstFileW(tDir, &Data);
	if (File == INVALID_HANDLE_VALUE) return 0;
	do
	{
		if (Data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)
		{
			if (wcscmp(Data.cFileName, L".") != 0 && wcscmp(Data.cFileName, L"..") != 0)
			{
				if (InData->IgnoreSymLinks)
					if (Data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
						if (Data.dwReserved0&IO_REPARSE_TAG_SYMLINK) continue;
				HGLOBAL hTmpDir = GlobalAlloc(GHND, (wcslen(Data.cFileName) + wcslen(Dir) + 2) * sizeof(wchar_t));
				wchar_t *TmpDir = (wchar_t *)GlobalLock(hTmpDir);
				wcscpy(TmpDir, Dir);
				wcscat(TmpDir, L"\\");
				wcscat(TmpDir, Data.cFileName);
				Size += CalcSizeRecursive(TmpDir, InData);
				GlobalUnlock(hTmpDir);
				GlobalFree(hTmpDir);
			}
		}
		else Size += ((int64_t)Data.nFileSizeHigh << 32) + Data.nFileSizeLow;

		if (!InData->Signal)
		{
			FindClose(File);
			GlobalUnlock(hDir);
			GlobalFree(hDir);
			return 0;
		}
	} while (FindNextFileW(File, &Data));
	FindClose(File);
	GlobalUnlock(hDir);
	GlobalFree(hDir);
	return Size;
}

DWORD WINAPI DDialogThread(void *lpData)
{
	InsidePluginData *InData = (InsidePluginData *)lpData;
	WIN32_FIND_DATAW Data;
	HANDLE File;
	DirSize *CurrentDir = nullptr;
	InData->FirstDir = nullptr;
	DirSize *InFiles = nullptr;
	File = FindFirstFileW(InData->PanelCurDir, &Data);
	if (File == INVALID_HANDLE_VALUE)
	{
		InData->Called = 1;
		StartupInfo.AdvControl(&MainGuid, ACTL_SYNCHRO, 0, InData);
		return 0;
	}
	do
	{
		if (Data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)
		{
			if (wcscmp(Data.cFileName, L".") != 0 && wcscmp(Data.cFileName, L"..") != 0)
			{
				if (InData->IgnoreSymLinks)
					if (Data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
						if (Data.dwReserved0 & IO_REPARSE_TAG_SYMLINK) continue;
				if (InData->FirstDir == nullptr)
				{
					InData->FirstDir = (DirSize *)GlobalLock(GlobalAlloc(GHND, sizeof(DirSize)));
					CurrentDir = InData->FirstDir;
				}
				else
				{
					DirSize *TmpDir;
					TmpDir = (DirSize *)GlobalLock(GlobalAlloc(GHND, sizeof(DirSize)));
					TmpDir->PrevDir = CurrentDir;
					CurrentDir->NextDir = TmpDir;
					CurrentDir = TmpDir;
				}
				CurrentDir->Dir = (wchar_t *)GlobalLock(GlobalAlloc(GHND, (wcslen(Data.cFileName) + 1) * sizeof(wchar_t)));
				wcscpy(CurrentDir->Dir, Data.cFileName);

				HGLOBAL hNDir = GlobalAlloc(GHND, (wcslen(CurrentDir->Dir) + (InData->SizeofCurDir - 4) + 1) * sizeof(wchar_t));
				wchar_t *wTmpDir = (wchar_t *)GlobalLock(hNDir);
				wcscpy(wTmpDir, InData->PanelCurDir);
				if (!InData->Network) wTmpDir[InData->SizeofCurDir - 5] = 0;
				else wTmpDir[InData->SizeofCurDir - 8] = 0;
				if (wcscmp(&wTmpDir[wcslen(wTmpDir) - 1], L"\\") != 0) wcscat(wTmpDir, L"\\");
				wcscat(wTmpDir, CurrentDir->Dir);
				CurrentDir->Size = CalcSizeRecursive(wTmpDir, InData);
				GlobalUnlock(hNDir);
				GlobalFree(hNDir);
			}
		}
		else
		{
			if (InFiles == nullptr)
			{
				const wchar_t *StrInFiles = GetMsg(MInFiles);
				InFiles = (DirSize *)GlobalLock(GlobalAlloc(GHND, sizeof(DirSize)));
				InFiles->Dir = (wchar_t *)GlobalLock(GlobalAlloc(GHND, (wcslen(StrInFiles) + 1) * sizeof(wchar_t)));
				wcscpy(InFiles->Dir, StrInFiles);
				InFiles->NextDir = nullptr;
			}
			InFiles->Size += ((int64_t)Data.nFileSizeHigh << 32) + Data.nFileSizeLow;
		}

		if (!InData->Signal)
		{
			if (InFiles)
				GlobalUnlock(GlobalHandle(InFiles));
			GlobalFree(GlobalHandle(InFiles));
			FindClose(File);
			/* SetEvent(InData->hEvent);*/
			return 0;
		}
	} while (FindNextFileW(File, &Data));
	FindClose(File);
	if (CurrentDir != nullptr) CurrentDir->NextDir = InFiles;
	if (InFiles != nullptr) InFiles->PrevDir = CurrentDir;

	if (InData->FirstDir == nullptr)
	{
		if (InFiles == nullptr)
			return 0;
		InData->FirstDir = InFiles;
	}
	CurrentDir = InData->FirstDir;
	while (CurrentDir->NextDir != nullptr)
	{
		DirSize *TmpDir = CurrentDir->NextDir;
		while (TmpDir != nullptr)
		{
			if (TmpDir->Size > CurrentDir->Size)
			{
				DirSize Buf;
				Buf.Dir = CurrentDir->Dir;
				Buf.Size = CurrentDir->Size;

				CurrentDir->Dir = TmpDir->Dir;
				CurrentDir->Size = TmpDir->Size;

				TmpDir->Dir = Buf.Dir;
				TmpDir->Size = Buf.Size;
			}
			TmpDir = TmpDir->NextDir;
		}
		CurrentDir = CurrentDir->NextDir;
		if (!InData->Signal)
		{
			/*SetEvent(InData->hEvent);*/
			return 0;
		}
	}

	CurrentDir = InData->FirstDir;
	while (CurrentDir != nullptr)
	{
		int64_t BufSize = CurrentDir->Size;
		int64_t n = 2, b;
		int i = 0;
		while (n > 0)
		{
			n = BufSize >> 10;
			if (n > 0)
			{
				BufSize = n;
				if (i < 5) i++;
			}
		}
		wchar_t Drob[3];
		_i64tow(BufSize, CurrentDir->Dimension, 10);
		MCHKHEAP;
		switch (i)
		{
		case 0: {
			wcscat(CurrentDir->Dimension, L" ");
			wcscat(CurrentDir->Dimension, GetMsg(MBytes));
			break;
		}
		case 1: {
			b = CurrentDir->Size - (BufSize << 10);
			if (b > 0)
				n = (b * 10) / KiloByte;
			if (n > 1)
			{
				_i64tow(n, Drob, 10);
				MCHKHEAP;
				wcscat(CurrentDir->Dimension, L",");
				wcscat(CurrentDir->Dimension, Drob);
			}
			wcscat(CurrentDir->Dimension, L" ");
			wcscat(CurrentDir->Dimension, GetMsg(MKiloBytes));
			break;
		}
		case 2: {
			b = CurrentDir->Size - (BufSize << 20);
			if (b > 0)
				n = (b * 10) / MegaByte;
			if (n > 1)
			{
				_i64tow(n, Drob, 10);
				MCHKHEAP;
				wcscat(CurrentDir->Dimension, L",");
				wcscat(CurrentDir->Dimension, Drob);
			}
			wcscat(CurrentDir->Dimension, L" ");
			wcscat(CurrentDir->Dimension, GetMsg(MMegaBytes));
			break;
		}
		case 3: {
			b = CurrentDir->Size - (BufSize << 30);
			if (b > 0)
				n = (b * 10) / GigaByte;
			if (n > 1)
			{
				_i64tow(n, Drob, 10);
				MCHKHEAP;
				wcscat(CurrentDir->Dimension, L",");
				wcscat(CurrentDir->Dimension, Drob);
			}
			wcscat(CurrentDir->Dimension, L" ");
			wcscat(CurrentDir->Dimension, GetMsg(MGigaBytes));
			break;
		}
		case 4: {
			b = CurrentDir->Size - (BufSize << 40);
			if (b > 0)
				n = (b * 10) / TeraByte;
			if (n > 1)
			{
				_i64tow(n, Drob, 10);
				MCHKHEAP;
				wcscat(CurrentDir->Dimension, L",");
				wcscat(CurrentDir->Dimension, Drob);
			}
			wcscat(CurrentDir->Dimension, L" ");
			wcscat(CurrentDir->Dimension, GetMsg(MTeraBytes));
			break;
		}
		case 5: {
			b = CurrentDir->Size - (BufSize << 50);
			if (b > 0)
				n = (b * 10) / PetaByte;
			if (n > 1)
			{
				_i64tow(n, Drob, 10);
				MCHKHEAP;
				wcscat(CurrentDir->Dimension, L",");
				wcscat(CurrentDir->Dimension, Drob);
			}
			wcscat(CurrentDir->Dimension, L" ");
			wcscat(CurrentDir->Dimension, GetMsg(MPetaBytes));
			break;
		}
		}
		CurrentDir = CurrentDir->NextDir;
		if (!InData->Signal)
		{
			/*SetEvent(InData->hEvent);*/
			return 0;
		}
	}

	size_t PanelWidth = InData->PInfo.PanelRect.right - InData->PInfo.PanelRect.left - 4;
	CurrentDir = InData->FirstDir;
	while (CurrentDir != nullptr && InData->Signal)
	{
		CurrentDir->Persent = (InData->FirstDir->Size != 0) ? 
			CurrentDir->Size * PanelWidth / InData->FirstDir->Size : 
			0;
		CurrentDir = CurrentDir->NextDir;
		if (!InData->Signal)
		{
			/* SetEvent(InData->hEvent);*/
			return 0;
		}
	}
	CurrentDir = InData->FirstDir;
	while (CurrentDir != nullptr && InData->Signal)
	{
		CurrentDir->Text = (wchar_t *)GlobalLock(GlobalAlloc(GHND, sizeof(wchar_t)*(PanelWidth + 1)));
		MCHKHEAP;
		if (wcslen(CurrentDir->Dir) > PanelWidth)
			for (unsigned int l = 0; l < PanelWidth - wcslen(CurrentDir->Dimension) - 1; l++)
				CurrentDir->Text[l] = CurrentDir->Dir[l];
		else wcscpy(CurrentDir->Text, CurrentDir->Dir);
		MCHKHEAP;
		if (wcslen(CurrentDir->Dir) > PanelWidth - wcslen(CurrentDir->Dimension) - 1)
		{
			CurrentDir->Text[PanelWidth - wcslen(CurrentDir->Dimension) - 4] = 0;
			wcscat(CurrentDir->Text, L"...");
		}
		MCHKHEAP;
		size_t f = wcslen(CurrentDir->Text);
		for (size_t k = 0; k < PanelWidth - f - wcslen(CurrentDir->Dimension); k++)
			wcscat(CurrentDir->Text, L" ");
		MCHKHEAP;
		wcscat(CurrentDir->Text, CurrentDir->Dimension);
		MCHKHEAP;
		CurrentDir->Graph = (wchar_t *)GlobalLock(GlobalAlloc(GHND, sizeof(wchar_t)*((size_t)CurrentDir->Persent + 1)));
		MCHKHEAP;
		for (int k = 0; k < CurrentDir->Persent; k++)
			wcscat(CurrentDir->Graph, L"▓");
		MCHKHEAP;
		CurrentDir = CurrentDir->NextDir;
		MCHKHEAP;
		if (!InData->Signal)
		{
			return 0;
		}
	}
	while (InData->hDialog == 0);
	InData->Called = 1;
	StartupInfo.AdvControl(&MainGuid, ACTL_SYNCHRO, 0, InData);
	return 0;
}

intptr_t WINAPI DialogProc1(HANDLE hDlg, intptr_t Msg, intptr_t Param1, void *Param2)
{
	static InsidePluginData *InData;
	switch (Msg)
	{
	case DN_INITDIALOG:
	{
		InData = (InsidePluginData *)Param2;
		break;
	}
	case DN_CONTROLINPUT:
	{
		INPUT_RECORD *input = static_cast<INPUT_RECORD *>(Param2);
		if (input->EventType == KEY_EVENT &&
			input->Event.KeyEvent.bKeyDown &&
			(input->Event.KeyEvent.dwControlKeyState & MODIFIER_PRESSED) == 0 &&
			input->Event.KeyEvent.uChar.UnicodeChar == L's')
		{
			InData->IgnoreSymLinks = !InData->IgnoreSymLinks;
			InData->Restart = TRUE;
			StartupInfo.SendDlgMessage(hDlg, DM_CLOSE, 0, 0);
			return TRUE;
		}
		break;
	}
	case DN_CLOSE:
	{
		if (Param1 != 555)
		{
			InData->Signal = FALSE;
			WaitForSingleObject(InData->hDialogThread, INFINITE);
			DirSize *CurrentDir = InData->FirstDir;
			while (CurrentDir)
			{
				if (CurrentDir->Graph)
				{
					GlobalUnlock(GlobalHandle(CurrentDir->Graph));
					GlobalFree(GlobalHandle(CurrentDir->Graph));
				}
				if (CurrentDir->Text)
				{
					GlobalUnlock(GlobalHandle(CurrentDir->Text));
					GlobalFree(GlobalHandle(CurrentDir->Text));
				}
				if (CurrentDir->Dir)
				{
					GlobalUnlock(GlobalHandle(CurrentDir->Dir));
					GlobalFree(GlobalHandle(CurrentDir->Dir));
				}
				InData->FirstDir = CurrentDir->NextDir;
				GlobalUnlock(GlobalHandle(CurrentDir));
				GlobalFree(GlobalHandle(CurrentDir));
				CurrentDir = InData->FirstDir;
			}
		}
		else InData->Called = 0;
		return TRUE;
	}
	}
	return StartupInfo.DefDlgProc(hDlg, Msg, Param1, Param2);
}

intptr_t WINAPI DialogProc(HANDLE hDlg, intptr_t Msg, intptr_t Param1, void *Param2)
{
	static InsidePluginData *InData;
	switch (Msg)
	{
	case DN_INITDIALOG:
	{
		InData = (InsidePluginData *)Param2;
		break;
	}
	case DN_CLOSE:
	{
		WaitForSingleObject(InData->hDialogThread, INFINITE);
		DirSize *CurrentDir = InData->FirstDir;
		while (CurrentDir != nullptr)
		{
			GlobalUnlock(GlobalHandle(CurrentDir->Graph));
			GlobalFree(GlobalHandle(CurrentDir->Graph));
			GlobalUnlock(GlobalHandle(CurrentDir->Text));
			GlobalFree(GlobalHandle(CurrentDir->Text));
			GlobalUnlock(GlobalHandle(CurrentDir->Dir));
			GlobalFree(GlobalHandle(CurrentDir->Dir));
			InData->FirstDir = CurrentDir->NextDir;
			GlobalUnlock(GlobalHandle(CurrentDir));
			GlobalUnlock(GlobalHandle(CurrentDir));
			CurrentDir = InData->FirstDir;
		}
		return TRUE;
	}
	case DN_CONTROLINPUT:
	{
		INPUT_RECORD *input = static_cast<INPUT_RECORD *>(Param2);
		if (input->EventType == KEY_EVENT &&
			input->Event.KeyEvent.bKeyDown &&
			(input->Event.KeyEvent.dwControlKeyState & MODIFIER_PRESSED) == 0 &&
			input->Event.KeyEvent.wVirtualKeyCode == 'S')
		{
			InData->Restart = TRUE;
			InData->IgnoreSymLinks = !InData->IgnoreSymLinks;
			StartupInfo.SendDlgMessage(hDlg, DM_CLOSE, 0, 0);
			return TRUE;
		}

		else if (input->EventType == KEY_EVENT &&
			input->Event.KeyEvent.bKeyDown &&
			(input->Event.KeyEvent.dwControlKeyState & CTRL_PRESSED) != 0 &&
			(input->Event.KeyEvent.dwControlKeyState & ALT_PRESSED) != 0 &&
			(input->Event.KeyEvent.dwControlKeyState & SHIFT_PRESSED) == 0 &&
			input->Event.KeyEvent.wVirtualKeyCode == VK_DOWN ||

			input->EventType == MOUSE_EVENT &&
			input->Event.MouseEvent.dwEventFlags == MOUSE_WHEELED &&
			input->Event.MouseEvent.dwButtonState >> 16 >= 0x8000)
		{
			if (InData->LastDrawElement->NextDir != nullptr)
			{
				InData->LastDrawElement = InData->LastDrawElement->NextDir;
				InData->FirstDrawElement = InData->FirstDrawElement->NextDir;
				ReSetItem(InData);
			}
			return TRUE;
		}

		else if (input->EventType == KEY_EVENT &&
			input->Event.KeyEvent.bKeyDown &&
			(input->Event.KeyEvent.dwControlKeyState & CTRL_PRESSED) != 0 &&
			(input->Event.KeyEvent.dwControlKeyState & ALT_PRESSED) != 0 &&
			(input->Event.KeyEvent.dwControlKeyState & SHIFT_PRESSED) == 0 &&
			input->Event.KeyEvent.wVirtualKeyCode == VK_UP ||

			input->EventType == MOUSE_EVENT &&
			input->Event.MouseEvent.dwEventFlags == MOUSE_WHEELED &&
			input->Event.MouseEvent.dwButtonState >> 16 < 0x8000)
		{
			if (InData->FirstDrawElement->PrevDir != nullptr)
			{
				InData->FirstDrawElement = InData->FirstDrawElement->PrevDir;
				InData->LastDrawElement = InData->LastDrawElement->PrevDir;
				ReSetItem(InData);
			}
			return TRUE;
		}

		else if (input->EventType == KEY_EVENT &&
			input->Event.KeyEvent.bKeyDown &&
			(input->Event.KeyEvent.dwControlKeyState & MODIFIER_PRESSED) == 0 &&
			input->Event.KeyEvent.wVirtualKeyCode == VK_ESCAPE)
		{
			StartupInfo.SendDlgMessage(hDlg, DM_CLOSE, 0, 0);
		}
	}
	}
	return StartupInfo.DefDlgProc(hDlg, Msg, Param1, Param2);
}

void SetDlgText(HANDLE hDlg, intptr_t index, const wchar_t *text)
{
	FarGetDialogItem gi = { sizeof(FarGetDialogItem) };
	gi.Size = StartupInfo.SendDlgMessage(hDlg, DM_GETDLGITEM, index, nullptr);
	HGLOBAL hDlgItem = GlobalAlloc(GHND, gi.Size);
	gi.Item = static_cast<FarDialogItem *>(GlobalLock(hDlgItem));
	if (gi.Item)
	{
		StartupInfo.SendDlgMessage(hDlg, DM_GETDLGITEM, index, &gi);
		FarDialogItem di = *gi.Item;
		di.Data = text;
		di.X2 = wcslen(text) + 1;
		StartupInfo.SendDlgMessage(hDlg, DM_SETDLGITEM, index, &di);
	}
	GlobalUnlock(hDlgItem);
	GlobalFree(hDlgItem);
}

void ReSetItem(InsidePluginData *InData)
{
	StartupInfo.SendDlgMessage(InData->hDialog, DM_ENABLEREDRAW, FALSE, 0);
	DirSize *CurrentDir = InData->FirstDrawElement;
	for (int n = 2; n < InData->CountItem * 2 + 1; n += 2)
	{
		SetDlgText(InData->hDialog, n, CurrentDir->Text);
		SetDlgText(InData->hDialog, n + 1, CurrentDir->Graph);
		CurrentDir = CurrentDir->NextDir;
	}
	StartupInfo.SendDlgMessage(InData->hDialog, DM_ENABLEREDRAW, TRUE, 0);
}

extern "C"
{
	BOOL WINAPI DllMainCRTStartup(HANDLE, DWORD, PVOID)
	{
		return true;
	}
}
