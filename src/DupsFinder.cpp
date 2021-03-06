#include "stdafx.h"
#include "commctrl.h"
#include "windowsx.h"
#include "process.h"
#include "DupsFinder.h"
#include "Searcher.h"
#include "Results.h"


#define MAX_LOADSTRING 100

// Пользовательское сообщение: установлен/снят флажок у элемента дерева
#define UM_CHECKSTATECHANGE (WM_USER + 100)

// Глобальные переменные

HINSTANCE hInst;                    // текущий экземпляр приложения
HANDLE hThread;                     // дескриптор потока, в котором выполняется поиск        
ParamsAndResults SrchParams;        // параметры и результаты поиска

// Предварительные объявления функций

ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE hInstance, int nCmdShow);
LRESULT CALLBACK    WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK    About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
VOID                Dlg_OnCommand(HWND hWnd, int id, HWND hWndCtl, UINT codeNotify);
VOID                Dlg_OnNotify(HWND hWnd, LPNMHDR lpnmh);
VOID                InitDialogControls(HWND hWnd);
BOOL                LoadDrivesList(HWND hTree, HTREEITEM hRoot);
VOID                ExpandItem(HWND hTree, HTREEITEM hItem);
HTREEITEM           InsertItem(HWND hTree, HTREEITEM hParentItem, LPTSTR pText, UINT state);
VOID                UpdateCheckStates(HWND hTree, HTREEITEM hItemChanged, UINT state);
VOID                UpdateItemsStateRecursivelyDown(HWND hTree, HTREEITEM hItem, UINT state);
VOID                UpdateItemsStateRecursivelyUp(HWND hTree, HTREEITEM hItem, UINT state);
VOID                AddFoldersRecursively(std::vector<std::wstring> &vec, HWND hTree, HTREEITEM hRoot);
BOOL                GetParams(HWND hWnd);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                      _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR    lpCmdLine,
                      _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    const INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TREEVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);

    MyRegisterClass(hInstance);

    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_DUPSFINDER));

    MSG msg;

    // Цикл выборки сообщений
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}

// Регистрация класса окна
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra	= DLGWINDOWEXTRA;	// Главное окно создаётся как диалог из ресурса
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_DUPSFINDER));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_BTNFACE + 1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_DUPSFINDER);
    wcex.lpszClassName  = TEXT("DupsFinderWindow");
    wcex.hIconSm        = nullptr;

    return RegisterClassExW(&wcex);
}

// Инициализация экземпляра: создание и показ главного окна
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance; // Сохранить дескриптор экземпляра в глобальной переменной

    HWND hWnd = CreateDialog(hInstance, MAKEINTRESOURCEW(IDD_DUPSFINDER), 0, nullptr);

    if (!hWnd)
        return FALSE;

    InitDialogControls(hWnd);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return TRUE;
}

