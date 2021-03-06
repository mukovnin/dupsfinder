#include "stdafx.h"
#include "commctrl.h"
#include "Shellapi.h"
#include "Shlwapi.h"
#include "Shlobj.h"
#include "Results.h"
#include "Searcher.h"

// Отображает в hLV список результатов, хранящийся в vec, после чего очищает vec
VOID FillListView(HWND hLV, std::vector<CFileEx> &vec)
{
    unsigned prevId = 0;
    LVGROUP gr = { sizeof(gr) };
    gr.state = LVGS_COLLAPSIBLE;
    gr.mask = LVGF_GROUPID | LVGF_STATE | LVGF_HEADER;
    TCHAR buf[500];
    DuplicatesType DT = CFileEx::TypeOfComparison;

    SendMessage(hLV, WM_SETREDRAW, (WPARAM)FALSE, 0);

    int index = -1; // Индекс очередного вставляемого элемента
    for (decltype(vec.size()) i = 0; i < vec.size(); ++i)
    {
        CFileEx &file = vec[i];
        unsigned id = file.GetGroupId();
        if (id) // Элемент принадлежит какой-то группе
        {

            if (id != prevId) // Новая группа
            {
                // Подсчёт числа элементов группы
                int k = 0;
                while ((k + i) < vec.size() && vec[i].GetGroupId() == vec[k + i].GetGroupId())
                    ++k;

                // Формирование заголовка группы
                switch (DT)
                {
                case Name:
                    wsprintf(
                                buf,
                                TEXT("Файлы с именем \"%s\" (всего %d файлов)"),
                                vec[i].GetName(),
                                k);
                    break;
                case Contents:
                    wsprintf(
                                buf,
                                TEXT("Файлы размером %s с идентичным содержанием (всего %d файлов)"),
                                vec[i].GetFormattedSizeString(),
                                k);
                    break;
                case NameAndContents:
                    wsprintf(
                                buf,
                                TEXT("Файлы с именем \"%s\" размером %s с идентичным содержанием (всего %d файлов)"),
                                vec[i].GetName(), vec[i].GetFormattedSizeString(),
                                k);
                    break;
                }

                // Вставка группы в список
                gr.pszHeader = buf;
                gr.iGroupId = id;
                ListView_InsertGroup(hLV, -1, &gr);
                prevId = id;
            }

            LPCTSTR text[6] = {	file.GetName(),
                                file.GetFolder(),
                                file.GetFormattedSizeString(),
                                file.GetCreationDateTimeString(),
                                file.IsHidden() ? TEXT("Да") : TEXT("Нет"),
                                file.IsSystem() ? TEXT("Да") : TEXT("Нет")
                              };

            LVITEM item;
            item.pszText = DT == Contents ? const_cast<LPTSTR>(text[0]) : const_cast<LPTSTR>(text[1]);
            item.iSubItem = 0;
            item.iGroupId = id;
            item.iItem = ++index;

            // Ассоциированный с item полный путь к файлу, заканчивающийся двумя нулевыми символами
            // (два нулевых символа нужны для корректного вызова SHFileOperation)
            // сохраняется в item.lParam
            int len = wcslen(text[0]) + wcslen(text[1]);
            LPTSTR fullPath = new TCHAR[len + 3];
            PathCombine(fullPath, text[1], text[0]);
            fullPath[len + 2] = 0;

            item.lParam = (LPARAM)fullPath;
            item.mask = LVIF_TEXT | LVIF_GROUPID | LVIF_PARAM;
            ListView_InsertItem(hLV, &item);

            // Вставка SubItem'ов
            item.iItem = index;
            item.mask = LVIF_TEXT;

            int textIndex[3][4] = { 2, 3, 4, 5,
                                    1, 3, 4, 5,
                                    3, 4, 5, -1
                                  };

            for (int j = 1; j <= 4; ++j)
            {
                int ind = textIndex[DT][j - 1];
                if (ind != -1)
                {
                    item.pszText = const_cast<LPTSTR>(text[ind]);
                    item.iSubItem = j;
                    ListView_SetItem(hLV, &item);
                }
            }
        }
    }

    SendMessage(hLV, WM_SETREDRAW, (WPARAM)TRUE, 0);

    vec.clear();
}

// Возвращает связанный с элементом index списка hLV полный путь к файлу
LPCTSTR GetFilePathFromListView(HWND hLV, int index)
{
    LVITEM item;
    item.iItem = index;
    item.mask = LVIF_PARAM;
    ListView_GetItem(hLV, &item);
    return (LPCTSTR)(item.lParam);
}

