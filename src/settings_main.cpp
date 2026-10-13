#include "pinyin/ime_settings.hpp"
#include "pinyin/dictionary_manager.hpp"
#include "pinyin/user_store.hpp"
#include "pinyin/input_statistics.hpp"
#include "pinyin/vocabulary_growth.hpp"
#include "settings_growth_chart.hpp"
#include "settings_actions.hpp"
#include "settings_resource.h"
#include <commdlg.h>
#include <commctrl.h>
#include <array>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>

namespace {
pinyin::VocabularyGrowth chart_growth;
bool chart_available = true;
void select_statistics_tab(HWND window) {
    const bool growth = TabCtrl_GetCurSel(GetDlgItem(window, IDC_STATISTICS_TABS)) == 1;
    ShowWindow(GetDlgItem(window, IDC_DAILY_STATISTICS), growth ? SW_HIDE : SW_SHOW);
    ShowWindow(GetDlgItem(window, IDC_GROWTH_CHART), growth ? SW_SHOW : SW_HIDE);
    SetDlgItemTextW(window, IDC_STATISTICS_RULE, growth ?
        L"按字词去重，含合并个人词库；重复使用或增加读音不计入。统计开始前的日期留空。" :
        L"仅计确认上屏的汉字；速度按有效输入时间计算，排除 30 秒及以上停顿。");
}
std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const auto size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring value(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), value.data(), size);
    return value;
}
bool choose_file(HWND owner, bool save, std::filesystem::path& path, bool& personal) {
    std::array<wchar_t, 32768> filename{};
    if (save) wcscpy_s(filename.data(), filename.size(), L"个人词库.tsv");
    OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = owner;
    dialog.lpstrFile = filename.data(); dialog.nMaxFile = static_cast<DWORD>(filename.size());
    dialog.lpstrFilter = save ? L"个人词库 (*.tsv)\0*.tsv\0\0" :
        L"个人词库（选择次数） (*.tsv)\0*.tsv\0普通词库（排序权重） (*.tsv)\0*.tsv\0\0";
    dialog.nFilterIndex = 1; dialog.lpstrDefExt = L"tsv";
    dialog.lpstrTitle = save ? L"导出个人词库" : L"合并词库 · 请选择对应的文件类型";
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
        (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))) {
        if (CommDlgExtendedError()) throw std::runtime_error("cannot open file selection dialog");
        return false;
    }
    path = filename.data(); personal = dialog.nFilterIndex == 1; return true;
}
void export_personal(HWND window) {
    std::filesystem::path target; bool personal = true;
    if (!choose_file(window, true, target, personal)) return;
    const auto count = pinyin::export_user_dictionary(pinyin::default_user_path(), target, true);
    const auto message = L"已导出 " + std::to_wstring(count) + L" 条个人词库记录。\n\n" + target.wstring();
    MessageBoxW(window, message.c_str(), L"隐私拼音", MB_OK | MB_ICONINFORMATION);
}
void merge_dictionary(HWND window) {
    std::filesystem::path source; bool personal = true;
    if (!choose_file(window, false, source, personal)) return;
    std::wostringstream message;
    if (personal) {
        const auto result = pinyin::merge_user_dictionary(pinyin::default_user_path(), source);
        message << L"已合并个人词库：新增 " << result.added << L" 条，更新 " << result.updated
            << L" 条，未变 " << result.unchanged << L" 条。\n共 " << result.total << L" 条记录。";
        if (!result.backup.empty()) message << L"\n\n原词库备份：\n" << result.backup.wstring();
        message << L"\n\n开启本地学习时，下一轮拼音输入生效。";
    } else {
        const auto inspected = pinyin::read_dictionary_file(source);
        for (const auto& comment : inspected.comments)
            if (comment.find("private-pinyin user dictionary") != std::string::npos)
                throw std::runtime_error("this is a personal dictionary; select the personal dictionary file type");
        pinyin::DictionaryManager manager(pinyin::default_dictionary_directory());
        const auto count = manager.import_file("imported", source);
        message << L"已合并到 imported 辅词库，共 " << count << L" 条记录。\n"
            L"同词同读音取较大权重，下一轮拼音输入生效。\n\n" <<
            (manager.root() / L"imported.tsv").wstring();
        const auto listed = manager.list();
        for (const auto& item : listed) if (item.name == "imported" && !item.enabled)
            message << L"\n\n该辅词库当前已停用，请启用后使用。";
    }
    MessageBoxW(window, message.str().c_str(), L"隐私拼音", MB_OK | MB_ICONINFORMATION);
}
void refresh(HWND window) {
    static std::optional<pinyin::InputStatistics> displayed_input;
    static std::optional<pinyin::VocabularyGrowth> displayed_growth;
    static std::string displayed_date;
    const auto user = pinyin::default_user_path();
    const auto date = pinyin::local_statistics_date();
    const auto list = GetDlgItem(window, IDC_DAILY_STATISTICS);
    std::wstring status = L"自动刷新。新词从安装此功能后开始统计。";
    auto speed = [](const pinyin::InputTotals& total) {
        if (total.active_ms < 1000 || !total.characters) return std::wstring(L"待积累");
        std::wostringstream text; text << std::fixed << std::setprecision(1)
            << pinyin::input_characters_per_minute(total); return text.str() + L" 字/分";
    };
    auto duration = [](std::uint64_t ms) {
        const auto seconds = ms / 1000;
        return std::to_wstring(seconds / 3600) + L"时 " + std::to_wstring(seconds / 60 % 60) + L"分 " +
            std::to_wstring(seconds % 60) + L"秒";
    };
    pinyin::InputStatistics days;
    bool input_available = true;
    try {
        days = pinyin::read_input_statistics(pinyin::input_statistics_path(user));
        const auto total = pinyin::total_input_statistics(days);
        const auto found = days.find(date);
        const auto today = found == days.end() ? pinyin::InputTotals{} : found->second;
        SetDlgItemTextW(window, IDC_STATISTICS_TOTAL, (L"累计：" + std::to_wstring(total.characters) + L" 字 · " +
            std::to_wstring(total.commits) + L" 次上屏").c_str());
        SetDlgItemTextW(window, IDC_STATISTICS_TODAY, (L"今日：" + std::to_wstring(today.characters) + L" 字 · " +
            std::to_wstring(today.commits) + L" 次上屏").c_str());
        SetDlgItemTextW(window, IDC_STATISTICS_SPEED, (L"平均速度：" + speed(total) + L"    今日：" + speed(today)).c_str());
        if (days.empty()) status = L"暂无输入统计。重开使用输入法的应用后开始累计。";
    } catch (const std::exception&) {
        input_available = false; displayed_input.reset();
        SetDlgItemTextW(window, IDC_STATISTICS_TOTAL, L"累计：读取失败");
        SetDlgItemTextW(window, IDC_STATISTICS_TODAY, L"今日：读取失败");
        SetDlgItemTextW(window, IDC_STATISTICS_SPEED, L"输入速度：读取失败");
        status = L"输入统计文件暂不可读，请检查文件后刷新。";
    }
    std::wstring growth_summary;
    try {
        const auto growth = pinyin::read_vocabulary_growth(pinyin::vocabulary_growth_path(user));
        const auto today = growth.days.find(date);
        growth_summary = L"今日新增：" + std::to_wstring(today == growth.days.end() ? 0 : today->second) +
            L" 个字词 · 开始统计后累计新增：" + std::to_wstring(pinyin::total_vocabulary_growth(growth));
        if (!chart_available || growth != chart_growth || date != displayed_date) {
            chart_growth = growth;
            InvalidateRect(GetDlgItem(window, IDC_GROWTH_CHART), nullptr, FALSE);
        }
        chart_available = true;
    } catch (const std::exception&) {
        chart_available = false; displayed_growth.reset();
        growth_summary = L"每日新增词汇：读取失败";
        status = L"词库增长文件暂不可读，请检查文件后刷新。";
        InvalidateRect(GetDlgItem(window, IDC_GROWTH_CHART), nullptr, FALSE);
    }
    if (!displayed_input || days != *displayed_input || !displayed_growth || chart_growth != *displayed_growth ||
        date != displayed_date) {
        ListView_DeleteAllItems(list);
        std::set<std::string> dates;
        for (const auto& day : days) dates.insert(day.first);
        if (chart_available) {
            for (const auto& day : chart_growth.days) dates.insert(day.first);
            if (!chart_growth.since.empty() && date >= chart_growth.since) dates.insert(date);
        }
        int row = 0;
        for (auto item = dates.rbegin(); item != dates.rend(); ++item) {
            const auto found = days.find(*item);
            const auto total = found == days.end() ? pinyin::InputTotals{} : found->second;
            std::wstring added = L"—";
            if (chart_available && !chart_growth.since.empty() && *item >= chart_growth.since) {
                const auto growth = chart_growth.days.find(*item);
                added = std::to_wstring(growth == chart_growth.days.end() ? 0 : growth->second);
            }
            std::array<std::wstring, 6> values{wide(*item), input_available ? std::to_wstring(total.characters) : L"—",
                input_available ? std::to_wstring(total.commits) : L"—", input_available ? duration(total.active_ms) : L"—",
                input_available ? speed(total) : L"—", added};
            LVITEMW entry{}; entry.mask = LVIF_TEXT; entry.iItem = row; entry.pszText = values[0].data();
            const auto index = ListView_InsertItem(list, &entry);
            for (int column = 1; column < static_cast<int>(values.size()); ++column)
                ListView_SetItemText(list, index, column, values[column].data());
            ++row;
        }
        if (input_available) displayed_input = days;
        if (chart_available) displayed_growth = chart_growth;
        displayed_date = date;
    }
    try {
        const auto words = pinyin::read_user_dictionary(user);
        std::set<std::string> texts;
        std::uint64_t selections = 0;
        for (const auto& word : words) { texts.insert(word.first.second); selections += word.second; }
        SetDlgItemTextW(window, IDC_PERSONAL_SUMMARY, (L"个人词库：" + std::to_wstring(texts.size()) + L" 个字词 · " +
            std::to_wstring(words.size()) + L" 条读音\n词库选择次数合计：" + std::to_wstring(selections) +
            L"（含已有及合并记录）\n" + growth_summary).c_str());
    } catch (const std::exception&) {
        SetDlgItemTextW(window, IDC_PERSONAL_SUMMARY,
            (L"个人词库暂不可读，请检查词库格式或文件权限。\n" + growth_summary).c_str());
    }
    SetDlgItemTextW(window, IDC_STATISTICS_STATUS, status.c_str());
}
void initialize_statistics(HWND window) {
    const auto list = GetDlgItem(window, IDC_DAILY_STATISTICS);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    const wchar_t* names[] = {L"日期", L"字数", L"上屏次数", L"有效时间", L"平均速度", L"新增词汇"};
    const int widths[] = {80, 42, 56, 80, 72, 62};
    for (int index = 0; index < 6; ++index) {
        RECT column_units{0, 0, widths[index], 0}; MapDialogRect(window, &column_units);
        LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.pszText = const_cast<wchar_t*>(names[index]); column.cx = column_units.right;
        column.iSubItem = index; ListView_InsertColumn(list, index, &column);
    }
    const auto tabs = GetDlgItem(window, IDC_STATISTICS_TABS);
    SetWindowTextW(tabs, L"统计视图");
    const wchar_t* views[] = {L"每日统计", L"词库增长曲线"};
    for (int index = 0; index < 2; ++index) {
        TCITEMW tab{}; tab.mask = TCIF_TEXT; tab.pszText = const_cast<wchar_t*>(views[index]);
        TabCtrl_InsertItem(tabs, index, &tab);
    }
    select_statistics_tab(window);
    try {
        pinyin::add_vocabulary_growth(pinyin::vocabulary_growth_path(pinyin::default_user_path()),
            pinyin::local_statistics_date(), 0);
    } catch (...) {} // Reading below displays an invalid file without overwriting it.
    SetTimer(window, 1, 3000, nullptr);
}
INT_PTR CALLBACK dialog(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    try {
        switch (message) {
        case WM_INITDIALOG:
            SetDlgItemTextW(window, IDC_VERSION, (L"版本：" + wide(PRIVATE_PINYIN_VERSION)).c_str());
            initialize_statistics(window); refresh(window); return TRUE;
        case WM_ACTIVATE: if (LOWORD(wparam) != WA_INACTIVE) refresh(window); return FALSE;
        case WM_TIMER: if (wparam == 1) refresh(window); return TRUE;
        case WM_DESTROY: KillTimer(window, 1); return TRUE;
        case WM_NOTIFY:
            if (reinterpret_cast<NMHDR*>(lparam)->idFrom == IDC_STATISTICS_TABS &&
                reinterpret_cast<NMHDR*>(lparam)->code == TCN_SELCHANGE) {
                select_statistics_tab(window); return TRUE;
            }
            return FALSE;
        case WM_DRAWITEM:
            if (wparam == IDC_GROWTH_CHART) {
                paint_vocabulary_growth(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam), chart_growth, chart_available);
                return TRUE;
            }
            return FALSE;
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
            case IDC_REFRESH_STATISTICS: refresh(window); return TRUE;
            case IDC_OPEN_DIRECTORY:
                if (FAILED(pinyin::open_user_directory(window))) throw std::runtime_error("cannot open dictionary directory");
                return TRUE;
            case IDC_EXPORT_PERSONAL:
                if (HIWORD(wparam) != BN_CLICKED) return FALSE;
                export_personal(window); return TRUE;
            case IDC_MERGE_DICTIONARY:
                if (HIWORD(wparam) != BN_CLICKED) return FALSE;
                merge_dictionary(window); refresh(window); return TRUE;
            case IDOK: case IDCANCEL: EndDialog(window, 0); return TRUE;
            }
            break;
        }
    } catch (const std::exception& error) {
        const auto error_message = L"操作未完成，请检查词库格式或文件访问权限。\n\n" + wide(error.what());
        MessageBoxW(window, error_message.c_str(), L"隐私拼音", MB_OK | MB_ICONERROR);
        try { refresh(window); } catch (...) {}
    } catch (...) {
        MessageBoxW(window, L"操作未完成。", L"隐私拼音", MB_OK | MB_ICONERROR);
    }
    return FALSE;
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES};
    if (!InitCommonControlsEx(&controls)) return 1;
    return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), nullptr, dialog, 0) == -1 ? 1 : 0;
}
