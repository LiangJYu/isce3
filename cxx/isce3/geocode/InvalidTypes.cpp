#include <cmath>

#include "InvalidTypes.h"

isce3::geocode::GeocodeInvalidTypes::GeocodeInvalidTypes(double invalid_dbl) {
    if (std::isnan(invalid_dbl)) {
        _float = std::numeric_limits<float>::quiet_NaN();
        _double = std::numeric_limits<double>::quiet_NaN();
        // Not sure if below is ideal behavior.
        _unsigned_char = std::numeric_limits<unsigned char>::max();
        _unsigned_short = std::numeric_limits<unsigned short>::max();
        _unsigned_int = std::numeric_limits<unsigned int>::max();
    } else {
        _float = static_cast<float>(invalid_dbl);
        _double = invalid_dbl;
        // What if invalid_dbl is < 0?
        _unsigned_char = static_cast<unsigned char>(invalid_dbl);
        _unsigned_short = static_cast<unsigned short>(invalid_dbl);
        _unsigned_int = static_cast<unsigned int>(invalid_dbl);
    }
}
