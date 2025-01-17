// txt2qr --- QR Code maker
// Copyright (C) 2020 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
// All Rights Reserved.
#define NOMINMAX
#define STRICT
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <process.h>

#include <string>
#include <vector>
#include <algorithm>

#include <cstdlib>
#include <csetjmp>
#include <cassert>

#include "MSmoothLayout.hpp"
#include "QR_Read.hpp"
#include "resource.h"

#define MAX_TEXT 500
#define TIMER_ID 999

static HINSTANCE s_hInstance = NULL;
static HICON s_hIcon = NULL;
static HICON s_hIconSmall = NULL;
static WCHAR s_szTempFile[MAX_PATH] = L"";
static HBITMAP s_hbm1 = NULL;
static HBITMAP s_hbm2 = NULL;
static BOOL s_bInProcessing = FALSE;
static BOOL s_bUpdatedInProcessing = FALSE;
static WNDPROC s_fnEditWndProc = NULL;
static MSmoothLayout s_layout;
static INT s_cxMinTrack;
static INT s_cyMinTrack;

LPWSTR LoadStringDx(INT nID)
{
    static UINT s_index = 0;
    const UINT cchBuffMax = 1024;
    static WCHAR s_sz[4][cchBuffMax];

    WCHAR *pszBuff = s_sz[s_index];
    s_index = (s_index + 1) % ARRAYSIZE(s_sz);
    pszBuff[0] = 0;
    if (!::LoadStringW(NULL, nID, pszBuff, cchBuffMax))
        assert(0);
    return pszBuff;
}

LPWSTR MakeFilterDx(LPWSTR psz)
{
    for (LPWSTR pch = psz; *pch; ++pch)
    {
        if (*pch == L'|')
            *pch = 0;
    }
    return psz;
}

void DoTrimRight(std::wstring& str, const WCHAR *spaces)
{
    size_t j = str.find_last_not_of(spaces);
    if (j == std::wstring::npos)
    {
        str.clear();
    }
    else
    {
        str = str.substr(0, j + 1);
    }
}

template <INT siz>
void DoTrimRight(WCHAR (&str)[siz], const WCHAR *spaces)
{
    std::wstring s = str;
    DoTrimRight(s, spaces);
    lstrcpynW(str, s.c_str(), siz);
}

template <typename T_STR>
bool
DoReplaceAll(T_STR& str, const T_STR& from, const T_STR& to)
{
    bool ret = false;
    size_t i = 0;
    for (;;) {
        i = str.find(from, i);
        if (i == T_STR::npos)
            break;
        ret = true;
        str.replace(i, from.size(), to);
        i += to.size();
    }
    return ret;
}
template <typename T_STR>
bool
DoReplaceAll(T_STR& str,
             const typename T_STR::value_type *from,
             const typename T_STR::value_type *to)
{
    return DoReplaceAll(str, T_STR(from), T_STR(to));
}

BOOL DoGetTempPathName(LPWSTR pszPath)
{
    WCHAR szTempPath[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, szTempPath))
        return FALSE;

    WCHAR szFileName[MAX_PATH];
    DWORD n = rand() ^ GetTickCount();
    wsprintfW(szFileName, L"tmp-txt2qr-%lX.png", n);

    lstrcpynW(pszPath, szTempPath, MAX_PATH);
    PathAppendW(pszPath, szFileName);

    return TRUE;
}

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

HBITMAP DoLoadPngAsBitmap(LPCWSTR pszFileName)
{
    Gdiplus::GdiplusStartupInput gdiPlusStartupInput;
    ULONG_PTR gdiPlusToken;
    Gdiplus::GdiplusStartup(&gdiPlusToken, &gdiPlusStartupInput, nullptr);

    Gdiplus::Bitmap* bitmap = Gdiplus::Bitmap::FromFile(pszFileName, FALSE);
    HBITMAP hBitmap = nullptr;
    
    if (bitmap)
    {
        bitmap->GetHBITMAP(Gdiplus::Color(0, 0, 0), &hBitmap);
        delete bitmap;
    }

    Gdiplus::GdiplusShutdown(gdiPlusToken);
    return hBitmap;
}

