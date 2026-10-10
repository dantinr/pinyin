#include "pinyin/ime_settings.hpp"
#include "pinyin/dictionary_manager.hpp"
#include "pinyin/user_store.hpp"
#include "settings_actions.hpp"
#include "settings_resource.h"
#include <commdlg.h>
#include <array>
#include <sstream>

namespace {
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
    const auto settings = pinyin::read_ime_settings(pinyin::default_user_path());
    CheckDlgButton(window, IDC_LEARNING, settings.learning ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, IDC_PUNCTUATION, settings.chinese_punctuation ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, IDC_AUTOMATIC_ENGLISH, settings.automatic_english ? BST_CHECKED : BST_UNCHECKED);
}
INT_PTR CALLBACK dialog(HWND window, UINT message, WPARAM wparam, LPARAM) noexcept {
    try {
        switch (message) {
        case WM_INITDIALOG: refresh(window); return TRUE;
        case WM_ACTIVATE: if (LOWORD(wparam) != WA_INACTIVE) refresh(window); return FALSE;
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
            case IDC_LEARNING:
                if (HIWORD(wparam) != BN_CLICKED) return FALSE;
                pinyin::set_ime_learning(pinyin::default_user_path(), IsDlgButtonChecked(window, IDC_LEARNING) == BST_CHECKED);
                refresh(window); return TRUE;
            case IDC_PUNCTUATION:
                if (HIWORD(wparam) != BN_CLICKED) return FALSE;
                pinyin::set_chinese_punctuation(pinyin::default_user_path(), IsDlgButtonChecked(window, IDC_PUNCTUATION) == BST_CHECKED);
                refresh(window); return TRUE;
            case IDC_OPEN_DIRECTORY:
                if (FAILED(pinyin::open_user_directory(window))) throw std::runtime_error("cannot open dictionary directory");
                return TRUE;
            case IDC_EXPORT_PERSONAL:
                if (HIWORD(wparam) != BN_CLICKED) return FALSE;
                export_personal(window); return TRUE;
            case IDC_MERGE_DICTIONARY:
                if (HIWORD(wparam) != BN_CLICKED) return FALSE;
                merge_dictionary(window); return TRUE;
            case IDC_AUTOMATIC_ENGLISH:
                if (HIWORD(wparam) != BN_CLICKED) return FALSE;
                pinyin::set_automatic_english(pinyin::default_user_path(), IsDlgButtonChecked(window, IDC_AUTOMATIC_ENGLISH) == BST_CHECKED);
                refresh(window); return TRUE;
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
    return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), nullptr, dialog, 0) == -1 ? 1 : 0;
}
