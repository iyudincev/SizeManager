#include <windows.h>
#include <winuser.h>
#include <wincon.h>
#define _FAR_NO_NAMELESS_UNIONS
#include "plugin.hpp"
#include "farkeys.hpp"
#include "farcolor.hpp"
#include "crtdbg.h"
#define MCHKHEAP //_ASSERT(HeapValidate(GetProcessHeap(),0,NULL))

//GUID_1 = 5431982e-24ca-4bac-8831-177300c2405c
//GUID_2 = e41f6eff-49da-40d8-bb50-37d355d812cc

static struct PluginStartupInfo StartupInfo;

struct DirSize
{
    DWORDLONG Size;
    BYTE Persent;
    WCHAR *Dir;
    WCHAR *Text;
    WCHAR *Graph;
    DirSize *NextDir;
    DirSize *PrevDir;
    WCHAR Dimension[20];
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
	BOOL Complete;
	BOOL IgnoreSymLinks;
	BOOL DoubleCall;
	BOOL Restart;
	int Called;
	int SizeofCurDir;
	WCHAR *PanelCurDir;
	PanelInfo PInfo;
};

DWORDLONG CalcSizeRecursive(WCHAR *Dir);
DWORD WINAPI DDialogThread(void *Data);
DWORD WINAPI ResetThread(void *Data);
LONG_PTR WINAPI DialogProc(HANDLE hDlg, int Msg, int Param1, LONG_PTR Param2);
LONG_PTR WINAPI DialogProc1(HANDLE hDlg, int Msg, int Param1, LONG_PTR Param2);
void ReSetItem(InsidePluginData *InData);