BOOL
DoExecuteQrEncode(HWND hwnd, LPCWSTR pszText, LPCWSTR pszOutFile,
                  INT nMode, INT nQuality)
{
    WCHAR szPath[MAX_PATH];
    WCHAR szParams[512];

    GetModuleFileNameW(NULL, szPath, ARRAYSIZE(szPath));
    PathRemoveFileSpecW(szPath);
    PathAppendW(szPath, L"qrencode.exe");

    szParams[0] = 0;
    lstrcatW(szParams, L"-o \"");
    lstrcatW(szParams, pszOutFile);
    lstrcatW(szParams, L"\"");

    switch (nQuality)
    {
    case 0:
        lstrcatW(szParams, L" -l L");
        break;
    case 1:
        lstrcatW(szParams, L" -l M");
        break;
    case 2:
        lstrcatW(szParams, L" -l Q");
        break;
    case 3:
        lstrcatW(szParams, L" -l H");
        break;
    }

    switch (nMode)
    {
    case 0:
        break;
    case 1:
        lstrcatW(szParams, L" --ignorecase");
        break;
    case 2:
        lstrcatW(szParams, L" --kanji");
        break;
    case 3:
        lstrcatW(szParams, L" --8bit");
        break;
    }

    std::wstring text = pszText;
    DoReplaceAll(text, L"\r", L"");
    DoReplaceAll(text, L"\"", L"\"\"");

    lstrcatW(szParams, L" \"");
    lstrcatW(szParams, text.c_str());
    lstrcatW(szParams, L"\"");

    SHELLEXECUTEINFOW info = { sizeof(info) };
    info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOCLOSEPROCESS;
    info.hwnd = hwnd;
    info.lpFile = szPath;
    info.lpParameters = szParams;
    info.nShow = SW_HIDE;
    if (ShellExecuteExW(&info))
    {
        WaitForSingleObject(info.hProcess, INFINITE);
        CloseHandle(info.hProcess);
        return TRUE;
    }
    return FALSE;
}

BOOL DoGetDIBFromBitmap(std::vector<BYTE>& dib, HBITMAP hbm)
{
    BITMAP bm;
    BITMAPINFOEX bi;
    BITMAPINFOHEADER *pbmih;
    DWORD cColors, cbColors;

    if (!GetObject(hbm, sizeof(BITMAP), &bm))
        return FALSE;

    pbmih = &bi.bmiHeader;
    ZeroMemory(pbmih, sizeof(BITMAPINFOHEADER));
    pbmih->biSize             = sizeof(BITMAPINFOHEADER);
    pbmih->biWidth            = bm.bmWidth;
    pbmih->biHeight           = bm.bmHeight;
    pbmih->biPlanes           = 1;
    pbmih->biBitCount         = bm.bmBitsPixel;
    pbmih->biCompression      = BI_RGB;
    pbmih->biSizeImage        = bm.bmWidthBytes * bm.bmHeight;

    if (bm.bmBitsPixel < 16)
        cColors = 1 << bm.bmBitsPixel;
    else
        cColors = 0;
    cbColors = cColors * sizeof(RGBQUAD);

    BYTE *pBits = new BYTE[pbmih->biSizeImage];
    if (pBits == NULL)
        return FALSE;

    BOOL bOK = FALSE;
    if (HDC hDC = CreateCompatibleDC(NULL))
    {
        SIZE_T ib = 0, cb = 0;

        if (GetDIBits(hDC, hbm, 0, bm.bmHeight, pBits, (BITMAPINFO*)&bi,
                      DIB_RGB_COLORS))
        {
            cb += sizeof(BITMAPINFOHEADER);
            dib.resize(cb);
            CopyMemory(&dib[ib], &bi, sizeof(BITMAPINFOHEADER));
            ib = cb;

            cb += cbColors;
            dib.resize(cb);
            CopyMemory(&dib[ib], bi.bmiColors, cbColors);
            ib = cb;

            cb += pbmih->biSizeImage;
            dib.resize(cb);
            CopyMemory(&dib[ib], pBits, pbmih->biSizeImage);
            ib = cb;

            bOK = TRUE;
        }

        DeleteDC(hDC);
    }

    delete[] pBits;

    return bOK;
}

