#pragma once

#include <limits>

namespace isce3::geocode{

/** Class to convert a given invalid value and return said value as different types.
 *
 * This class contains the logic for handling invalid value initialization and
 * return as different types in a common location for all geocoding functions
 * to access.
 */
class GeocodeInvalidTypes {
    private:
        // Invalid value as a float.
        float _float;

        // Invalid value as a double.
        double _double;

        // Invalid value as an unsigned char.
        unsigned char _unsigned_char;

        // Invalid value as a unsigned short.
        unsigned short _unsigned_short;

        // Invalid value as an unsigned int.
        unsigned int _unsigned_int;

    public:
        /** Class constuctor that converts and sets invalid values of different
         *  types.
         *
         *  \param[in] invalid_dbl  Invalid value, as double, to be converted to
         *                          other types.
         */
        GeocodeInvalidTypes(double invalid_dbl);

        float as_float() const {return _float;}
        double as_double() const {return _double;}
        unsigned char as_unsigned_char() const {return _unsigned_char;}
        unsigned short as_unsigned_short() const {return _unsigned_short;}
        unsigned int as_unsigned_int() const {return _unsigned_int;}
};

}
