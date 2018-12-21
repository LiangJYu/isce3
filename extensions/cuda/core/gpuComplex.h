#ifndef __ISCE_CUDA_CORE_GPUCOMPLEX_H__
#define __ISCE_CUDA_CORE_GPUCOMPLEX_H__

#ifdef __CUDACC__
#define CUDA_HOSTDEV __host__ __device__
#define CUDA_DEV __device__
#define CUDA_HOST __host__
#define CUDA_GLOBAL __global__
#else
#define CUDA_HOSTDEV
#define CUDA_DEV
#define CUDA_HOST
#define CUDA_GLOBAL
#endif

namespace isce { namespace cuda { namespace core {
template <class U>
    struct gpuComplex {
        U r, i;
        CUDA_HOSTDEV gpuComplex(): r(0.), i(0.) {};
        CUDA_HOSTDEV gpuComplex(U real): r(real), i(0.) {};
        CUDA_HOSTDEV gpuComplex(U real, U imag): r(real), i(imag) {};

        CUDA_HOSTDEV gpuComplex<U>& operator+=(float const& other) {
            r += other;
            return *this;
        }
        CUDA_HOSTDEV gpuComplex<U>& operator-=(float const& other) {
            r -= other;
            return *this;
        }
        CUDA_HOSTDEV gpuComplex<U>& operator*=(float const& other) {
            r *= other;
            i *= other;
            return *this;
        }
        CUDA_HOSTDEV gpuComplex<U>& operator/=(float const& other) {
            r /= other;
            i /= other;
            return *this;
        }

        CUDA_HOSTDEV gpuComplex<U>& operator+=(double const& other) {
            r += other;
            return *this;
        }
        CUDA_HOSTDEV gpuComplex<U>& operator-=(double const& other) {
            r -= other;
            return *this;
        }
        CUDA_HOSTDEV gpuComplex<U>& operator*=(double const& other) {
            r *= other;
            i *= other;
            return *this;
        }
        CUDA_HOSTDEV gpuComplex<U>& operator/=(double const& other) {
            r /= other;
            i /= other;
            return *this;
        }

        CUDA_HOSTDEV gpuComplex<U>& operator+=(gpuComplex<float> const& other) {
            r += other.r;
            i += other.i;
            return *this;
        }
        CUDA_HOSTDEV gpuComplex<U>& operator-=(gpuComplex<float> const& other) {
            r -= other.r;
            i -= other.i;
            return *this;
        }
        CUDA_HOSTDEV gpuComplex<U>& operator*=(gpuComplex<float> const& other) {
            r = r*other.r + i*other.i;
            i = r*other.i + i*other.r;
            return *this;
        }
        CUDA_HOSTDEV gpuComplex<U>& operator/=(gpuComplex<float> const& other) {
            U s = fabsf(other.r + fabsf(other.i));
            U oos = 1.0 / s;
            U ars = r * oos;
            U ais = i * oos;
            U brs = other.r * oos;
            U bis = other.i * oos;
            s = (brs * brs) + (bis * bis);
            oos = 1.0f / s;
            return gpuComplex<U>(((ars * brs) + (ais * bis)) * oos,
                                ((ais * brs) - (ars * bis)) * oos);
        }