void DoShowPlaceHolder(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    DWORD dw = (DWORD)SendMessage(hwnd, EM_GETMARGINS, 0, 0);
    WORD wLeftMargin = LOWORD(dw);

    HFONT hFont = GetWindowFont(hwnd);

    if (HDC hdc = GetDC(hwnd))
    {
        HGDIOBJ hFontOld = SelectObject(hdc, hFont);
        COLORREF clrTextOld = SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
        INT nBkModeOld = SetBkMode(hdc, TRANSPARENT);
        LPCWSTR pszText = LoadStringDx(IDS_INPUTTEXT);
        TextOutW(hdc, wLeftMargin, rc.top, pszText, lstrlenW(pszText));
        SetBkMode(hdc, nBkModeOld);
        SetTextColor(hdc, clrTextOld);
        SelectObject(hdc, hFontOld);
        ReleaseDC(hwnd, hdc);
    }
}

void DoSwapEndian(LPBYTE pb, DWORD cb)
{
    while (cb >= 2)
    {
        BYTE b = *pb;
        *pb = pb[1];
        pb[1] = b;

        pb += 2;
        cb -= 2;
    }
}

HBITMAP StretchImage(HBITMAP hbm, INT cx, INT cy)
{
    HBITMAP ret = NULL;
    if (!hbm)
        return ret;

    if (HDC hdc1 = CreateCompatibleDC(NULL))
    {
        BITMAPINFO bi;
        ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = cx;
        bi.bmiHeader.biHeight = cy;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 24;

        LPVOID pvBits;
        ret = CreateDIBSection(hdc1, &bi, DIB_RGB_COLORS, &pvBits, NULL, 0);
        if (ret)
        {
            HGDIOBJ hbm1Old = SelectObject(hdc1, ret);
            if (HDC hdc2 = CreateCompatibleDC(NULL))
            {
                BITMAP bm;
                GetObject(hbm, sizeof(bm), &bm);
                HGDIOBJ hbm2Old = SelectObject(hdc2, hbm);

                SetStretchBltMode(hdc1, STRETCH_DELETESCANS);
                StretchBlt(hdc1, 0, 0, cx, cy, hdc2, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);

                SelectObject(hdc2, hbm2Old);
                DeleteDC(hdc2);
            }
            SelectObject(hdc1, hbm1Old);
        }
        DeleteDC(hdc1);
    }
    return ret;
}

std::wstring DoReadBinaryText(HWND hwnd, LPVOID pv, DWORD cb)
{
    WCHAR szText[MAX_TEXT + 1] = { 0 };

    // UTF-8 with BOM
    if (memcmp(pv, "\xEF\xBB\xBF", 3) == 0 && cb >= 3)
    {
        BYTE *pb = (BYTE *)pv;
        pb += 3;
        cb -= 3;
        if (MultiByteToWideChar(CP_UTF8, 0, (LPSTR)pb, cb,
                                szText, ARRAYSIZE(szText)))
        {
            return szText;
        }
    }

    // UTF-16LE with BOM
    if (memcmp(pv, "\xFF\xFE", 2) == 0 && cb >= 2)
    {
        BYTE *pb = (BYTE *)pv;
        pb += 2;
        cb -= 2;
        return (LPWSTR)pb;
    }

    // UTF-16BE with BOM
    if (memcmp(pv, "\xFE\xFF", 2) == 0 && cb >= 2)
    {
        BYTE *pb = (BYTE *)pv;
        pb += 2;
        cb -= 2;
        DoSwapEndian(pb, cb);
        return (LPWSTR)pb;
    }

    // UTF-8 without BOM
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (LPSTR)pv, cb,
                            szText, ARRAYSIZE(szText)))
    {
        return szText;
    }

    // Unicode (Little Endian) without BOM
    if (IsTextUnicode(pv, cb, NULL))
    {
        return (LPWSTR)pv;
    }

    // ANSI (Shift_JIS)
    ZeroMemory(szText, sizeof(szText));
    if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, (LPSTR)pv, cb,
                            szText, ARRAYSIZE(szText)))
    {
        return szText;
    }

    // UTF-8 without BOM
    MultiByteToWideChar(CP_UTF8, 0, (LPSTR)pv, cb, szText, ARRAYSIZE(szText));
    return szText;
}

