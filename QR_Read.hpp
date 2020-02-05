#ifndef QR_READ_HPP_
#define QR_READ_HPP_

#include <windows.h>
#include <gdiplus.h>        // GDI+
#include <vector>
#include <cstdio>
#include "zbar.h"

typedef struct BITMAPINFOEX
{
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD bmiColors[256];
} BITMAPINFOEX;

inline HBITMAP
Create8BppDIB(HDC hdc, INT width, INT height, LPVOID *ppvBits)
{
    BITMAPINFOEX bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 8;
    for (INT i = 0; i < 256; ++i)
    {
        BYTE b = static_cast<BYTE>(i);
        bmi.bmiColors[i].rgbBlue = b;
        bmi.bmiColors[i].rgbGreen = b;
        bmi.bmiColors[i].rgbRed = b;
        bmi.bmiColors[i].rgbReserved = 0;
    }
    return CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO *>(&bmi), DIB_RGB_COLORS,
                            ppvBits, NULL, 0);
}

inline HBITMAP
ConvertTo8BppDIB(HBITMAP hbm1, LPVOID *ppvBits)
{
    HBITMAP hbm2 = NULL;

    BITMAP bm;
    GetObject(hbm1, sizeof(bm), &bm);

    if (HDC hdc1 = CreateCompatibleDC(NULL))
    {
        if (HDC hdc2 = CreateCompatibleDC(NULL))
        {
            hbm2 = Create8BppDIB(hdc2, bm.bmWidth, bm.bmHeight, ppvBits);
            if (hbm2)
            {
                HGDIOBJ hbm1Old = SelectObject(hdc1, hbm1);
                HGDIOBJ hbm2Old = SelectObject(hdc2, hbm2);

                BitBlt(hdc2, 0, 0, bm.bmWidth, bm.bmHeight, hdc1, 0, 0, SRCCOPY);

                SelectObject(hdc1, hbm1Old);
                SelectObject(hdc2, hbm2Old);
            }
            DeleteDC(hdc2);
        }
        DeleteDC(hdc1);
    }

    return hbm2;
}

template <typename T_CALLBACK>
inline bool
QR_ReadRaw(LONG width, LONG height, void *raw, T_CALLBACK& callback, LPARAM lParam = 0)
{
    zbar::Image image(width, height, "Y800", raw, width * height);

    zbar::ImageScanner scanner;
    scanner.set_config(zbar::ZBAR_NONE, zbar::ZBAR_CFG_ENABLE, 0);
    scanner.set_config(zbar::ZBAR_QRCODE, zbar::ZBAR_CFG_ENABLE, 1);
    int n = scanner.scan(image);

    for (auto symbol = image.symbol_begin(); symbol != image.symbol_end(); ++symbol)
    {
        if (!callback(symbol, lParam))
            return false;
    }

    return n > 0;
}

template <typename T_CALLBACK>
inline bool
QR_ReadHBM(HBITMAP hbm, T_CALLBACK& callback, LPARAM lParam = 0)
{
    BITMAP bm;
    if (!GetObject(hbm, sizeof(bm), &bm))
        return false;

    LPVOID pvBits = NULL;
    HBITMAP hbm8bpp = ConvertTo8BppDIB(hbm, &pvBits);
    if (!GetObject(hbm8bpp, sizeof(bm), &bm) || !pvBits)
        return false;

    LPBYTE pb = reinterpret_cast<LPBYTE>(pvBits);
    std::vector<BYTE> vec(bm.bmWidth * bm.bmHeight);

    for (LONG y = 0; y < bm.bmHeight; ++y)
    {
        memcpy(&vec[y * bm.bmWidth], &pb[y * bm.bmWidthBytes], bm.bmWidth);
    }

    return QR_ReadRaw(bm.bmWidth, bm.bmHeight, vec.data(), callback, lParam);
}

template <typename T_CALLBACK>
inline bool
QR_ReadFile(LPCWSTR filename, T_CALLBACK& callback, LPARAM lParam = 0)
{
    using namespace Gdiplus;
    Bitmap *pBitmap = Bitmap::FromFile(filename);
    if (!pBitmap)
        return false;

    HBITMAP hbm = NULL;
    Color color(0xFF, 0xFF, 0xFF);
    Status status = pBitmap->GetHBITMAP(color, &hbm);
    delete pBitmap;
    if (!hbm)
        return false;

    bool ret = QR_ReadHBM(hbm, callback, lParam);
    DeleteObject(hbm);

    return ret;
}

struct QR_CALLBACK
{
    std::vector<std::string> m_strs;

    QR_CALLBACK()
    {
    }

    bool operator()(zbar::Image::SymbolIterator& symbol, LPARAM lParam = 0)
    {
        if (symbol->get_type() != zbar::ZBAR_QRCODE)
            return true;

        m_strs.emplace_back(symbol->get_data());
        return true;
    }
};

#ifdef QR_READ_UNITTEST
extern "C"
int __cdecl wmain(int argc, wchar_t **wargv)
{
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, 0);

    QR_CALLBACK callback;
    if (QR_ReadFile(wargv[1], callback))
    {
        printf("%s\n", callback.m_strs[0].c_str());
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    return 0;
}
#endif  // def QR_READ_UNITTEST

#endif  // ndef QR_READ_HPP_
