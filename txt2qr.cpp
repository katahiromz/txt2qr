#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <process.h>
#include <string>
#include <vector>
#include <cstdlib>
#include <csetjmp>
#include <cassert>
#include "resource.h"

#define MAX_TEXT 255

#include <png.h>
//#pragma comment(lib, "libpng.lib")
//#pragma comment(lib, "zlib.lib")

static HINSTANCE s_hInstance = NULL;
static HICON s_hIcon = NULL;
static HICON s_hIconSmall = NULL;
static WCHAR s_szTempFile[MAX_PATH] = L"";
static HBITMAP s_hbm1 = NULL;
static HBITMAP s_hbm2 = NULL;
static BOOL s_bInProcessing = FALSE;
static BOOL s_bUpdatedInProcessing = FALSE;
static WNDPROC s_fnEditWndProc = NULL;

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

#define WIDTHBYTES(i) (((i) + 31) / 32 * 4)

HBITMAP DoLoadPngAsBitmap(LPCWSTR pszFileName)
{
    FILE            *inf;
    HBITMAP         hbm;
    png_structp     png;
    png_infop       info;
    png_uint_32     y, width, height, rowbytes;
    int             color_type, depth, widthbytes;
    double          gamma;
    BITMAPINFO      bi;
    BYTE            *pbBits;
    png_bytepp      row_pointers;

    inf = _wfopen(pszFileName, L"rb");
    if (inf == NULL)
        return NULL;

    png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png == NULL)
    {
        fclose(inf);
        return NULL;
    }

    info = png_create_info_struct(png);
    if (info == NULL || setjmp(png_jmpbuf(png)))
    {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(inf);
        return NULL;
    }

    if (setjmp(png_jmpbuf(png)))
    {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(inf);
        return NULL;
    }

    png_init_io(png, inf);
    png_read_info(png, info);

    png_get_IHDR(png, info, &width, &height, &depth, &color_type,
                 NULL, NULL, NULL);
    png_set_strip_16(png);
    png_set_gray_to_rgb(png);
    png_set_palette_to_rgb(png);
    png_set_bgr(png);
    png_set_packing(png);
    if (png_get_gAMA(png, info, &gamma))
        png_set_gamma(png, 2.2, gamma);
    else
        png_set_gamma(png, 2.2, 0.45455);

    png_read_update_info(png, info);
    png_get_IHDR(png, info, &width, &height, &depth, &color_type,
                 NULL, NULL, NULL);

    rowbytes = png_get_rowbytes(png, info);
    row_pointers = (png_bytepp)malloc(height * sizeof(png_bytep));
    for (y = 0; y < height; y++)
    {
        row_pointers[y] = (png_bytep)png_malloc(png, rowbytes);
    }

    png_read_image(png, row_pointers);
    png_read_end(png, NULL);
    fclose(inf);

    ZeroMemory(&bi.bmiHeader, sizeof(BITMAPINFOHEADER));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = width;
    bi.bmiHeader.biHeight      = height;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = depth * png_get_channels(png, info);

    hbm = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (VOID **)&pbBits, 
                           NULL, 0);
    if (hbm == NULL)
    {
        png_destroy_read_struct(&png, &info, NULL);
        return NULL;
    }

    widthbytes = WIDTHBYTES(width * bi.bmiHeader.biBitCount);
    for(y = 0; y < height; y++)
    {
        CopyMemory(pbBits + y * widthbytes, 
                   row_pointers[height - 1 - y], rowbytes);
    }

    png_destroy_read_struct(&png, &info, NULL);
    free(row_pointers);
    return hbm;
}

BOOL DoExecuteQrEncode(HWND hwnd, LPCWSTR pszText, LPCWSTR pszOutFile, BOOL bKanji)
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

    if (bKanji)
        lstrcatW(szParams, L" --kanji");

    lstrcatW(szParams, L" \"");
    lstrcatW(szParams, pszText);
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

typedef struct tagBITMAPINFOEX
{
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD          bmiColors[256];
} BITMAPINFOEX, FAR * LPBITMAPINFOEX;

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
        LPCWSTR pszText = LoadStringDx(105);
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