void OnEditChange(HWND hwnd);

unsigned __stdcall DoThreadFunc(void *arg)
{
    HWND hwnd = (HWND)arg;
    assert(s_bInProcessing);

    EnableWindow(GetDlgItem(hwnd, edt1), FALSE);
    EnableWindow(GetDlgItem(hwnd, cmb1), FALSE);
    EnableWindow(GetDlgItem(hwnd, cmb2), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDOK), FALSE);
    EnableWindow(GetDlgItem(hwnd, psh1), FALSE);
    EnableWindow(GetDlgItem(hwnd, psh2), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDCANCEL), FALSE);

    WCHAR szText[MAX_TEXT + 1];
    Edit_GetText(GetDlgItem(hwnd, edt1), szText, ARRAYSIZE(szText));
    //DoTrimRight(szText, L" \t\r\n");

    if (s_hbm1)
    {
        DeleteObject(s_hbm1);
        s_hbm1 = NULL;
    }

    if (s_hbm2)
    {
        DeleteObject(s_hbm2);
        s_hbm2 = NULL;
    }

    do
    {
        if (szText[0] == 0)
        {
            break;
        }

        INT nMode = (INT)SendDlgItemMessage(hwnd, cmb1, CB_GETCURSEL, 0, 0);
        INT nQuality = (INT)SendDlgItemMessage(hwnd, cmb2, CB_GETCURSEL, 0, 0);

        DeleteFileW(s_szTempFile);

        if (!DoExecuteQrEncode(hwnd, szText, s_szTempFile, nMode, nQuality))
        {
            break;
        }

        if (!PathFileExistsW(s_szTempFile))
        {
            break;
        }

        s_hbm1 = DoLoadPngAsBitmap(s_szTempFile);

        HWND hStc1 = GetDlgItem(hwnd, stc1);
        RECT rc;
        GetClientRect(hStc1, &rc);

        INT cxStc1 = rc.right - rc.left;
        INT cyStc1 = rc.bottom - rc.top;
        INT cxyStc1 = std::min(cxStc1, cyStc1);

        s_hbm2 = StretchImage(s_hbm1, cxyStc1, cxyStc1);
    } while (0);

    SendDlgItemMessage(hwnd, stc1, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)s_hbm2);

    EnableWindow(GetDlgItem(hwnd, edt1), TRUE);
    EnableWindow(GetDlgItem(hwnd, cmb1), TRUE);
    EnableWindow(GetDlgItem(hwnd, cmb2), TRUE);
    EnableWindow(GetDlgItem(hwnd, psh2), TRUE);
    EnableWindow(GetDlgItem(hwnd, IDCANCEL), TRUE);

    if (szText[0] && s_hbm1)
    {
        EnableWindow(GetDlgItem(hwnd, IDOK), TRUE);
        EnableWindow(GetDlgItem(hwnd, psh1), TRUE);
    }
    else
    {
        EnableWindow(GetDlgItem(hwnd, IDOK), FALSE);
        EnableWindow(GetDlgItem(hwnd, psh1), FALSE);
    }

    SetForegroundWindow(hwnd);

    std::wstring text = szText;
    DoReplaceAll(text, L"\r", L"");

    INT cch = lstrlenW(text.c_str());
    if (cch)
    {
        wsprintfW(szText, LoadStringDx(IDS_CHARACTERS), cch);
        SetDlgItemTextW(hwnd, stc2, szText);
    }
    else
    {
        SetDlgItemTextW(hwnd, stc2, NULL);
    }

    BOOL bUpdated = s_bUpdatedInProcessing;
    s_bInProcessing = FALSE;
    s_bUpdatedInProcessing = FALSE;

    if (bUpdated)
    {
        OnEditChange(hwnd);
    }

    return 0;
}