HANDLE WINAPI _export OpenPluginW(int OpenFrom, INT_PTR Item)
{
	InsidePluginData *InData = NULL;
	DirSize *CurrentDir = NULL;
	InData = (InsidePluginData *) GlobalLock(GlobalAlloc(GHND, sizeof(InsidePluginData)));
	do
	{
		InData->Called = 0;
		InData->Signal = TRUE;
		InData->Network = TRUE;
		InData->Restart = FALSE;
		InData->Complete = TRUE;
		DWORD d;
		InData->SizeofCurDir = StartupInfo.Control(PANEL_ACTIVE, FCTL_GETPANELDIR, 0, 0);
		InData->SizeofCurDir += 8;
		InData->PanelCurDir = (WCHAR *) GlobalLock(GlobalAlloc(GHND, InData->SizeofCurDir*sizeof(WCHAR)));
		StartupInfo.Control(PANEL_ACTIVE, FCTL_GETPANELDIR, InData->SizeofCurDir, (LONG_PTR) InData->PanelCurDir);
		if (InData->PanelCurDir[0] != '\\')
		{
			InData->Network = FALSE;
			wcscpy(InData->PanelCurDir, L"\\\\?\\");
			StartupInfo.Control(PANEL_ACTIVE, FCTL_GETPANELDIR, InData->SizeofCurDir, (LONG_PTR) &InData->PanelCurDir[4]);
		}
		WCHAR *z = (WCHAR *) L"\\";
		if (wcscmp(&InData->PanelCurDir[wcslen(InData->PanelCurDir)-1], z) == 0) wcscat(InData->PanelCurDir, L"*");
		else wcscat(InData->PanelCurDir, L"\\*");

		StartupInfo.Control(PANEL_PASSIVE, FCTL_GETPANELINFO, 0, (LONG_PTR) &InData->PInfo);
		InData->hDialogThread = CreateThread(0, 0, DDialogThread, InData, 0, &d);
		FarDialogItem DItem[3];
		memset(DItem, 0, sizeof(DItem));
		DItem[0].Type = DI_DOUBLEBOX;
		DItem[0].X1 = 0;
		DItem[0].Y1 = 0;
		DItem[0].X2 = 32;
		DItem[0].Y2 = 2;
		DItem[0].Focus = TRUE;
		DItem[0].PtrData = L"Size Manager";

		DItem[1].Type = DI_TEXT;
		DItem[1].X1 = 2;
		DItem[1].Y1 = 1;
		DItem[1].X2 = 15;
		DItem[1].Y2 = 1;
		DItem[1].PtrData = L"Идет расчет...";

		DItem[2].Type = DI_TEXT;
		DItem[2].X1 = 2;
		DItem[2].Y1 = 2;
		DItem[2].X2 = 30;
		DItem[2].Y2 = 2;
		if (!InData->IgnoreSymLinks) DItem[2].PtrData = L"Нажмите S для пропуска ссылок";
		else { DItem[2].PtrData = L"Нажмите S для расчета ссылок"; DItem[2].X2 = 29;}

		InData->hDialog = StartupInfo.DialogInit(StartupInfo.ModuleNumber, -1, -1, 33, 3, 0, &DItem[0],
										3, 0, FDLG_SMALLDIALOG, DialogProc1, (LONG_PTR) InData);

		StartupInfo.DialogRun(InData->hDialog);
		StartupInfo.DialogFree(InData->hDialog);
		GlobalUnlock(GlobalHandle(InData->PanelCurDir));
		GlobalFree(GlobalHandle(InData->PanelCurDir));
		if (InData->Signal && !InData->Restart)
		{
			InData->hDialog = 0;
			int PanelHeight = InData->PInfo.PanelRect.bottom - InData->PInfo.PanelRect.top;
			int PanelWidth = InData->PInfo.PanelRect.right - InData->PInfo.PanelRect.left - 4;
			int Count = (PanelHeight-2)/2;
			int i = 0;
			InData->FirstDrawElement = InData->FirstDir;
			CurrentDir = InData->FirstDir;
			while (CurrentDir != NULL)
			{
				i++;
				if (i == Count) break;
				if (CurrentDir->NextDir == NULL) break;
				else CurrentDir = CurrentDir->NextDir;
			}
			InData->CountItem = i;
			InData->LastDrawElement = CurrentDir;

			HGLOBAL hDlgItem = GlobalAlloc(GHND, sizeof(FarDialogItem)*((InData->CountItem*2)+2));
			FarDialogItem *DlgItems = (FarDialogItem *) GlobalLock(hDlgItem);

			CurrentDir = InData->FirstDrawElement;
			for (i = 2; i < (InData->CountItem*2)+1; i = i+2)
			{
				DlgItems[i].Type = DI_TEXT;
				DlgItems[i].X1 = 2;
				DlgItems[i].Y1 = i-1;
				DlgItems[i].X2 = wcslen(CurrentDir->Text)+1;
				DlgItems[i].Y2 = i-1;
				DlgItems[i].Focus = FALSE;
				DlgItems[i].PtrData = CurrentDir->Text;

				DlgItems[i+1].Type = DI_TEXT;
				DlgItems[i+1].X1 = 2;
				DlgItems[i+1].Y1 = i;
				DlgItems[i+1].X2 = wcslen(CurrentDir->Graph)+1;
				DlgItems[i+1].Y2 = i;
				DlgItems[i+1].Focus = FALSE;
				DlgItems[i+1].PtrData = CurrentDir->Graph;

				CurrentDir = CurrentDir->NextDir;
			}

			DlgItems[0].Type = DI_DOUBLEBOX;
			DlgItems[0].X1 = 0;
			DlgItems[0].X2 = PanelWidth+4;
			DlgItems[0].Y1 = 0;
			DlgItems[0].Y2 = PanelHeight;
			DlgItems[0].Focus = TRUE;
			DlgItems[0].PtrData = L"Size Manager";

			DlgItems[1].Type = DI_TEXT;
			DlgItems[1].X1 = 2;
			DlgItems[1].Y1 = PanelHeight;
			DlgItems[1].X2 = 37;
			DlgItems[1].Y2 = PanelHeight;
			if (!InData->IgnoreSymLinks) DlgItems[1].PtrData = L"Нажмите S для перерасчета без ссылок";
			else DlgItems[1].PtrData = L"Нажмите S для перерасчета c ссылками";
			InData->hDialog = StartupInfo.DialogInit(StartupInfo.ModuleNumber, InData->PInfo.PanelRect.left, InData->PInfo.PanelRect.top,
											InData->PInfo.PanelRect.right, InData->PInfo.PanelRect.bottom, NULL, DlgItems, (InData->CountItem*2)+2, 0,
											FDLG_NODRAWSHADOW|FDLG_SMALLDIALOG, DialogProc, (LONG_PTR) InData);
			StartupInfo.DialogRun(InData->hDialog);
			StartupInfo.DialogFree(InData->hDialog);
			GlobalUnlock(hDlgItem);
			GlobalFree(hDlgItem);
		}
	}
	while (InData->Restart);
	GlobalUnlock(GlobalHandle(InData));
	GlobalFree(GlobalHandle(InData));
	InData = NULL;
    return (HANDLE) INVALID_HANDLE_VALUE;
}

