// SPDX-License-Identifier: Apache-2.0
#include <Tactility/app/dialog/Dialog.h>

#include <localisation/tr.h>

namespace tt::app::dialog {

error_t alert(uint32_t appInstanceId, const std::string& title, const std::string& message) {
    return alert(appInstanceId, title, message, { tr("OK") }, nullptr, nullptr);
}

error_t alert(uint32_t appInstanceId, const std::string& title, const std::string& message, const std::vector<std::string>& buttonLabels, WindowAlertDialogResultFn onResult, void* userData) {
    std::vector<const char*> labels;
    labels.reserve(buttonLabels.size());
    for (const auto& label : buttonLabels) {
        labels.push_back(label.c_str());
    }
    return window_manager_alert_dialog(appInstanceId, title.c_str(), message.c_str(), labels.data(), static_cast<uint32_t>(labels.size()), onResult, userData);
}

error_t input(uint32_t appInstanceId, const std::string& title, const std::string& message, const std::string& prefilled, WindowInputDialogResultFn onResult, void* userData) {
    return window_manager_input_dialog(appInstanceId, title.c_str(), message.c_str(), prefilled.c_str(), tr("OK"), tr("Cancel"), onResult, userData);
}

error_t selection(uint32_t appInstanceId, const std::string& title, const std::vector<std::string>& items, WindowSelectionDialogResultFn onResult, void* userData) {
    std::vector<const char*> texts;
    texts.reserve(items.size());
    for (const auto& item : items) {
        texts.push_back(item.c_str());
    }
    return window_manager_selection_dialog(appInstanceId, title.c_str(), texts.data(), static_cast<uint32_t>(texts.size()), onResult, userData);
}

error_t selection(uint32_t appInstanceId, const std::string& title, const std::vector<std::string>& items, const std::vector<const char*>& icons, WindowSelectionDialogResultFn onResult, void* userData) {
    if (icons.size() != items.size()) {
        return ERROR_INVALID_ARGUMENT;
    }
    std::vector<const char*> texts;
    texts.reserve(items.size());
    for (const auto& item : items) {
        texts.push_back(item.c_str());
    }
    return window_manager_selection_dialog_with_icons(appInstanceId, title.c_str(), texts.data(), icons.data(), static_cast<uint32_t>(texts.size()), onResult, userData);
}

} // namespace