void OnEditChange(HWND hwnd)
{
    WCHAR sz[2];
    if (GetDlgItemTextW(hwnd, edt1, sz, ARRAYSIZE(sz)) && sz[0])
    {
        EnableWindow(GetDlgItem(hwnd, IDOK), TRUE);
        EnableWindow(GetDlgItem(hwnd, psh1), TRUE);
    }
    else
    {
        EnableWindow(GetDlgItem(hwnd, IDOK), FALSE);
        EnableWindow(GetDlgItem(hwnd, psh1), FALSE);
    }

    if (s_bInProcessing)
    {
        s_bUpdatedInProcessing = TRUE;
    }
    else
    {
        s_bInProcessing = TRUE;
        HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, DoThreadFunc, hwnd, 0, NULL);
        CloseHandle(hThread);
    }
}

BOOL DoOpenTextFile(HWND hwnd, LPCWSTR pszFileName)
{
    HANDLE hFile = CreateFileW(pszFileName, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        MessageBoxW(hwnd, LoadStringDx(IDS_CANTOPENFILE), LoadStringDx(IDS_APPNAME), MB_ICONERROR);
        return FALSE;
    }

    BOOL bOK = FALSE;
    DWORD cb = GetFileSize(hFile, NULL);
    if (cb > 3 + MAX_TEXT * sizeof(WCHAR))
    {
        CloseHandle(hFile);
        MessageBoxW(hwnd, LoadStringDx(IDS_FILETOOLARGE), LoadStringDx(IDS_APPNAME), MB_ICONERROR);
        return FALSE;
    }

    DWORD cbRead;
    BYTE ab[3 + (MAX_TEXT + 1) * sizeof(WCHAR)] = { 0 };
    if (ReadFile(hFile, ab, cb, &cbRead, NULL))
    {
        std::wstring str = DoReadBinaryText(hwnd, ab, cb);
        if (str.size() > MAX_TEXT)
            str.resize(MAX_TEXT);

        SetDlgItemTextW(hwnd, edt1, str.c_str());
        Edit_SetSel(GetDlgItem(hwnd, edt1), 0, -1);
        SendDlgItemMessage(hwnd, edt1, EM_SCROLLCARET, 0, 0);
        OnEditChange(hwnd);
        bOK = !str.empty();
    }

    CloseHandle(hFile);
    return TRUE;
}

BOOL DoOpenImageFile(HWND hwnd, LPCWSTR pszFileName)
{
    QR_CALLBACK callback;
    if (QR_ReadFile(pszFileName, callback))
    {
        std::string& data = callback.m_strs[0];
        if (data.empty() || data.size() >= 0x1000)
            return FALSE;

        std::wstring str = DoReadBinaryText(hwnd, &data[0], DWORD(data.size()));
        if (str.size() > MAX_TEXT)
            str.resize(MAX_TEXT);

        SetDlgItemTextW(hwnd, edt1, str.c_str());
        Edit_SetSel(GetDlgItem(hwnd, edt1), 0, -1);
        SendDlgItemMessage(hwnd, edt1, EM_SCROLLCARET, 0, 0);
        OnEditChange(hwnd);
        return !str.empty();
    }

    return TRUE;
}

