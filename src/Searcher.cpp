#include "stdafx.h"
#include "Shlwapi.h"
#include <algorithm>
#include "Searcher.h"

// Рекурсивно ищет файлы, удовлетворяющие заданным в *lpParams ограничениям, в папке path
// При заходе во вложенную папку увеличивает fcount
bool FindFilesRecursively(ParamsAndResults *lpParams, const std::wstring &path, int &fcount)
{
    // Событие находится в сигнальном состоянии, пользователь отменил поиск, выходим
    if (WaitForSingleObject(lpParams->event, 0) == WAIT_OBJECT_0)
        return false;
    WIN32_FIND_DATA fd;
    std::wstring extPath = path + TEXT("\\*");
    HANDLE hFind = FindFirstFile(extPath.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L".."))
                {
                    if (!FindFilesRecursively(lpParams, path + TEXT("\\") + fd.cFileName, fcount))
                    {
                        FindClose(hFind);
                        return false;
                    }
                    ++fcount;
                    if (!(fcount % 10))
                        PostMessage(lpParams->window, UM_SEARCHPROGRESSCHANGED, fcount, 0);
                }
            }
            else
            {
                ULONGLONG size;
                // Если заданы маски, имя найденного файла должно удовлетворять хотя бы одной маске
                std::vector<std::wstring> &masks = lpParams->masks;
                bool accept = masks.empty();
                for (decltype(masks.size()) i = 0; i < masks.size() && !accept; ++i)
                    accept = accept || PathMatchSpec(fd.cFileName, masks[i].c_str());
                if (accept)
                {
                    // Проверка, удовлетворяет ли файл ограничениям по размеру, дате создания и атрибутам
                    size = ((ULONGLONG)(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
                    accept = !(lpParams->useSize[0] && size < lpParams->fileSize[0])
                            && !(lpParams->useSize[1] && size > lpParams->fileSize[1])
                            && !(lpParams->useTime[0] && CompareFileTime(&fd.ftCreationTime, &(lpParams->fileTime[0])) == -1)
                            && !(lpParams->useTime[1] && CompareFileTime(&fd.ftCreationTime, &(lpParams->fileTime[1])) == 1)
                            && !(fd.dwFileAttributes & lpParams->exclAttributes);
                }
                if (accept)
                    // Файл удовлетворяет ограничениям, сохраняем его в списке для дальнейшего анализа
                    lpParams->results.emplace_back(fd.ftCreationTime, fd.dwFileAttributes, size, fd.cFileName, path.c_str());
            }
        while (FindNextFile(hFind, &fd));
        FindClose(hFind);
    }
    return true;
}

// Поточная функция
DWORD WINAPI ThreadProc(LPVOID lpParams)
{
    ParamsAndResults *pParams = (ParamsAndResults*)(lpParams);
    int cFolders = 0;
    CFileEx::TypeOfComparison = pParams->dupType;

    // В каждой папке из списка pParams->folders рекурсивно ищутся файлы, удовлетворяющие заданным в *pParams ограничениям
    for (auto& ws : pParams->folders)
    {
        if (!FindFilesRecursively(pParams, ws, cFolders))
        {
            pParams->results.clear();
            PostMessage(pParams->window, UM_THREADFINISHED, 0, -1);
            return 0;
        }
    }

    // Начало поиска дубликатов
    PostMessage(pParams->window, UM_COMPARISONSTARTED, 0, 0);

    std::vector<CFileEx> &vec = pParams->results;
    // Сортировка файлов по имени и, если нужно, устойчивая сортировка по размеру
    std::sort(
                vec.begin(),
                vec.end(),
                [](const CFileEx &first, const CFileEx &second) { return _wcsicmp(first.GetName(), second.GetName()) < 0; });
    if (CFileEx::TypeOfComparison == Contents)
        std::stable_sort(
                    vec.begin(),
                    vec.end(),
                    [](const CFileEx &first, const CFileEx &second) { return first.GetSize() < second.GetSize(); });
    unsigned size = vec.size();
    unsigned group = 0; // Текущий номер группы
    if (size > 1)
    {
        for (decltype(vec.size()) i = 0; i < size - 1; ++i)
        {
            if (!(i % 10))
                PostMessage(pParams->window, UM_COMPARISONPROGRESSCHANGED, i, size);
            bool found = false; // Признак того, что для элемента vec[i] был найден хотя бы один дубликат
            ++group;
            for (decltype(vec.size()) j = i + 1; j < size; ++j)
            {
                if (vec[j].GetGroupId() == 0 && vec[i] == vec[j])
                {
                    found = true;
                    vec[j].SetGroupId(group);
                }
            }
            if (found)
                vec[i].SetGroupId(group);
            else
                --group;
            // Если пользователь отменил поиск, очищаем вектор, уведомляем окно и выходим
            if (WaitForSingleObject(pParams->event, 0) == WAIT_OBJECT_0)
            {
                vec.clear();
                PostMessage(pParams->window, UM_THREADFINISHED, 0, -1);
                return 0;
            }
        }
        if (!group) // Текущий номер группы не увеличился, т.е. не найдено ни одной группы дубликатов
            vec.clear();
    }
    // Устойчивая сортировка по номеру группы
    std::stable_sort(
                vec.begin(),
                vec.end(),
                [](const CFileEx &first, const CFileEx &second) { return first.GetGroupId() < second.GetGroupId(); });

    // Успешно закончили, отправляем сообщение об этом с количеством сформированных групп
    PostMessage(pParams->window, UM_THREADFINISHED, group, 0);
    return 0;
}