// Инициализация элементов главного окна, создание панели статуса и индикатора прогресса
VOID InitDialogControls(HWND hWnd)
{
    // Единицы измерения размера
    UINT idComboBoxes[2] = {IDC_COMBO_MINSIZE, IDC_COMBO_MAXSIZE};
    LPCTSTR items[4] = { TEXT("байт"), TEXT("кибибайт"), TEXT("мебибайт"), TEXT("гибибайт") };
    for (UINT id : idComboBoxes)
    {
        HWND hCB = GetDlgItem(hWnd, id);
        for (LPCTSTR item : items)
            ComboBox_AddString(hCB, item);
        ComboBox_SetCurSel(hCB, 0);
    }
    // По умолчанию поиск дубликатов по имени и содержанию с пропуском файлов с атрибутом "Системный"
    Button_SetCheck(GetDlgItem(hWnd, IDC_RADIO_BYNAMEANDCONTENTS), 1);
    Button_SetCheck(GetDlgItem(hWnd, IDC_CHECK_SYSFILES), 1);
    // Вставка корневого элемента дерева и раскрытие его
    HWND hTree = GetDlgItem(hWnd, IDC_TREE_FILESYSTEM);
    TreeView_Expand(hTree, InsertItem(hTree, nullptr, TEXT("Компьютер"), 0), TVE_EXPAND);
    // Создание панели статуса
    HWND hwndStatus = CreateWindowEx(
                0,
                STATUSCLASSNAME,
                nullptr,
                WS_CHILD | WS_VISIBLE,
                0, 0, 0, 0,
                hWnd,
                (HMENU)IDC_STATUS,
                hInst,
                nullptr);
    // Разделение панели статуса на части
    RECT cr;
    GetClientRect(hWnd, &cr);
    INT parts[2] = { cr.right * 2 / 3, cr.right };
    SendMessage(hwndStatus, SB_SETPARTS, 2, (LPARAM)(&parts));
    // Вставка индикатора прогресса в правую часть панели статуса
    CreateWindowEx(
                0,
                PROGRESS_CLASS,
                nullptr,
                WS_CHILD,
                parts[0] + 20, 5, cr.right / 3 - 40, 15,
                hwndStatus,
                (HMENU)IDC_PROGRESS,
                hInst,
                nullptr);
    // Начальный текст панели статуса
    SendMessage(
                hwndStatus,
                SB_SETTEXT,
                MAKEWPARAM(0, SBT_NOBORDERS),
                (LPARAM)TEXT("Отметьте папки, настройте параметры поиска и нажмите кнопку \"Начать поиск\"."));
}

// Обработка сообщений WM_COMMAND, поступающих в оконную процедуру главного окна
VOID Dlg_OnCommand(HWND hWnd, int id, HWND hWndCtl, UINT codeNotify)
{
    static HWND hEdit = GetDlgItem(hWnd, IDC_EDIT_MASK);
    static HWND hList = GetDlgItem(hWnd, IDC_LIST_MASKS);

    switch (id)
    {
    case IDC_CHECK_USEMASKS:
    {
        // Включение/выключение элементов управления для ввода масок
        BOOL bUseMasks = Button_GetCheck(hWndCtl);
        UINT idControls[5] = { IDC_EDIT_MASK, IDC_LIST_MASKS, IDC_BUTTON_ADDMASK, IDC_BUTTON_CLEARSELMASK, IDC_BUTTON_CLEARLIST };
        for (UINT ctrl : idControls)
            EnableWindow(GetDlgItem(hWnd, ctrl), bUseMasks);
    }
        break;
    case IDC_CHECK_MINSIZE:
    case IDC_CHECK_MAXSIZE:
    {
        // Включение/выключение элементов управления для ввода ограничений по размеру
        UINT idControls[2][2] = { IDC_EDIT_MINSIZE, IDC_COMBO_MINSIZE, IDC_EDIT_MAXSIZE, IDC_COMBO_MAXSIZE };
        for (UINT i = 0; i < 2; ++i)
            EnableWindow(GetDlgItem(hWnd, idControls[id == IDC_CHECK_MAXSIZE][i]), Button_GetCheck(hWndCtl));
    }
        break;
    case IDC_CHECK_MINDATE:
    case IDC_CHECK_MAXDATE:
    {
        // Включение/выключение элементов управления для ввода ограничений по дате создания
        UINT idControls[2] = { IDC_DATETIMEPICKER_MINDATE, IDC_DATETIMEPICKER_MAXDATE };
        EnableWindow(GetDlgItem(hWnd, idControls[id == IDC_CHECK_MAXDATE]), Button_GetCheck(hWndCtl));
    }
        break;
    case IDC_BUTTON_ADDMASK:
    {
        // Добавление новой маски в список
        DWORD len = Edit_GetTextLength(hEdit);
        if (len++)
        {
            LPTSTR buf = new TCHAR[len];
            Edit_GetText(hEdit, buf, len);
            ListBox_AddString(hList, buf);
            Edit_SetText(hEdit, TEXT(""));
            delete[] buf;
        }
    }
        break;
    case IDC_BUTTON_CLEARLIST:
    {
        // Очистка списка масок
        ListBox_ResetContent(hList);
    }
        break;
    case IDC_BUTTON_CLEARSELMASK:
    {
        // Удаление выбранной в списке маски
        int index = ListBox_GetCurSel(hList);
        if (index != LB_ERR)
            ListBox_DeleteString(hList, index);
    }
        break;
    case IDC_BUTTON_START:
    {
        // Ввод и проверка параметров, в случае успеха - запуск поиска в параллельном потоке
        if (GetParams(hWnd))
        {
            EnableWindow(hWndCtl, FALSE);
            hThread = (HANDLE) _beginthread((_beginthread_proc_type)&ThreadProc, 0, &SrchParams);
        }
    }
        break;
    case IDC_BUTTON_CANCEL:
    {
        // Запрос завершения потока
        SetEvent(SrchParams.event);
    }
        break;
    case IDM_ABOUT:
    {
        DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
    }
        break;
    case IDM_EXIT:
    {
        DestroyWindow(hWnd);
    }
        break;
    }
}