BOOL DoOpenFile(HWND hwnd, LPCWSTR pszFileName)
{
    LPTSTR pchDotExt = PathFindExtension(pszFileName);

    if (lstrcmpi(pchDotExt, TEXT(".txt")) == 0)
        return DoOpenTextFile(hwnd, pszFileName);

    if (lstrcmpi(pchDotExt, TEXT(".bmp")) == 0 ||
        lstrcmpi(pchDotExt, TEXT(".png")) == 0 ||
        lstrcmpi(pchDotExt, TEXT(".jpg")) == 0 ||
        lstrcmpi(pchDotExt, TEXT(".jpeg")) == 0 ||
        lstrcmpi(pchDotExt, TEXT(".gif")) == 0 ||
        lstrcmpi(pchDotExt, TEXT(".tif")) == 0 ||
        lstrcmpi(pchDotExt, TEXT(".tiff")) == 0 ||
        lstrcmpi(pchDotExt, TEXT(".dib")) == 0)
    {
        return DoOpenImageFile(hwnd, pszFileName);
    }

    return FALSE;
}

LRESULT CALLBACK
EditWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_PAINT:
        CallWindowProc(s_fnEditWndProc, hwnd, uMsg, wParam, lParam);
        if (GetWindowTextLength(hwnd) == 0)
        {
            DoShowPlaceHolder(hwnd);
        }
        return 0;
    }

    return CallWindowProc(s_fnEditWndProc, hwnd, uMsg, wParam, lParam);
}

BOOL OnInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam)
{
    RECT rc;
    GetWindowRect(hwnd, &rc);
    s_cxMinTrack = rc.right - rc.left;
    s_cyMinTrack = rc.bottom - rc.top;

    s_layout.init(hwnd);

    s_hIcon = LoadIcon(s_hInstance, MAKEINTRESOURCE(IDI_MAIN));
    s_hIconSmall = (HICON)LoadImage(s_hInstance, MAKEINTRESOURCE(IDI_MAIN), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);

    SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)s_hIcon);
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)s_hIconSmall);

    Edit_LimitText(GetDlgItem(hwnd, edt1), MAX_TEXT);

    s_fnEditWndProc = SubclassWindow(GetDlgItem(hwnd, edt1), EditWindowProc);

    DragAcceptFiles(hwnd, TRUE);

    EnableWindow(GetDlgItem(hwnd, IDOK), FALSE);
    EnableWindow(GetDlgItem(hwnd, psh1), FALSE);

    HWND hCmb1 = GetDlgItem(hwnd, cmb1);
    ComboBox_AddString(hCmb1, LoadStringDx(IDS_ASCII));
    ComboBox_AddString(hCmb1, LoadStringDx(IDS_ICASEASCII));
    ComboBox_AddString(hCmb1, LoadStringDx(IDS_JPNSHIFTJIS));
    ComboBox_AddString(hCmb1, LoadStringDx(IDS_8BIT));
    ComboBox_SetCurSel(hCmb1, 2);

    HWND hCmb2 = GetDlgItem(hwnd, cmb2);
    ComboBox_AddString(hCmb2, LoadStringDx(IDS_L));
    ComboBox_AddString(hCmb2, LoadStringDx(IDS_M));
    ComboBox_AddString(hCmb2, LoadStringDx(IDS_Q));
    ComboBox_AddString(hCmb2, LoadStringDx(IDS_H));
    ComboBox_SetCurSel(hCmb2, 1);

    INT argc = 0;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 2)
    {
        DoOpenFile(hwnd, wargv[1]);
    }
    LocalFree(wargv);

    return TRUE;
}