        CUDA_HOSTDEV gpuComplex<U>& operator+=(gpuComplex<double> const& other) {
            r += other.r;
            i += other.i;
            return *this;
        }
        CUDA_HOSTDEV gpuComplex<U>& operator-=(gpuComplex<double> const& other) {
            r -= other.r;
            i -= other.i;
            return *this;
        }
        CUDA_HOSTDEV gpuComplex<U>& operator*=(gpuComplex<double> const& other) {
            r = r*other.r - i*other.i;
            i = r*other.i + i*other.r;
            return *this;
        }
        CUDA_HOSTDEV gpuComplex<U>& operator/=(gpuComplex<double> const& other) {
            U s = fabsf(other.r + fabsf(other.i));
            U oos = 1.0 / s;
            U ars = r * oos;
            U ais = i * oos;
            U brs = other.r * oos;
            U bis = other.i * oos;
            s = (brs * brs) + (bis * bis);
            oos = 1.0f / s;
            return gpuComplex<U>(((ars * brs) + (ais * bis)) * oos,
                                ((ais * brs) - (ars * bis)) * oos);
        }

    };

// add
template <class U>
CUDA_HOSTDEV gpuComplex<U> operator +(gpuComplex<U> x, gpuComplex<U> y) {
    return gpuComplex<U>(x.r + y.r, x.i + y.i);
}

template <class U>
CUDA_HOSTDEV gpuComplex<U> operator +(gpuComplex<U> x, float y) {
    return gpuComplex<U>(x.r + y, x.i);
}

template <class U>
CUDA_HOSTDEV gpuComplex<U> operator +(float x, gpuComplex<U> y) {
    return gpuComplex<U>(x + y.r, y.i);
}

template <class U>
CUDA_HOSTDEV gpuComplex<U> operator +(gpuComplex<U> x, double y) {
    return gpuComplex<U>(x.r + y, x.i);
}

template <class U>
CUDA_HOSTDEV gpuComplex<U> operator +(double x, gpuComplex<U> y) {
    return gpuComplex<U>(x + y.r, y.i);
}

// subtract
template <class U>
CUDA_HOSTDEV gpuComplex<U> operator -(gpuComplex<U> x, gpuComplex<U> y) {
    return gpuComplex<U>(x.r - y.r, x.i - y.i);
}

template <class U>
CUDA_HOSTDEV gpuComplex<U> operator -(gpuComplex<U> x, float y) {
    return gpuComplex<U>(x.r - y, x.i);
}

template <class U>
CUDA_HOSTDEV gpuComplex<U> operator -(float x, gpuComplex<U> y) {
    return gpuComplex<U>(x - y.r, -y.i);
}

template <class U>
CUDA_HOSTDEV gpuComplex<U> operator -(gpuComplex<U> x, double y) {
    return gpuComplex<U>(x.r - y, x.i);
}

template <class U>
CUDA_HOSTDEV gpuComplex<U> operator -(double x, gpuComplex<U> y) {
    return gpuComplex<U>(x - y.r, -y.i);
}

// multiply
template <class U>
CUDA_HOSTDEV gpuComplex<U> operator *(gpuComplex<U> x, gpuComplex<U> y) {
    return gpuComplex<U>(x.r*y.r - x.i*y.i, x.r*y.i + x.i*y.r);
}

template <class U>
CUDA_HOSTDEV gpuComplex<U> operator *(gpuComplex<U> x, float y) {
    return gpuComplex<U>(x.r*y, x.i*y);
}

template <class U>
CUDA_HOSTDEV gpuComplex<U> operator *(float x, gpuComplex<U> y) {
    return gpuComplex<U>(x*y.r, x*y.i);
}

template <class U>
CUDA_HOSTDEV gpuComplex<U> operator *(gpuComplex<U> x, double y) {
    return gpuComplex<U>(x.r*y, x.i*y);
}

template <class U>
CUDA_HOSTDEV gpuComplex<U> operator *(double x, gpuComplex<U> y) {
    return gpuComplex<U>(x*y.r, x*y.i);
}

// divide
template <class U>
CUDA_HOSTDEV gpuComplex<U> operator /(gpuComplex<U> x, gpuComplex<U> y) {
    U s = fabsf(y.r + fabsf(y.i));
    U oos = 1.0 / s;
    U ars = x.r * oos;
    U ais = x.i * oos;
    U brs = y.r * oos;
    U bis = y.i * oos;
    s = (brs * brs) + (bis * bis);
    oos = 1.0f / s;
    return gpuComplex<U>(((ars * brs) + (ais * bis)) * oos,
                        ((ais * brs) - (ars * bis)) * oos);
}

template <class U>
CUDA_HOSTDEV gpuComplex<U> operator /(gpuComplex<U> x, double y) {
    // TODO div by 0 check
    return gpuComplex<U>(x.r/y, x.r/y);
}

template <class U>
CUDA_HOSTDEV gpuComplex<U> operator /(double x, gpuComplex<U> y) {
    U s = fabsf(y.r + fabsf(y.i));
    U oos = 1.0 / s;
    U ars = x;
    U brs = y.r * oos;
    U bis = y.i * oos;
    s = (brs * brs) + (bis * bis);
    oos = 1.0f / s;
    return gpuComplex<U>((ars * brs) * oos, (ars * bis) * oos);
}

// magnitude
template <class U>
CUDA_HOSTDEV U abs(gpuComplex<U> x) {
    U v, w, t;
    U a = fabsf(x.r);
    U b = fabsf(x.i);
    if (a > b) {
        v = a;
        w = b;
    } else {
        v = b;
        w = a;
    }
    t = w / v;
    t = 1.0f + t * t;
    t = v * sqrtf(t);
    if ((v == 0.0f) || (v > 3.402823466e38f) || (w > 3.402823466e38f)) {
        t = v + w;
    }
    return t;
}

}}}
#endif
