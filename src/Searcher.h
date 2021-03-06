#pragma once

#include "windows.h"
#include <string>
#include <vector>
#include "CFileEx.h"

// Пользовательские сообщения, отправляемые из потока, в котором происходит поиск и анализ файлов

// Обновление количества просмотренных папок (передаётся в lParam)
#define UM_SEARCHPROGRESSCHANGED		(WM_USER + 101)
// Оповещение о начале анализа файлов
#define UM_COMPARISONSTARTED			(WM_USER + 102)
// Обновление количества проанализированных файлов (в lParam - число проанализированных, в wParam - общее число)
#define UM_COMPARISONPROGRESSCHANGED            (WM_USER + 103)
// Оповещение о завершении потока
// Если lParam == -1, поток отменён пользователем
// Иначе в wParam передаётся число найденных групп дубликатов
#define UM_THREADFINISHED			(WM_USER + 104)

// Поточная функция, получающая указатель на структуру типа ParamsAndResults
DWORD WINAPI ThreadProc(LPVOID lpParams);

// Структура, используемая в ходе поиска
// Содержит параметры и результаты поиска
typedef struct 
{
    // Окно, которому следует посылать сообщения о ходе поиска
    HWND window;
    // Событие, устанавливаемое пользователем в сигнальное состояние, если нужно прервать работу
    HANDLE event;
    // Тип поиска дубликатов
    DuplicatesType dupType;
    // Папки, в которых следует рекурсивно произвести поиск
    std::vector<std::wstring> folders;
    // Список масок
    std::vector<std::wstring> masks;
    // Список результатов
    std::vector<CFileEx> results;
    // Ограничения по минимальной и максимальной дате создания файла (0 - мин, 1 - макс)
    FILETIME fileTime[2];
    // Признак использования ограничения по дате создания
    BOOL useTime[2];
    // Ограничения по минимальному и максимальному размеру файла (0 - мин, 1 - макс)
    ULONGLONG fileSize[2];
    // Признак использования ограничения по размеру
    BOOL useSize[2];
    // Исключаемые атрибуты
    DWORD exclAttributes;
} ParamsAndResults;