void OnCopy(HWND hwnd)
{
    if (!s_hbm1 || s_bInProcessing)
        return;

    BOOL bOK = FALSE;
    if (OpenClipboard(hwnd))
    {
        EmptyClipboard();

        std::vector<BYTE> dib;
        DoGetDIBFromBitmap(dib, s_hbm1);

        if (HGLOBAL hGlobal = GlobalAlloc(GHND | GMEM_SHARE, dib.size()))
        {
            if (LPVOID pv = GlobalLock(hGlobal))
            {
                CopyMemory(pv, dib.data(), dib.size());
                GlobalUnlock(hGlobal);

                bOK = !!SetClipboardData(CF_DIB, hGlobal);
            }

            if (!bOK)
                GlobalFree(hGlobal);
        }

        DWORD cbDrop = sizeof(DROPFILES) + (lstrlenW(s_szTempFile) + 2) * sizeof(WCHAR);
        if (HDROP hDrop = (HDROP)GlobalAlloc(GHND | GMEM_SHARE, cbDrop))
        {
            if (LPDROPFILES pDropFiles = (LPDROPFILES)GlobalLock(hDrop))
            {
                pDropFiles->pFiles = sizeof(DROPFILES);
                pDropFiles->pt.x = -1;
                pDropFiles->pt.y = -1;
                pDropFiles->fNC = TRUE;
                pDropFiles->fWide = TRUE;
                LPWSTR pszz = LPWSTR(pDropFiles + 1);

                LPWSTR pch = pszz;
                lstrcpyW(pch, s_szTempFile);
                pch += lstrlenW(pch) + 1;
                *pch = 0;
                ++pch;
                assert((void *)pch == LPBYTE(pDropFiles) + cbDrop);

                GlobalUnlock(hDrop);

                SetClipboardData(CF_HDROP, hDrop);
            }
        }

        CloseClipboard();
    }

    if (bOK)
    {
        SetDlgItemTextW(hwnd, IDOK, LoadStringDx(IDS_COPYED));
        SetTimer(hwnd, TIMER_ID, 2000, NULL);
    }
    else
    {
        MessageBoxW(hwnd, LoadStringDx(IDS_COPYFAIL), LoadStringDx(IDS_APPNAME), MB_ICONERROR);
    }
}

void OnSaveAs(HWND hwnd)
{
    if (s_bInProcessing || !PathFileExistsW(s_szTempFile))
        return;

    WCHAR szFile[MAX_PATH] = L"";
    OPENFILENAMEW ofn = { OPENFILENAME_SIZE_VERSION_400W };
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = MakeFilterDx(LoadStringDx(IDS_PNGFILTER));
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = ARRAYSIZE(szFile);
    ofn.lpstrTitle = LoadStringDx(IDS_SAVEIMGAS);
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT |
                OFN_PATHMUSTEXIST | OFN_ENABLESIZING;
    ofn.lpstrDefExt = L"png";
    if (GetSaveFileNameW(&ofn))
    {
        if (!CopyFileW(s_szTempFile, szFile, FALSE))
        {
            MessageBoxW(hwnd, LoadStringDx(IDS_SAVEFAIL), LoadStringDx(IDS_APPNAME), MB_ICONERROR);
        }
    }
}

void OnOpenReadMe(HWND hwnd)
{
    if (s_bInProcessing)
        return;

    TCHAR szPath[MAX_PATH];
    GetModuleFileName(NULL, szPath, ARRAYSIZE(szPath));
    PathRemoveFileSpec(szPath);
    PathAppend(szPath, TEXT("README.txt"));
    if (!PathFileExists(szPath))
    {
        PathRemoveFileSpec(szPath);
        PathAppend(szPath, TEXT("..\\README.txt"));
        if (!PathFileExists(szPath))
        {
            PathRemoveFileSpec(szPath);
            PathAppend(szPath, TEXT("..\\..\\README.txt"));
        }
    }

    ShellExecute(hwnd, NULL, szPath, NULL, NULL, SW_SHOWNORMAL);
}

void OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
    switch (id)
    {
    case IDOK:
        OnCopy(hwnd);
        break;
    case ID_COPY:
        OnCopy(hwnd);
        break;
    case IDCANCEL:
        EndDialog(hwnd, id);
        break;
    case psh1:
        OnSaveAs(hwnd);
        break;
    case psh2:
        OnOpenReadMe(hwnd);
        break;
    case edt1:
        if (codeNotify == EN_CHANGE)
        {
            OnEditChange(hwnd);
        }
        break;
    case cmb1:
        if (codeNotify == CBN_SELCHANGE)
        {
            OnEditChange(hwnd);
        }
        break;
    case cmb2:
        if (codeNotify == CBN_SELCHANGE)
        {
            OnEditChange(hwnd);
        }
        break;
    }
}

