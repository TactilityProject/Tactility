#include <Tactility/lvgl/Lvgl.h>
#include <Tactility/lvgl/Style.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/service/loader/Loader.h>
#include <Tactility/Logger.h>
#include <Tactility/StringUtils.h>

#include <lvgl.h>

namespace tt::app::imageviewer {

extern const AppManifest manifest;

static const auto LOGGER = Logger("ImageViewer");
constexpr auto* IMAGE_VIEWER_FILE_ARGUMENT = "file";

class ImageViewerApp final : public App {

    void onShow(AppContext& app, lv_obj_t* parent) override {
        auto wrapper = lv_obj_create(parent);
        lv_obj_set_size(wrapper, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_border_width(wrapper, 0, 0);
        lv_obj_set_style_pad_all(wrapper, 0, 0);
        lv_obj_set_style_pad_gap(wrapper, 0, 0);

        auto toolbar = lvgl::toolbar_create(wrapper, app);
        lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 0);

        auto* image_wrapper = lv_obj_create(wrapper);
        lv_obj_align_to(image_wrapper, toolbar, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);
        lv_obj_set_width(image_wrapper, LV_PCT(100));
        auto parent_height = lv_obj_get_height(wrapper);
        auto toolbar_height = lv_obj_get_height(toolbar);
        lv_obj_set_height(image_wrapper, parent_height - toolbar_height);
        lv_obj_set_flex_flow(image_wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(image_wrapper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(image_wrapper, 0, 0);
        lv_obj_set_style_pad_gap(image_wrapper, 0, 0);
        lvgl::obj_set_style_bg_invisible(image_wrapper);

        auto* image = lv_image_create(image_wrapper);
        lv_obj_align(image, LV_ALIGN_CENTER, 0, 0);

        auto* file_label = lv_label_create(wrapper);
        lv_obj_align_to(file_label, wrapper, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        std::shared_ptr<const Bundle> bundle = app.getParameters();
        check(bundle != nullptr, "Parameters not set");
        std::string file_argument;
        if (bundle->optString(IMAGE_VIEWER_FILE_ARGUMENT, file_argument)) {
            std::string prefixed_path = lvgl::PATH_PREFIX + file_argument;
            LOGGER.info("Opening {}", prefixed_path);
            lv_img_set_src(image, prefixed_path.c_str());
            auto path = string::getLastPathSegment(file_argument);
            lv_label_set_text(file_label, path.c_str());
        } else {
            lv_label_set_text(file_label, "File not found");
        }
    }
};

extern const AppManifest manifest = {
    .appId = "ImageViewer",
    .appName = "Image Viewer",
    .appCategory = Category::System,
    .appFlags = AppManifest::Flags::Hidden,
    .createApp = create<ImageViewerApp>
};

LaunchId start(const std::string& file) {
    auto parameters = std::make_shared<Bundle>();
    parameters->putString(IMAGE_VIEWER_FILE_ARGUMENT, file);
    return app::start(manifest.appId, parameters);
}

} // namespace