std::wstring DoReadBinaryText(HWND hwnd, LPVOID pv, DWORD cb)
{
    WCHAR szText[MAX_TEXT + 1] = { 0 };

    // UTF-8 BOM
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

    // UTF-16LE BOM
    if (memcmp(pv, "\xFF\xFE", 2) == 0 && cb >= 2)
    {
        BYTE *pb = (BYTE *)pv;
        pb += 2;
        cb -= 2;
        return (LPWSTR)pb;
    }

    // UTF-16BE BOM
    if (memcmp(pv, "\xFE\xFF", 2) == 0 && cb >= 2)
    {
        BYTE *pb = (BYTE *)pv;
        pb += 2;
        cb -= 2;
        DoSwapEndian(pb, cb);
        return (LPWSTR)pb;
    }

    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (LPSTR)pv, cb,
                            szText, ARRAYSIZE(szText)))
    {
        return szText;
    }

    if (IsTextUnicode(pv, cb, NULL))
    {
        return (LPWSTR)pv;
    }

    ZeroMemory(szText, sizeof(szText));
    if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, (LPSTR)pv, cb,
                            szText, ARRAYSIZE(szText)))
    {
        return szText;
    }

    return L"";
}

unsigned __stdcall ProcessingFunc(void *arg)
{
    HWND hwnd = (HWND)arg;
    assert(s_bInProcessing);

    EnableWindow(GetDlgItem(hwnd, edt1), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDOK), FALSE);
    EnableWindow(GetDlgItem(hwnd, psh1), FALSE);
    EnableWindow(GetDlgItem(hwnd, psh2), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDCANCEL), FALSE);

    WCHAR szText[MAX_TEXT + 1];
    Edit_GetText(GetDlgItem(hwnd, edt1), szText, ARRAYSIZE(szText));
    StrTrimW(szText, L" \t");

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
            s_hbm2 = CreateBitmap(1, 1, 1, 1, NULL);
            break;
        }

        BOOL bKanji = (IsDlgButtonChecked(hwnd, chx1) == BST_CHECKED);

        DeleteFileW(s_szTempFile);

        if (!DoExecuteQrEncode(hwnd, szText, s_szTempFile, bKanji))
        {
            s_hbm2 = CreateBitmap(1, 1, 1, 1, NULL);
            break;
        }

        if (!PathFileExistsW(s_szTempFile))
        {
            s_hbm2 = CreateBitmap(1, 1, 1, 1, NULL);
            break;
        }

        s_hbm1 = DoLoadPngAsBitmap(s_szTempFile);

        HWND hStc1 = GetDlgItem(hwnd, stc1);
        RECT rc;
        GetClientRect(hStc1, &rc);
        INT cx = rc.right - rc.left, cy = rc.bottom - rc.top;

        BITMAP bm;
        GetObject(s_hbm1, sizeof(bm), &bm);

        if (bm.bmWidth > cx || bm.bmHeight > cy)
        {
            s_hbm2 = (HBITMAP)CopyImage(s_hbm1, IMAGE_BITMAP, cy, cy, LR_CREATEDIBSECTION);
        }
        else
        {
            s_hbm2 = (HBITMAP)CopyImage(s_hbm1, IMAGE_BITMAP, bm.bmWidth, bm.bmHeight, LR_CREATEDIBSECTION);
        }
    } while (0);

    SendDlgItemMessage(hwnd, stc1, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)s_hbm2);

    EnableWindow(GetDlgItem(hwnd, edt1), TRUE);
    EnableWindow(GetDlgItem(hwnd, IDOK), TRUE);
    EnableWindow(GetDlgItem(hwnd, psh1), TRUE);
    EnableWindow(GetDlgItem(hwnd, psh2), TRUE);
    EnableWindow(GetDlgItem(hwnd, IDCANCEL), TRUE);

    BOOL bUpdated = s_bUpdatedInProcessing;
    s_bInProcessing = FALSE;
    s_bUpdatedInProcessing = FALSE;

    if (bUpdated)
    {
        PostMessage(hwnd, WM_COMMAND, psh5, 0);
    }

    return 0;
}

void OnEditChange(HWND hwnd)
{
    if (s_bInProcessing)
    {
        s_bUpdatedInProcessing = TRUE;
    }
    else
    {
        s_bInProcessing = TRUE;

        HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, ProcessingFunc, hwnd, 0, NULL);
        CloseHandle(hThread);
    }
}