void WINAPI _export GetPluginInfoW(struct PluginInfo *Info)
{
    static WCHAR *DiskString[1];
    DiskString[0] = (WCHAR *) L"Size Manager";
    Info->StructSize = sizeof(struct PluginInfo);
    Info->DiskMenuStrings = DiskString;
    Info->DiskMenuStringsNumber = 1;
    Info->PluginMenuStrings = DiskString;
    Info->PluginMenuStringsNumber = 1;
}

void WINAPI _export SetStartupInfoW(const struct PluginStartupInfo *StartInfo)
{
    StartupInfo = *StartInfo;
}

int WINAPI _export ProcessSynchroEventW(int Event, void *Param)
{
	if (Event == SE_COMMONSYNCHRO)
	{
		InsidePluginData *InData = (InsidePluginData *) Param;
		if (InData->Called == 1) StartupInfo.SendDlgMessage(InData->hDialog, DM_CLOSE, 555, 0);
	}
	return 0;
}


DWORDLONG CalcSizeRecursive(WCHAR *Dir, InsidePluginData *InData)
{
	WIN32_FIND_DATAW Data;
	DWORDLONG Size = 0;
	int StrLen = wcslen(Dir);
	HGLOBAL hDir = GlobalAlloc(GHND, (StrLen+3)*2);
	WCHAR *tDir = (WCHAR *) GlobalLock(hDir);
	wcscpy(tDir, Dir);
	wcscat(tDir, L"\\*");
	if (!InData->Signal){ GlobalUnlock(hDir); GlobalFree(hDir); return 0;}
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
				HGLOBAL hTmpDir = GlobalAlloc(GHND, (wcslen(Data.cFileName)+wcslen(Dir)+2)*2);
				WCHAR *TmpDir = (WCHAR *) GlobalLock(hTmpDir);
				wcscpy(TmpDir, Dir);
				wcscat(TmpDir, L"\\");
				wcscat(TmpDir, Data.cFileName);
				Size += CalcSizeRecursive(TmpDir, InData);
				GlobalUnlock(hTmpDir);
				GlobalFree(hTmpDir);
			}
		}
		else Size += ((DWORDLONG)Data.nFileSizeHigh<<32) + Data.nFileSizeLow;
		if (!InData->Signal){ FindClose(File); GlobalUnlock(hDir); GlobalFree(hDir); return 0; }
	} while(FindNextFileW(File, &Data));
	GlobalUnlock(hDir);
	GlobalFree(hDir);
	FindClose(File);
	return Size;
}
DWORD WINAPI DDialogThread(void *lpData)
{
	InsidePluginData *InData = (InsidePluginData *) lpData;
	WIN32_FIND_DATAW Data;
	HANDLE File;
	DirSize *CurrentDir = NULL;
	InData->FirstDir = NULL;
	DirSize *InFiles = NULL;
	InData->Complete = FALSE;
	File = FindFirstFileW(InData->PanelCurDir, &Data);
	if (File == INVALID_HANDLE_VALUE)
	{
		InData->Complete = TRUE;
		InData->Called = 1;
		StartupInfo.AdvControl(StartupInfo.ModuleNumber, ACTL_SYNCHRO, InData);
		return 0;
	}
	do
	{
		if (Data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)
		{
			if (wcscmp(Data.cFileName, L".") != 0 && wcscmp(Data.cFileName, L"..") != 0)
			{
				if (InData->IgnoreSymLinks)
					if (Data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
						if (Data.dwReserved0&IO_REPARSE_TAG_SYMLINK) continue;
				if (InData->FirstDir == NULL)
				{
					InData->FirstDir = (DirSize *) GlobalLock(GlobalAlloc(GHND, sizeof(DirSize)));
					CurrentDir = InData->FirstDir;
				}
				else
				{
					DirSize *TmpDir;
					TmpDir = (DirSize *) GlobalLock(GlobalAlloc(GHND, sizeof(DirSize)));
					TmpDir->PrevDir = CurrentDir;
					CurrentDir->NextDir = TmpDir;
					CurrentDir = TmpDir;
				}
				CurrentDir->Dir = (WCHAR *) GlobalLock(GlobalAlloc(GHND, (wcslen(Data.cFileName)+1)*2));
				wcscpy(CurrentDir->Dir, Data.cFileName);

				HGLOBAL hNDir = GlobalAlloc(GHND, (wcslen(CurrentDir->Dir)+(InData->SizeofCurDir-4)+1)*2);
				WCHAR *wTmpDir = (WCHAR *) GlobalLock(hNDir);
				wcscpy(wTmpDir, InData->PanelCurDir);
				if (!InData->Network) wTmpDir[InData->SizeofCurDir-5] = 0;
				else wTmpDir[InData->SizeofCurDir-8] = 0;
				WCHAR *z = (WCHAR *) L"\\";
				if (wcscmp(&wTmpDir[wcslen(wTmpDir)-1], z) != 0) wcscat(wTmpDir, L"\\");
				wcscat(wTmpDir, CurrentDir->Dir);
				CurrentDir->Size = CalcSizeRecursive(wTmpDir, InData);
				GlobalUnlock(hNDir);
				GlobalFree(hNDir);
			}
		}
		else
		{
			if (InFiles == NULL)
			{
				InFiles = (DirSize *) GlobalLock(GlobalAlloc(GHND, sizeof(DirSize)));
				InFiles->Dir = (WCHAR *) GlobalLock(GlobalAlloc(GHND, (wcslen(L"В файлах")+1)*2));
				wcscpy(InFiles->Dir, L"В файлах");
				InFiles->NextDir = NULL;
			}
			InFiles->Size += ((DWORDLONG)Data.nFileSizeHigh<<32) + Data.nFileSizeLow;
		}
		if (!InData->Signal){ if (InFiles) GlobalUnlock(GlobalHandle(InFiles)); GlobalFree(GlobalHandle(InFiles)); InData->Complete = TRUE; FindClose(File); /* SetEvent(InData->hEvent);*/ return 0; }
	}
	while (FindNextFileW(File, &Data));
	FindClose(File);
	if (CurrentDir != NULL) CurrentDir->NextDir = InFiles;
	if (InFiles != NULL) InFiles->PrevDir = CurrentDir;

	if (InData->FirstDir == NULL)
	{
        if (InFiles == NULL) return 0;
        else InData->FirstDir = InFiles;
	}
	CurrentDir = InData->FirstDir;
	while (CurrentDir->NextDir != NULL)
	{
		DirSize *TmpDir = CurrentDir->NextDir;
		while (TmpDir != NULL)
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
		if (!InData->Signal){ InData->Complete = TRUE; /*SetEvent(InData->hEvent);*/ return 0; }
	}

	CurrentDir = InData->FirstDir;
	DWORDLONG TByte = 1099511627776ULL;
    DWORDLONG PByte = 1125899906842624ULL;
	while (CurrentDir != NULL)
	{
	 	DWORDLONG BufSize = CurrentDir->Size;
        DWORDLONG n = 2, b;
        int i = 0;
        while (n > 0)
        {
            n = BufSize>>10;
            if (n > 0)
            {
                BufSize = n;
                if (i < 5) i++;
            }
        }
         WCHAR Drob[3];
        _i64tow(BufSize, CurrentDir->Dimension, 10);
		MCHKHEAP;
        switch (i)
        {
            case 0 : { wcscat(CurrentDir->Dimension, L" Байт"); break;}
            case 1 : {
                b = CurrentDir->Size - (BufSize<<10);
                if (b > 0) n = (b * 10)/1024;
                if (n > 1){ _i64tow(n, Drob, 10);
				MCHKHEAP;
                wcscat(CurrentDir->Dimension, L",");
                wcscat(CurrentDir->Dimension, Drob);}
                wcscat(CurrentDir->Dimension, L" КБ"); break;}
            case 2 : {
                b = CurrentDir->Size - (BufSize<<20);
                if (b > 0) n = (b * 10)/1048576;
                if (n > 1){ _i64tow(n, Drob, 10);
				MCHKHEAP;
                wcscat(CurrentDir->Dimension, L",");
                wcscat(CurrentDir->Dimension, Drob);}
                wcscat(CurrentDir->Dimension, L" МБ"); break;}
            case 3 : {
                b = CurrentDir->Size - (BufSize<<30);
                if (b > 0) n = (b * 10)/1073741824;
                if (n > 1){ _i64tow(n, Drob, 10);
				MCHKHEAP;
                wcscat(CurrentDir->Dimension, L",");
                wcscat(CurrentDir->Dimension, Drob);}
                wcscat(CurrentDir->Dimension, L" ГБ"); break;}
            case 4 : {
                b = CurrentDir->Size - (BufSize<<40);
                if (b > 0) n = (b * 10)/TByte;
                if (n > 1){ _i64tow(n, Drob, 10);
				MCHKHEAP;
                wcscat(CurrentDir->Dimension, L",");
                wcscat(CurrentDir->Dimension, Drob);}
                wcscat(CurrentDir->Dimension, L" ТБ"); break;}
            case 5 : {
                b = CurrentDir->Size - (BufSize<<50);
                if (b > 0) n = (b * 10)/PByte;
                if (n > 1){ _i64tow(n, Drob, 10);
				MCHKHEAP;
                wcscat(CurrentDir->Dimension, L",");
                wcscat(CurrentDir->Dimension, Drob);}
                wcscat(CurrentDir->Dimension, L" ПБ"); break;}
        }
        CurrentDir = CurrentDir->NextDir;
		if (!InData->Signal){ InData->Complete = TRUE; /*SetEvent(InData->hEvent);*/ return 0; }
	}

	unsigned int PanelWidth = InData->PInfo.PanelRect.right - InData->PInfo.PanelRect.left - 4;
	CurrentDir = InData->FirstDir;
	while(CurrentDir != NULL && InData->Signal)
	{
		CurrentDir->Persent = (CurrentDir->Size * PanelWidth) / InData->FirstDir->Size;
		CurrentDir = CurrentDir->NextDir;
		if (!InData->Signal){ InData->Complete = TRUE;/* SetEvent(InData->hEvent);*/ return 0; }
	}
	CurrentDir = InData->FirstDir;
	while (CurrentDir != NULL && InData->Signal)
	{
		CurrentDir->Text = (WCHAR *) GlobalLock(GlobalAlloc(GHND, sizeof(WCHAR)*(PanelWidth+1)));
		WCHAR *space = (WCHAR *) L" ";
		MCHKHEAP;
		if (wcslen(CurrentDir->Dir) > PanelWidth)
			for (unsigned int l = 0; l < PanelWidth-wcslen(CurrentDir->Dimension)-1; l++)
				CurrentDir->Text[l] = CurrentDir->Dir[l];
		else wcscpy(CurrentDir->Text, CurrentDir->Dir);
		MCHKHEAP;
		if (wcslen(CurrentDir->Dir) > PanelWidth-wcslen(CurrentDir->Dimension)-1)
		{
			CurrentDir->Text[PanelWidth-wcslen(CurrentDir->Dimension)-4] = 0;
			wcscat(CurrentDir->Text, L"...");
		}
		MCHKHEAP;
		int f = wcslen(CurrentDir->Text);
		for (unsigned int k = 0; k < PanelWidth-f-wcslen(CurrentDir->Dimension); k++)
            wcscat(CurrentDir->Text, space);
		MCHKHEAP;
		wcscat(CurrentDir->Text, CurrentDir->Dimension);
		MCHKHEAP;
		CurrentDir->Graph = (WCHAR *) GlobalLock(GlobalAlloc(GHND, sizeof(WCHAR)*(CurrentDir->Persent+1)));
        WCHAR *z = (WCHAR *) L"▓";
		MCHKHEAP;
		for (int k = 0; k < CurrentDir->Persent; k++)
            wcscat(CurrentDir->Graph, z);
		MCHKHEAP;
		CurrentDir = CurrentDir->NextDir;
		MCHKHEAP;
		if (!InData->Signal){ InData->Complete = TRUE;  return 0; }
	}
	InData->Complete = TRUE;
	while(InData->hDialog == 0);
	InData->Called = 1;
	StartupInfo.AdvControl(StartupInfo.ModuleNumber, ACTL_SYNCHRO, InData);
	return 0;
}

LONG_PTR WINAPI DialogProc1(HANDLE hDlg, int Msg, int Param1, LONG_PTR Param2)
{
	static InsidePluginData *InData;
	switch (Msg)
	{
		case DN_INITDIALOG:
		{
			InData = (InsidePluginData *) Param2;
			break;
		}
		case DN_GETDIALOGINFO:
		{
			static const GUID DlgGUID = {0x5431982e, 0x24ca, 0x4bac, {0x88, 0x31, 0x17, 0x73, 0x00, 0xc2, 0x40, 0x5c}};
			if (((DialogInfo*)(Param2))->StructSize != sizeof(DialogInfo))
				return FALSE;
		  	((DialogInfo*)(Param2))->Id = DlgGUID;
		  	return TRUE;
		}
		case DN_KEY:
		{
			if (Param2 == 115)
			{
				InData->IgnoreSymLinks = !InData->IgnoreSymLinks;
				InData->Restart = TRUE;
				StartupInfo.SendDlgMessage(hDlg, DM_CLOSE, 0, 0);
				return TRUE;
			}
			break;
		}
		case DM_CLOSE:
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

LONG_PTR WINAPI DialogProc(HANDLE hDlg, int Msg, int Param1, LONG_PTR Param2)
{
	static InsidePluginData *InData;
	switch (Msg)
	{
		case DN_INITDIALOG:
		{
			InData = (InsidePluginData *) Param2;
			break;
		}
		case DN_GETDIALOGINFO:
		{
			static const GUID DlgGUID = {0xe41f6eff, 0x49da, 0x40d8, {0xbb, 0x50, 0x37, 0xd3, 0x55, 0xd8, 0x12, 0xcc}};
			if (((DialogInfo*)(Param2))->StructSize != sizeof(DialogInfo))
				return FALSE;
		  	((DialogInfo*)(Param2))->Id = DlgGUID;
		  	return TRUE;
		}
		case DN_CLOSE:
		{
			WaitForSingleObject(InData->hDialogThread, INFINITE);
			DirSize *CurrentDir = InData->FirstDir;
			while (CurrentDir != NULL)
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
		case DN_KEY :
		{
			switch (Param2)
			{
				case 115 :
				{
					InData->Restart = TRUE;
					InData->IgnoreSymLinks = !InData->IgnoreSymLinks;
					StartupInfo.SendDlgMessage(hDlg, DM_CLOSE, 0, 0);
					return TRUE;
				}
				case KEY_CTRLALTDOWN:
				case KEY_MSWHEEL_DOWN :
				{
					if (InData->LastDrawElement->NextDir != NULL)
					{
						InData->LastDrawElement = InData->LastDrawElement->NextDir;
						InData->FirstDrawElement = InData->FirstDrawElement->NextDir;
						ReSetItem(InData);
					}
					return TRUE;
				}
				case KEY_CTRLALTUP:
				case KEY_MSWHEEL_UP :
				{
					if (InData->FirstDrawElement->PrevDir != NULL)
					{
						InData->FirstDrawElement = InData->FirstDrawElement->PrevDir;
						InData->LastDrawElement = InData->LastDrawElement->PrevDir;
						ReSetItem(InData);
					}
					return TRUE;
				}
				case KEY_ESC:
				{
					StartupInfo.SendDlgMessage(hDlg, DM_CLOSE, 0, 0);
					break;
				}
			}
			break;
		}
	}
	return StartupInfo.DefDlgProc(hDlg, Msg, Param1, Param2);
}

void ReSetItem(InsidePluginData *InData)
{
	FarDialogItem *DlgItem;
	StartupInfo.SendDlgMessage(InData->hDialog,DM_ENABLEREDRAW,FALSE,0);
	DirSize *CurrentDir = InData->FirstDrawElement;
	for (int n = 2; n < InData->CountItem*2+1; n = n + 2)
	{
		HGLOBAL hDlgItem = GlobalAlloc(GHND, StartupInfo.SendDlgMessage(InData->hDialog, DM_GETDLGITEM, n, NULL));
		DlgItem = (FarDialogItem *) GlobalLock(hDlgItem);
		StartupInfo.SendDlgMessage(InData->hDialog, DM_GETDLGITEM, n, (LONG_PTR) DlgItem);
		DlgItem->PtrData = CurrentDir->Text;
		DlgItem->X2 = wcslen(CurrentDir->Text)+1;
		StartupInfo.SendDlgMessage(InData->hDialog, DM_SETDLGITEM, n, (LONG_PTR) DlgItem);
		GlobalUnlock(hDlgItem);
		GlobalFree(hDlgItem);
		hDlgItem = GlobalAlloc(GHND, StartupInfo.SendDlgMessage(InData->hDialog, DM_GETDLGITEM, n+1, NULL));
		DlgItem = (FarDialogItem *) GlobalLock(hDlgItem);
		StartupInfo.SendDlgMessage(InData->hDialog, DM_GETDLGITEM, n+1, (LONG_PTR) DlgItem);
		DlgItem->PtrData = CurrentDir->Graph;
		DlgItem->X2 = wcslen(CurrentDir->Graph)+1;
		StartupInfo.SendDlgMessage(InData->hDialog, DM_SETDLGITEM, n+1, (LONG_PTR) DlgItem);
		GlobalUnlock(hDlgItem);
		GlobalFree(hDlgItem);

		CurrentDir = CurrentDir->NextDir;
	}
	StartupInfo.SendDlgMessage(InData->hDialog,DM_ENABLEREDRAW,TRUE,0);
}

extern "C"
{
BOOL WINAPI DllMainCRTStartup(HANDLE, DWORD, PVOID)
{
    return true;
}
}
