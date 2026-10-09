#include "pinyin/ime_settings.hpp"
#include "settings_actions.hpp"
#include "settings_resource.h"

namespace {
void refresh(HWND window) {
    const auto settings = pinyin::read_ime_settings(pinyin::default_user_path());
    CheckDlgButton(window, IDC_LEARNING, settings.learning ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, IDC_PUNCTUATION, settings.chinese_punctuation ? BST_CHECKED : BST_UNCHECKED);
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
            case IDOK: case IDCANCEL: EndDialog(window, 0); return TRUE;
            }
            break;
        }
    } catch (...) {
        MessageBoxW(window, L"无法保存设置或打开目录，请检查个人目录的访问权限。", L"隐私拼音", MB_OK | MB_ICONERROR);
        try { refresh(window); } catch (...) {}
    }
    return FALSE;
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), nullptr, dialog, 0) == -1 ? 1 : 0;
}