// Обработка сообщений WM_NOTIFY, поступающих в оконную процедуру главного окна
VOID Dlg_OnNotify(HWND hWnd, LPNMHDR lpnmh)
{
    if (lpnmh->idFrom == IDC_TREE_FILESYSTEM)
    {
        switch (lpnmh->code)
        {
        case NM_CLICK:
        {
            // Если произошёл щелчок мышью в области флажка, шлём пользовательское сообщение UM_CHECKSTATECHANGE
            TVHITTESTINFO ht = { 0 };
            DWORD dwpos = GetMessagePos();
            ht.pt.x = GET_X_LPARAM(dwpos);
            ht.pt.y = GET_Y_LPARAM(dwpos);
            MapWindowPoints(HWND_DESKTOP, lpnmh->hwndFrom, &ht.pt, 1);
            TreeView_HitTest(lpnmh->hwndFrom, &ht);
            if (TVHT_ONITEMSTATEICON & ht.flags)
                PostMessage(hWnd, UM_CHECKSTATECHANGE, 0, (LPARAM)ht.hItem);
        }
            break;
        case TVN_ITEMEXPANDING:
        {
            // Загружаем "детей" раскрываемого узла, если он ещё не раскрывался
            LPNMTREEVIEW lpnmtv = (LPNMTREEVIEW)lpnmh;
            if (!(lpnmtv->itemNew.state & TVIS_EXPANDEDONCE))
                ExpandItem(GetDlgItem(hWnd, IDC_TREE_FILESYSTEM), lpnmtv->itemNew.hItem);
        }
            break;
        case TVN_DELETEITEM:
        {
            // Удаляем связанную с элементом дерева строку, хранящую полный путь к нему
            LPNMTREEVIEW lpnmtv = (LPNMTREEVIEW)lpnmh;
            delete[](LPTSTR)(lpnmtv->itemOld.lParam);
        }
            break;
        }
    }
}

