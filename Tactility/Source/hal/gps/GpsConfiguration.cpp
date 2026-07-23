#include "Tactility/hal/gps/GpsConfiguration.h"
#include "Tactility/service/gps/GpsService.h"
#include "Tactility/file/ObjectFile.h"

namespace tt::hal::gps {

const char* toString(GpsModel model) {
    using enum GpsModel;
    switch (model) {
        case AG3335:
            return TT_STRINGIFY(AG3335);
        case AG3352:
            return TT_STRINGIFY(AG3352);
        case ATGM336H:
            return TT_STRINGIFY(ATGM336H);
        case LS20031:
            return TT_STRINGIFY(LS20031);
        case MTK:
            return TT_STRINGIFY(MTK);
        case MTK_L76B:
            return TT_STRINGIFY(MTK_L76B);
        case MTK_PA1616S:
            return TT_STRINGIFY(MTK_PA1616S);
        case UBLOX6:
            return TT_STRINGIFY(UBLOX6);
        case UBLOX7:
            return TT_STRINGIFY(UBLOX7);
        case UBLOX8:
            return TT_STRINGIFY(UBLOX8);
        case UBLOX9:
            return TT_STRINGIFY(UBLOX9);
        case UBLOX10:
            return TT_STRINGIFY(UBLOX10);
        case UC6580:
            return TT_STRINGIFY(UC6580);
        default:
            return TT_STRINGIFY(Unknown);
    }
}

std::vector<std::string> getModels() {
    std::vector<std::string> result;
    for (GpsModel model = GpsModel::Unknown; model <= GpsModel::UC6580; ++(int&)model) {
        result.push_back(toString(model));
    }
    return result;
}

}