// Обработка WM_COMMAND
VOID ResDlg_OnCommand(HWND hDlg, int id, HWND hWndCtl, UINT codeNotify)
{
    HWND hList = GetDlgItem(hDlg, IDC_LIST_RESULTS);
    switch (id)
    {
    case IDCANCEL:
    {
        CoUninitialize();
        EndDialog(hDlg, id);
    }
        break;
    // Удаление отмеченных файлов
    case IDC_BUTTON_DELETECHECKED:
    // Удаление отмеченных файлов в корзину
    case IDC_BUTTON_DELETECHECKEDINRECYCLE:
    {
        int count = ListView_GetItemCount(hList);
        int checkedCount = 0;
        for (int i = count - 1; i >= 0; --i)
            if (ListView_GetCheckState(hList, i))
            {
                ++checkedCount;
                LPCTSTR filePath = GetFilePathFromListView(hList, i);
                if (filePath)
                {
                    bool success;
                    if (id == IDC_BUTTON_DELETECHECKED)
                        success = DeleteFile(filePath);
                    else
                    {
                        SHFILEOPSTRUCT shop;
                        shop.hwnd = 0;
                        shop.pFrom = filePath;
                        shop.pTo = nullptr;
                        shop.wFunc = FO_DELETE;
                        shop.fFlags = FOF_ALLOWUNDO | FOF_NO_UI;
                        success = !(SHFileOperation(&shop));
                    }
                    if (success)
                        ListView_DeleteItem(hList, i);
                    else
                    {
                        TCHAR buf[MAX_PATH + 100];
                        wsprintf(buf, TEXT("Не удалось удалить файл \n %s"), filePath);
                        MessageBox(hDlg, buf, nullptr, MB_ICONEXCLAMATION | MB_OK);
                    }
                }
            }
        if (!checkedCount)
            MessageBox(hDlg, TEXT("Не отмечено флажком ни одного файла."), nullptr, MB_ICONEXCLAMATION | MB_OK);
    }
        break;
    // Открытие папки с выбранным файлом в файловом менеджере
    case IDC_BUTTON_OPENFOLDER:
    {
        int selCount = ListView_GetSelectedCount(hList);
        if (!selCount)
            MessageBox(hDlg, TEXT("Не выбран файл."), nullptr, MB_ICONEXCLAMATION | MB_OK);
        else
        {
            int i;
            int count = ListView_GetItemCount(hList);
            for (i = 0; i < count && ListView_GetItemState(hList, i, LVIS_SELECTED) != LVIS_SELECTED; ++i);
            if (i < count)
            {
                LPCWSTR filePath = GetFilePathFromListView(hList, i);
                if (filePath)
                {
                    ITEMIDLIST *pidl = ILCreateFromPath(filePath);
                    if (pidl)
                    {
                        if (SHOpenFolderAndSelectItems(pidl, 0, 0, 0) != S_OK)
                            MessageBox(hDlg, TEXT("Не удалось открыть папку."), nullptr, MB_ICONEXCLAMATION | MB_OK);
                        ILFree(pidl);
                    }
                }
            }
        }
    }
        break;
    }
}

// Обработка WM_NOTIFY
VOID ResDlg_OnNotify(HWND hWnd, LPNMHDR lpnmh)
{
    if (lpnmh->idFrom == IDC_LIST_RESULTS && lpnmh->code == LVN_DELETEITEM)
    {
        LPNMLISTVIEW lpnmlv = (LPNMLISTVIEW)lpnmh;
        // Удаление строки (содержащей полный путь к файлу), связанной с элементом
        delete[] (LPTSTR)(lpnmlv->lParam);
    }
}

// Инициализация диалога (обработка WM_INITDIALOG, в lParam передаётся указатель на вектор результатов)
VOID ResDlg_OnInit(HWND hDlg, std::vector<CFileEx> *pResults)
{
    CoInitializeEx(nullptr, 0);

    // Настройка вида списка результатов
    HWND hList = GetDlgItem(hDlg, IDC_LIST_RESULTS);
    ListView_SetExtendedListViewStyle(hList, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
    ListView_EnableGroupView(hList, TRUE);
    // Колонки:
    // - если поиск дубликатов по имени, отсутствует колонка "Имя"
    // - если по содержанию, то "Размер"
    // - если по имени и содержанию, отсутствуют обе эти колонки
    LPTSTR ColTitles[6] = { TEXT("Имя"), TEXT("Папка"), TEXT("Размер"), TEXT("Дата создания"), TEXT("Скрытый"), TEXT("Системный") };
    const int ColWidths[6] = { 150, 310, 70, 140, 70, 70 };
    LVCOLUMN col;

    for (int i = 0, k = 0; i < 6; ++i)
    {
        DuplicatesType DT = CFileEx::TypeOfComparison;
        if ((DT == Name && i == 0) || (DT == Contents && i == 2) || (DT == NameAndContents && (i == 0 || i == 2)))
            continue;
        col.pszText = ColTitles[i];
        col.cx = ColWidths[i];
        if (k == 0)
        {
            col.mask = LVCF_TEXT | LVCF_WIDTH;
        }
        else
        {
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            col.iSubItem = k;
        }
        ListView_InsertColumn(hList, k, &col);
        ++k; // k - номер очередного SubItem'а, ассоциированного с колонкой
    }

    // Заполнить список результатами
    FillListView(hList, *pResults);
}

// Оконная процедура диалога со списком результатов
INT_PTR CALLBACK    ResultsDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
    {
        ResDlg_OnInit(hDlg, (std::vector<CFileEx>*)lParam);
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND:
    {
        ResDlg_OnCommand(hDlg, LOWORD(wParam), (HWND)lParam, HIWORD(wParam));
        return (INT_PTR)TRUE;
    }
    case WM_NOTIFY:
        ResDlg_OnNotify(hDlg, (LPNMHDR)lParam);
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}