void OnTimer(HWND hwnd, UINT id)
{
    if (id == TIMER_ID)
    {
        SetDlgItemTextW(hwnd, IDOK, LoadStringDx(IDS_COPY));
        SetFocus(GetDlgItem(hwnd, edt1));
    }
}

void OnDropFiles(HWND hwnd, HDROP hdrop)
{
    WCHAR szFile[MAX_PATH];
    szFile[0] = 0;
    DragQueryFileW(hdrop, 0, szFile, ARRAYSIZE(szFile));

    DoOpenFile(hwnd, szFile);
    DragFinish(hdrop);
}

void OnSize(HWND hwnd, UINT state, int cx, int cy)
{
    s_layout.OnSize(cx, cy);

    if (s_hbm2)
    {
        DeleteObject(s_hbm2);
        s_hbm2 = NULL;
    }

    HWND hStc1 = GetDlgItem(hwnd, stc1);
    RECT rc;
    GetClientRect(hStc1, &rc);

    INT cxStc1 = rc.right - rc.left;
    INT cyStc1 = rc.bottom - rc.top;
    INT cxyStc1 = std::min(cxStc1, cyStc1);
    s_hbm2 = StretchImage(s_hbm1, cxyStc1, cxyStc1);

    SendDlgItemMessage(hwnd, stc1, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)s_hbm2);
}

void OnGetMinMaxInfo(HWND hwnd, LPMINMAXINFO lpMinMaxInfo)
{
    lpMinMaxInfo->ptMinTrackSize.x = s_cxMinTrack;
    lpMinMaxInfo->ptMinTrackSize.y = s_cyMinTrack;
}

void OnContextMenu(HWND hwnd, HWND hwndContext, UINT xPos, UINT yPos)
{
    if (hwndContext == GetDlgItem(hwnd, stc1))
    {
        HMENU hMenu = LoadMenu(s_hInstance, MAKEINTRESOURCE(IDR_POPUP));
        HMENU hSubMenu = GetSubMenu(hMenu, 0);

        if (!IsWindowEnabled(GetDlgItem(hwnd, IDOK)))
        {
            EnableMenuItem(hMenu, ID_COPY, MF_BYCOMMAND | MF_GRAYED);
        }

        SetForegroundWindow(hwnd);
        INT nCmd = TrackPopupMenuEx(hSubMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                    xPos, yPos, hwnd, NULL);
        DestroyMenu(hMenu);
        PostMessage(hwnd, WM_COMMAND, nCmd, 0);
        PostMessage(hwnd, WM_NULL, 0, 0);
    }
}

INT_PTR CALLBACK
DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        HANDLE_MSG(hwnd, WM_INITDIALOG, OnInitDialog);
        HANDLE_MSG(hwnd, WM_COMMAND, OnCommand);
        HANDLE_MSG(hwnd, WM_TIMER, OnTimer);
        HANDLE_MSG(hwnd, WM_DROPFILES, OnDropFiles);
        HANDLE_MSG(hwnd, WM_SIZE, OnSize);
        HANDLE_MSG(hwnd, WM_GETMINMAXINFO, OnGetMinMaxInfo);
        HANDLE_MSG(hwnd, WM_CONTEXTMENU, OnContextMenu);
    }
    return 0;
}

INT WINAPI
WinMain(HINSTANCE   hInstance,
        HINSTANCE   hPrevInstance,
        LPSTR       lpCmdLine,
        INT         nCmdShow)
{
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, 0);
    {
        s_hInstance = hInstance;
        InitCommonControls();
        srand(GetTickCount());

        DoGetTempPathName(s_szTempFile);

        DialogBox(hInstance, MAKEINTRESOURCE(IDD_MAIN), NULL, DialogProc);

        DeleteFileW(s_szTempFile);
        DeleteObject(s_hbm1);
        DeleteObject(s_hbm2);
    }
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return 0;
}
