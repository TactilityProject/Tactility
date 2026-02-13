#include "Tactility/app/fileselection/FileSelectionPrivate.h"
#include "Tactility/app/fileselection/View.h"
#include "Tactility/app/fileselection/State.h"
#include "Tactility/app/AppContext.h"

#include <Tactility/Assets.h>
#include <Tactility/service/loader/Loader.h>

#include <memory>

namespace tt::app::fileselection {

constexpr auto* TAG = "FileSelection";

extern const AppManifest manifest;

std::string getResultPath(const Bundle& bundle) {
    std::string result;
    if (bundle.optString("path", result)) {
        return result;
    } else {
        return "";
    }
}

Mode getMode(const Bundle& bundle) {
    int32_t mode = static_cast<int32_t>(Mode::ExistingOrNew);
    bundle.optInt32("mode", mode);
    return static_cast<Mode>(mode);
}

void setMode(Bundle& bundle, Mode mode) {
    auto mode_int = static_cast<int32_t>(mode);
    bundle.putInt32("mode", mode_int);
}

class FileSelection : public App {
    std::unique_ptr<View> view;
    std::shared_ptr<State> state;

public:
    FileSelection() {
        state = std::make_shared<State>();
        view = std::make_unique<View>(state, [this](const std::string& path) {
            auto bundle = std::make_unique<Bundle>();
            bundle->putString("path", path);
            setResult(Result::Ok, std::move(bundle));
            stop(manifest.appId);
        });
    }

    void onShow(AppContext& appContext, lv_obj_t* parent) override {
        auto mode = getMode(*appContext.getParameters());
        view->init(parent, mode);
    }
};

extern const AppManifest manifest = {
    .appId = "FileSelection",
    .appName = "File Selection",
    .appCategory = Category::System,
    .appFlags = AppManifest::Flags::Hidden,
    .createApp = create<FileSelection>
};

LaunchId startForExistingFile() {
    auto bundle = std::make_shared<Bundle>();
    setMode(*bundle, Mode::Existing);
    return start(manifest.appId, bundle);
}

LaunchId startForExistingOrNewFile() {
    auto bundle = std::make_shared<Bundle>();
    setMode(*bundle, Mode::ExistingOrNew);
    return start(manifest.appId, bundle);
}

} // namespace