BOOL DoOpenTextFile(HWND hwnd, LPCWSTR pszFileName)
{
    HANDLE hFile = CreateFileW(pszFileName, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }

    BOOL bOK = FALSE;
    DWORD cb = GetFileSize(hFile, NULL);
    if (cb > MAX_TEXT * sizeof(WCHAR))
        cb = MAX_TEXT * sizeof(WCHAR);

    DWORD cbRead;
    BYTE ab[(MAX_TEXT + 1) * sizeof(WCHAR)] = { 0 };
    if (ReadFile(hFile, ab, cb, &cbRead, NULL))
    {
        std::wstring str = DoReadBinaryText(hwnd, ab, cb);
        if (str.size() > MAX_TEXT)
            str.resize(MAX_TEXT);

        SetDlgItemTextW(hwnd, edt1, str.c_str());
        Edit_SetSel(GetDlgItem(hwnd, edt1), 0, -1);
        OnEditChange(hwnd);
        bOK = !str.empty();
    }

    CloseHandle(hFile);
    return TRUE;
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
    s_hIcon = LoadIcon(s_hInstance, MAKEINTRESOURCE(IDI_MAIN));
    s_hIconSmall = (HICON)LoadImageW(s_hInstance, MAKEINTRESOURCE(IDI_MAIN), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);

    SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)s_hIcon);
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)s_hIconSmall);

    Edit_LimitText(GetDlgItem(hwnd, edt1), 255);
    CheckDlgButton(hwnd, chx1, BST_CHECKED);

    s_fnEditWndProc = SubclassWindow(GetDlgItem(hwnd, edt1), EditWindowProc);

    DragAcceptFiles(hwnd, TRUE);

    INT argc = 0;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 2)
    {
        DoOpenTextFile(hwnd, wargv[1]);
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
        SetDlgItemTextW(hwnd, IDOK, LoadStringDx(106));
        SetTimer(hwnd, 999, 2000, NULL);
    }
    else
    {
        MessageBoxW(hwnd, LoadStringDx(104), LoadStringDx(100), MB_ICONERROR);
    }
}

void OnSaveAs(HWND hwnd)
{
    if (s_bInProcessing || !PathFileExistsW(s_szTempFile))
        return;

    WCHAR szFile[MAX_PATH] = L"";
    OPENFILENAMEW ofn = { OPENFILENAME_SIZE_VERSION_400W };
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = MakeFilterDx(LoadStringDx(108));
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = ARRAYSIZE(szFile);
    ofn.lpstrTitle = LoadStringDx(109);
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT |
                OFN_PATHMUSTEXIST | OFN_ENABLESIZING;
    ofn.lpstrDefExt = L"png";
    if (GetSaveFileNameW(&ofn))
    {
        if (!CopyFileW(s_szTempFile, szFile, FALSE))
        {
            MessageBoxW(hwnd, LoadStringDx(102), LoadStringDx(100), MB_ICONERROR);
        }
    }
}

void OnAbout(HWND hwnd)
{
    if (s_bInProcessing)
        return;

    MSGBOXPARAMS params = { sizeof(params) };
    params.hwndOwner = hwnd;
    params.hInstance = s_hInstance;
    params.lpszText = LoadStringDx(101);
    params.lpszCaption = LoadStringDx(100);
    params.dwStyle = MB_USERICON;
    params.lpszIcon = MAKEINTRESOURCE(IDI_MAIN);
    MessageBoxIndirectW(&params);
}

void OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
    switch (id)
    {
    case IDOK:
        OnCopy(hwnd);
        break;
    case IDCANCEL:
        EndDialog(hwnd, id);
        break;
    case psh1:
        OnSaveAs(hwnd);
        break;
    case psh2:
        OnAbout(hwnd);
        break;
    case edt1:
        if (codeNotify == EN_CHANGE)
        {
            OnEditChange(hwnd);
        }
        break;
    case psh5:
        OnEditChange(hwnd);
        break;
    }
}

void OnTimer(HWND hwnd, UINT id)
{
    if (id == 999)
    {
        SetDlgItemTextW(hwnd, IDOK, LoadStringDx(107));
        SetFocus(GetDlgItem(hwnd, edt1));
    }
}

void OnDropFiles(HWND hwnd, HDROP hdrop)
{
    WCHAR szFile[MAX_PATH];
    szFile[0] = 0;
    DragQueryFileW(hdrop, 0, szFile, ARRAYSIZE(szFile));

    DoOpenTextFile(hwnd, szFile);
    DragFinish(hdrop);
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
    }
    return 0;
}

INT WINAPI
WinMain(HINSTANCE   hInstance,
        HINSTANCE   hPrevInstance,
        LPSTR       lpCmdLine,
        INT         nCmdShow)
{
    s_hInstance = hInstance;
    InitCommonControls();
    srand(GetTickCount());
    DoGetTempPathName(s_szTempFile);

    DialogBox(hInstance, MAKEINTRESOURCE(IDD_MAIN), NULL, DialogProc);

    DeleteFileW(s_szTempFile);
    DeleteObject(s_hbm1);
    DeleteObject(s_hbm2);

    return 0;
}