// Оконная процедура главного окна приложения
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_NOTIFY:
    {
        Dlg_OnNotify(hWnd, (LPNMHDR)lParam);
    }
        break;
    case WM_COMMAND:
    {
        Dlg_OnCommand(hWnd, LOWORD(wParam), (HWND)lParam, HIWORD(wParam));
    }
        break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
    }
        break;
    case WM_DESTROY:
    {
        PostQuitMessage(0);
    }
        break;
    case UM_CHECKSTATECHANGE:           // Пользователь установил/снял флажок у элемента дерева
    {
        HTREEITEM hItemChanged = (HTREEITEM)lParam;
        HWND hTree = GetDlgItem(hWnd, IDC_TREE_FILESYSTEM);
        UINT state = TreeView_GetCheckState(hTree, hItemChanged);
        // Обновляем флажки у элементов, связанных с этим
        UpdateCheckStates(hTree, hItemChanged, state);
    }
        break;
    case UM_SEARCHPROGRESSCHANGED:      // Сообщение от рабочего потока о количестве просмотренных папок
    {
        HWND hStatus = GetDlgItem(hWnd, IDC_STATUS);
        TCHAR buf[100];
        wsprintf(buf, TEXT("Поиск файлов. Просмотрено %d папок..."), wParam);
        SendMessage(hStatus, SB_SETTEXT, MAKEWPARAM(0, SBT_NOBORDERS), (LPARAM)buf);
    }
        break;
    case UM_COMPARISONSTARTED:          // Сообщение от рабочего потока о начале анализа найденных файлов
    {
        ShowWindow(GetWindow(GetDlgItem(hWnd, IDC_STATUS), GW_CHILD), SW_SHOW);
    }
        break;
    case UM_COMPARISONPROGRESSCHANGED:  // Сообщение от рабочего потока о количестве проанализированных файлов
    {
        HWND hStatus = GetDlgItem(hWnd, IDC_STATUS);
        TCHAR buf[100];
        wsprintf(buf, TEXT("Поиск дубликатов. Проанализировано %d из %d файлов..."), wParam, lParam);
        SendMessage(hStatus, SB_SETTEXT, MAKEWPARAM(0, SBT_NOBORDERS), (LPARAM)buf);
        SendMessage(GetWindow(hStatus, GW_CHILD), PBM_SETPOS, 100 * wParam / lParam, 0);
    }
        break;
    case UM_THREADFINISHED:             // Сообщение от рабочего потока о завершении работы
    {
        // Если lParam == -1, то поток завершился по причине запроса его отмены пользователем.
        // Если поток доработал до конца, в wParam передаётся количество найденных групп дубликатов.

        // Скрытие индикатора прогресса
        ShowWindow(GetWindow(GetDlgItem(hWnd, IDC_STATUS), GW_CHILD), SW_HIDE);
        // Закрытие дескриптора потока, установка события в несигнальное состояние
        CloseHandle(hThread);
        ResetEvent(SrchParams.event);
        // Делаем доступной кнопку начала поиска
        EnableWindow(GetDlgItem(hWnd, IDC_BUTTON_START), TRUE);
        // Текст статуса
        LPTSTR pText;
        if (lParam == -1)
            pText = TEXT("Поиск отменён.");
        else
        {
            if (wParam)
            {
                TCHAR buf[100];
                wsprintf(buf, TEXT("Поиск завершён. Найдено %d групп дубликатов."), wParam);
                pText = buf;
            }
            else
                pText = TEXT("Поиск завершён. Дубликатов не найдено.");
        }
        SendMessage(GetDlgItem(hWnd, IDC_STATUS), SB_SETTEXT, MAKEWPARAM(0, SBT_NOBORDERS), (LPARAM)pText);
        if (lParam == -1)
            MessageBox(hWnd, TEXT("Поиск был отменён."), TEXT("Отмена"), MB_ICONINFORMATION | MB_OK);
        else
        {
            if (wParam)
                // Показ диалога со списком результатов с передачей ему указателя на вектор с информацией о результатах
                DialogBoxParam(hInst, MAKEINTRESOURCE(IDD_RESULTS), hWnd, ResultsDlgProc, (LPARAM)(&(SrchParams.results)));
            else
                MessageBox(
                            hWnd,
                            TEXT("Не найдено ни одного файла, удовлетворяющего заданным условиям."),
                            TEXT("Файлы не найдены"),
                            MB_ICONINFORMATION | MB_OK);
        }
    }
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Обработчик сообщений для окна "О программе"
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

// Загружает буквы дисков в дерево hTree как дочерние элементы корневого элемента hRoot
BOOL LoadDrivesList(HWND hTree, HTREEITEM hRoot)
{
    DWORD szBuf = GetLogicalDriveStrings(0, nullptr);
    if (!szBuf)
        return FALSE;
    TCHAR *buf, *p;
    buf = p = new TCHAR[szBuf + 1];
    GetLogicalDriveStrings(szBuf, p);
    do
    {
        p[2] = 0; // Удаление "\" в "C:\", "D:\" и т.д.
        InsertItem(hTree, hRoot, p, TreeView_GetCheckState(hTree, hRoot));
        p += 4;
    } while (*p);
    delete[] buf;
    return TRUE;
}

