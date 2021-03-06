#pragma once

#include "windows.h"
#include <string>

// Тип поиска дубликатов:
// Name - по имени
// Contents - по содержанию
// NameAndContents - по имени и содержанию
typedef enum { Name, Contents, NameAndContents } DuplicatesType;

// В экземпляры класса CFileEx копируются данные из WIN32_FIND_DATA находимых файлов,
// если файлы удовлетворяют заданным пользователем ограничениям.
// Не все из таких файлов могут после анализа оказаться в той или иной группе дубликатов,
// поэтому строки, возвращаемые GetFormattedSizeString() и GetCreationDateTimeString(),
// создаются не сразу при создании экземпляра, а при первом вызове этих методов.
// Класс снабжён оператором "==", сравнивающим файлы в соответствии с заданным типом поиска
class CFileEx
{
    friend bool operator==(const CFileEx &first, const CFileEx &second);
private:
    FILETIME ftCreationTime;
    DWORD dwFileAttributes;
    ULONGLONG nSize;
    // Номер группы дубликатов
    UINT nId = 0;
    std::wstring cName;
    std::wstring cFolder;
    std::wstring mutable cTimeString;
    std::wstring mutable cSizeString;
    static bool IsEqualContents(LPCTSTR firstFullName, LPCTSTR secondFullName); // Сравнивает содержимое файлов
public:
    static DuplicatesType TypeOfComparison;
    CFileEx(FILETIME CreationTime, DWORD FileAttributes, ULONGLONG Size, LPCTSTR Name, LPCTSTR Folder);
    CFileEx(const CFileEx &FileEx) = default;
    CFileEx(CFileEx &&FileEx) = default;
    CFileEx& operator=(const CFileEx &FileEx) = default;
    CFileEx& operator=(CFileEx &&FileEx) = default;
    ~CFileEx();
    ULONGLONG GetSize() const { return nSize; }
    UINT GetGroupId() const { return nId; }
    VOID SetGroupId(UINT id) { nId = id; }
    LPCTSTR GetFolder() const { return cFolder.c_str(); }
    LPCTSTR GetName() const { return cName.c_str(); }
    // Возвращает строку, содержащую размер файла в удобочитаемом виде
    LPCTSTR GetFormattedSizeString() const;
    // Возвращает строку, содержащую дату и время создания файла
    LPCTSTR GetCreationDateTimeString() const;
    BOOL IsHidden() const { return dwFileAttributes & FILE_ATTRIBUTE_HIDDEN; }
    BOOL IsSystem() const { return dwFileAttributes & FILE_ATTRIBUTE_SYSTEM; }
};

bool operator==(const CFileEx &first, const CFileEx &second);
