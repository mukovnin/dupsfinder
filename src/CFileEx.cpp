#include "stdafx.h"
#include "CFileEx.h"
#include "StrSafe.h"
#include "Shlwapi.h"
#include "windows.h"
#include <utility>

DuplicatesType CFileEx::TypeOfComparison;

CFileEx::CFileEx(FILETIME CreationTime, DWORD FileAttributes, ULONGLONG Size, LPCTSTR Name, LPCTSTR Folder)
    : ftCreationTime(CreationTime), dwFileAttributes(FileAttributes), nSize(Size), cName(Name), cFolder(Folder)
{
}

CFileEx::~CFileEx()
{
}

bool CFileEx::IsEqualContents(LPCTSTR firstFullName, LPCTSTR secondFullName)
{
    constexpr int BufferSize = 16384;
    bool res = false;
    HANDLE hFirstFile = CreateFile(
                firstFullName,
                GENERIC_READ,
                FILE_SHARE_READ,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
    if (hFirstFile != INVALID_HANDLE_VALUE)
    {
        HANDLE hSecondFile = CreateFile(
                    secondFullName,
                    GENERIC_READ,
                    FILE_SHARE_READ,
                    nullptr,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr);
        if (hSecondFile != INVALID_HANDLE_VALUE)
        {
            BYTE FirstBuffer[BufferSize], SecondBuffer[BufferSize];
            DWORD FirstBytesRead, SecondBytesRead;
            BOOL ReadSuccess;
            BOOL EndOfFile;
            BOOL CompareResult = TRUE;
            do
            {
                ReadSuccess = ReadFile(hFirstFile, FirstBuffer, BufferSize, &FirstBytesRead, nullptr)
                        && ReadFile(hSecondFile, SecondBuffer, BufferSize, &SecondBytesRead, nullptr);
                EndOfFile = (FirstBytesRead == 0 || SecondBytesRead == 0);
                if (ReadSuccess && !EndOfFile)
                    CompareResult = CompareResult
                            && FirstBytesRead == SecondBytesRead
                            && !memcmp(FirstBuffer, SecondBuffer, FirstBytesRead);
            } while (ReadSuccess && CompareResult && !EndOfFile);
            res = EndOfFile && ReadSuccess && CompareResult;
            CloseHandle(hSecondFile);
        }
        CloseHandle(hFirstFile);
    }
    return res;
}

LPCTSTR CFileEx::GetFormattedSizeString() const
{
    if (cSizeString.empty())
    {
        TCHAR* lst[5] = { TEXT("байт"), TEXT("КиБ"), TEXT("МиБ"), TEXT("ГиБ"), TEXT("ТиБ") };
        double fSize = static_cast<double>(nSize);
        TCHAR **p = lst;
        while (fSize >= 1024 && p != lst + 4)
        {
            fSize /= 1024.0;
            ++p;
        }
        constexpr int len = 20;
        LPTSTR bufSizeString = new TCHAR[len];
        StringCbPrintf(bufSizeString, len * sizeof(TCHAR), p == lst ? TEXT("%.0f %s") : TEXT("%.2f %s"), fSize, *p);
        cSizeString = bufSizeString;
        delete[] bufSizeString;
    }
    return cSizeString.c_str();
}

LPCTSTR CFileEx::GetCreationDateTimeString() const
{
    if (cTimeString.empty())
    {
        SYSTEMTIME st;
        FILETIME ft;
        FileTimeToLocalFileTime(&ftCreationTime, &ft);
        FileTimeToSystemTime(&ft, &st);
        int datelen = GetDateFormat(LOCALE_USER_DEFAULT, 0, &st, nullptr, nullptr, 0);
        int timelen = GetTimeFormat(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, nullptr, 0);
        LPTSTR bufTimeString = new TCHAR[datelen + timelen];
        GetDateFormat(LOCALE_USER_DEFAULT, 0, &st, nullptr, bufTimeString, datelen);
        bufTimeString[datelen - 1] = L' ';
        GetTimeFormat(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, bufTimeString + datelen, timelen);
        cTimeString = bufTimeString;
        delete[] bufTimeString;
    }
    return cTimeString.c_str();
}

bool operator==(const CFileEx & first, const CFileEx & second)
{
    if (CFileEx::TypeOfComparison == Name)
        // Сравнение имён без учёта регистра
        return !_wcsicmp(first.cName.c_str(), second.cName.c_str());
    else
    {
        if (first.nSize != second.nSize)
            return false;
        if (CFileEx::TypeOfComparison == Contents && (!first.nSize))
            return true;
        if (CFileEx::TypeOfComparison == NameAndContents && _wcsicmp(first.cName.c_str(), second.cName.c_str()))
            return false;
        TCHAR firstBuf[MAX_PATH], secondBuf[MAX_PATH];
        PathCombine(firstBuf, first.cFolder.c_str(), first.cName.c_str());
        PathCombine(secondBuf, second.cFolder.c_str(), second.cName.c_str());
        return CFileEx::IsEqualContents(firstBuf, secondBuf);
    }
}