// Вставляет в дерево hTree дочерний для hParentItem элемент с текстом pText и состоянием state
HTREEITEM InsertItem(HWND hTree, HTREEITEM hParentItem, LPTSTR pText, UINT state)
{
    TVINSERTSTRUCT tvis;
    LPTSTR path = nullptr;
    if (hParentItem)
    {
        // Родительский элемент существует, выясняем ассоциированный с ним путь
        path = new TCHAR[MAX_PATH];
        tvis.item.mask = TVIF_PARAM;
        tvis.item.hItem = hParentItem;
        TreeView_GetItem(hTree, &tvis.item);
        LPTSTR pred = (LPTSTR)(tvis.item.lParam);
        // Если с родительским элементом ассоциирован путь
        // (справедливо для всех родительских элементов, кроме корневого узла "Компьютер"),
        // формируем путь для текущего элемента с учётом пути родительского
        if (pred)
        {
            wcscpy(path, pred);
            wcscat(path, TEXT("\\"));
        }
        else
            path[0] = 0;
        wcscat(path, pText);
    }
    tvis.item.lParam = (LPARAM)path;
    tvis.hInsertAfter = TVI_SORT;
    tvis.item.cChildren = 1; // По умолчанию считаем, что узел развернуть будет можно
    tvis.hParent = hParentItem ? hParentItem : TVI_ROOT;
    tvis.item.pszText = pText;
    tvis.item.mask = TVIF_TEXT | TVIF_CHILDREN | TVIF_PARAM;
    HTREEITEM hItem = TreeView_InsertItem(hTree, &tvis);
    TreeView_SetCheckState(hTree, hItem, state);
    return hItem;
};

// Разворачивает узел hItem в дереве hTree
VOID ExpandItem(HWND hTree, HTREEITEM hItem)
{
    BOOL hasChildrens = FALSE;
    TVITEM item;
    item.mask = TVIF_PARAM;
    item.hItem = hItem;
    TreeView_GetItem(hTree, &item);
    if (!item.lParam)
        hasChildrens = LoadDrivesList(hTree, hItem);
    else
    {
        WIN32_FIND_DATA fd;
        LPTSTR path = (LPTSTR)item.lParam;
        wcscat(path, TEXT("\\*"));  // Временно добавляем к пути папки "\*"
        HANDLE hFind = FindFirstFile(path, &fd);
        path[wcslen(path) - 2] = 0; // ...и убираем
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY && wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L".."))
                {
                    InsertItem(hTree, hItem, fd.cFileName, TreeView_GetCheckState(hTree, hItem));
                    hasChildrens = TRUE;
                }
            while (FindNextFile(hFind, &fd));
            FindClose(hFind);
        }
    }
    item.mask = TVIF_CHILDREN;
    item.cChildren = hasChildrens == TRUE;  // Если загрузить вложенные элементы не удалось, убираем "+" около родительского узла
    TreeView_SetItem(hTree, &item);
}

// Устанавливает в дереве hTree у всех дочерних элементов такое же состояние флажка state, как и у родительского элемента hItem 
VOID UpdateItemsStateRecursivelyDown(HWND hTree, HTREEITEM hItem, UINT state)
{
    HTREEITEM hChildItem = TreeView_GetChild(hTree, hItem);
    if (hChildItem)
    {
        TreeView_SetCheckState(hTree, hChildItem, state);
        UpdateItemsStateRecursivelyDown(hTree, hChildItem, state);
        while (hChildItem = TreeView_GetNextSibling(hTree, hChildItem))
        {
            TreeView_SetCheckState(hTree, hChildItem, state);
            UpdateItemsStateRecursivelyDown(hTree, hChildItem, state);
        }
    }
}

// В дереве hTree приводит в соответствие состояния родительских элементов с изменившимся состоянием state их дочернего элемента hItem 
VOID UpdateItemsStateRecursivelyUp(HWND hTree, HTREEITEM hItem, UINT state)
{
    HTREEITEM hParentItem = TreeView_GetParent(hTree, hItem);
    if (hParentItem)
    {
        BOOL NeedToChangeParent = TRUE;
        if (state == 1)
        {
            // У hChildItem был установлен флажок.
            // Проверяем: если у всех его братьев уже был установлен флажок,
            // то устанавливаем флажок и у родителя.
            HTREEITEM hChildItem = TreeView_GetChild(hTree, hParentItem);
            do
            {
            } while (TreeView_GetCheckState(hTree, hChildItem) == state && (hChildItem = TreeView_GetNextSibling(hTree, hChildItem)));
            NeedToChangeParent = (hChildItem == nullptr);
        }
        // У hChildItem был снят флажок.
        // Рекурсивно снимаем флажки у родительских элементов.
        // (Состояние "частично выбран" не предусмотрено.)
        if (NeedToChangeParent)
        {
            TreeView_SetCheckState(hTree, hParentItem, state);
            UpdateItemsStateRecursivelyUp(hTree, hParentItem, state);
        }
    }
}

// Выполняет обновление состояний элементов дерева hTree в результате установки/снятия (state) флажка у элемента hItemChanged 
VOID UpdateCheckStates(HWND hTree, HTREEITEM hItemChanged, UINT state)
{
    UpdateItemsStateRecursivelyDown(hTree, hItemChanged, state);
    UpdateItemsStateRecursivelyUp(hTree, hItemChanged, state);
}

// Анализируя дерево, формирует список папок vec, подлежащих рекурсивному просмотру в ходе поиска
VOID AddFoldersRecursively(std::vector<std::wstring> &vec, HWND hTree, HTREEITEM hRoot)
{
    TVITEM item;
    item.mask = TVIF_PARAM;
    item.hItem = hRoot;
    TreeView_GetItem(hTree, &item);
    // Если папка отмечена, добавляем связанный с ней путь в список и дальше не идём,
    // иначе выполняем рекурсивный проход по дереву, пока не встретим отмеченную папку или лист дерева
    if (TreeView_GetCheckState(hTree, hRoot) && item.lParam)
        vec.push_back((LPTSTR)item.lParam);
    else
    {
        HTREEITEM hChild = TreeView_GetChild(hTree, hRoot);
        if (hChild)
        {
            do
            {
                AddFoldersRecursively(vec, hTree, hChild);
            } while (hChild = TreeView_GetNextSibling(hTree, hChild));
        }
    }
}

// Проверяет параметры поиска, введённые в главном окне hWnd
// Если всё в порядке, заполняет соответствующие поля в глобальной структуре SrchParams и возвращает TRUE
BOOL GetParams(HWND hWnd)
{
    // Папки
    SrchParams.folders.clear();
    HWND hTV = GetDlgItem(hWnd, IDC_TREE_FILESYSTEM);
    AddFoldersRecursively(SrchParams.folders, hTV, TreeView_GetRoot(hTV));
    if (SrchParams.folders.empty())
    {
        MessageBox(hWnd, TEXT("Не выбрано ни одной папки."), nullptr, MB_ICONEXCLAMATION | MB_OK);
        return FALSE;
    }
    // Окно для уведомлений
    SrchParams.window = hWnd;
    // Тип поиска
    UINT idRadioBtns[3] = { IDC_RADIO_BYNAME, IDC_RADIO_BYCONTENTS, IDC_RADIO_BYNAMEANDCONTENTS };
    UINT i = 0;
    while (Button_GetCheck(GetDlgItem(hWnd, idRadioBtns[i])) != 1)
        ++i;
    SrchParams.dupType = (DuplicatesType)i;
    // Маски
    SrchParams.masks.clear();
    if (Button_GetCheck(GetDlgItem(hWnd, IDC_CHECK_USEMASKS)))
    {
        HWND hLB = GetDlgItem(hWnd, IDC_LIST_MASKS);
        int count = ListBox_GetCount(hLB);
        for (int i = 0; i < count; ++i)
        {
            int len = ListBox_GetTextLen(hLB, i);
            LPTSTR buf = new TCHAR[len + 1];
            ListBox_GetText(hLB, i, buf);
            SrchParams.masks.push_back(buf);
            delete[] buf;
        }
    }
    // Размеры
    UINT idSizeControls[2][3] = { IDC_CHECK_MINSIZE, IDC_EDIT_MINSIZE, IDC_COMBO_MINSIZE,
                                  IDC_CHECK_MAXSIZE, IDC_EDIT_MAXSIZE, IDC_COMBO_MAXSIZE };
    for (UINT i = 0; i < 2; ++i)
    {
        SrchParams.useSize[i] = Button_GetCheck(GetDlgItem(hWnd, idSizeControls[i][0]));
        if (SrchParams.useSize[i])
        {
            BOOL success;
            UINT size = GetDlgItemInt(hWnd, idSizeControls[i][1], &success, FALSE);
            if (!success)
            {
                TCHAR errBuf[100];
                wsprintf(
                            errBuf,
                            TEXT("Значение, указанное в текстовом поле \"%s размер\", не удалось преобразовать в число."),
                            i ? TEXT("максимальный") : TEXT("минимальный"));
                MessageBox(hWnd, errBuf, nullptr, MB_ICONEXCLAMATION | MB_OK);
                return FALSE;
            }
            SrchParams.fileSize[i] = size;
            int index = ComboBox_GetCurSel(GetDlgItem(hWnd, idSizeControls[i][2]));
            for (INT j = 0; j < index; ++j)
                SrchParams.fileSize[i] *= 1024;
        }
    }
    if (SrchParams.useSize[0] && SrchParams.useSize[1] && SrchParams.fileSize[0] > SrchParams.fileSize[1])
    {
        MessageBox(hWnd, TEXT("Указанный минимальный размер файла больше максимального."), nullptr, MB_ICONEXCLAMATION | MB_OK);
        return FALSE;
    }
    // Время создания
    UINT idDateControls[2][2] = { IDC_CHECK_MINDATE, IDC_DATETIMEPICKER_MINDATE, IDC_CHECK_MAXDATE, IDC_DATETIMEPICKER_MAXDATE };
    for (UINT i = 0; i < 2; ++i)
    {
        SrchParams.useTime[i] = Button_GetCheck(GetDlgItem(hWnd, idDateControls[i][0]));
        if (SrchParams.useTime[i])
        {
            SYSTEMTIME st;
            DateTime_GetSystemtime(GetDlgItem(hWnd, idDateControls[i][1]), &st);
            if (i)
                st.wHour = 23, st.wMinute = 59, st.wSecond = 59, st.wMilliseconds = 999;
            else
                st.wHour = st.wMinute = st.wSecond = st.wMilliseconds = 0;
            FILETIME ft;
            SystemTimeToFileTime(&st, &ft);
            LocalFileTimeToFileTime(&ft, &SrchParams.fileTime[i]);
        }
    }
    if (SrchParams.useTime[0] && SrchParams.useTime[1] && CompareFileTime(&SrchParams.fileTime[0], &SrchParams.fileTime[1]) == 1)
    {
        MessageBox(hWnd, TEXT("Указанная минимальная дата создания файла больше максимальной."), nullptr, MB_ICONEXCLAMATION | MB_OK);
        return FALSE;
    }
    // Исключаемые атрибуты
    UINT attrCheckBoxes[2] = { IDC_CHECK_HIDDENFILES, IDC_CHECK_SYSFILES };
    DWORD attr[2] = { FILE_ATTRIBUTE_HIDDEN, FILE_ATTRIBUTE_SYSTEM };
    SrchParams.exclAttributes = 0;
    for (UINT i = 0; i < 2; ++i)
        if (Button_GetCheck(GetDlgItem(hWnd, attrCheckBoxes[i])) == 1)
            SrchParams.exclAttributes |= attr[i];
    // Событие "отмена"
    if (!SrchParams.event)
        SrchParams.event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    return TRUE;
}
