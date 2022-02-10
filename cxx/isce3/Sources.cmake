set(SRCS
antenna/detail/BinarySearchFunc.cpp
antenna/ElPatternEst.cpp
antenna/ElNullAnalyses.cpp
antenna/ElNullRangeEst.cpp
antenna/EdgeMethodCostFunc.cpp
antenna/geometryfunc.cpp
core/Attitude.cpp
core/Baseline.cpp
core/Basis.cpp
core/BicubicInterpolator.cpp
core/BilinearInterpolator.cpp
core/Constants.cpp
core/DateTime.cpp
core/detail/BuildOrbit.cpp
core/Ellipsoid.cpp
core/EulerAngles.cpp
core/Interpolator.cpp
core/LUT2d.cpp
core/LookSide.cpp
core/Metadata.cpp
core/NearestNeighborInterpolator.cpp
core/Orbit.cpp
core/Pegtrans.cpp
core/Poly1d.cpp
core/Poly2d.cpp
core/Projections.cpp
core/Quaternion.cpp
core/Sinc2dInterpolator.cpp
core/Spline2dInterpolator.cpp
core/TimeDelta.cpp
error/ErrorCode.cpp
except/Error.cpp
geometry/boundingbox.cpp
fft/detail/ConfigureFFTLayout.cpp
fft/detail/FFTWWrapper.cpp
fft/detail/Threads.cpp
focus/Backproject.cpp
focus/Chirp.cpp
focus/DryTroposphereModel.cpp
focus/GapMask.cpp
focus/Presum.cpp
focus/RangeComp.cpp
geocode/baseband.cpp
geocode/geocodeSlc.cpp
geocode/interpolate.cpp
geocode/loadDem.cpp
geometry/DEMInterpolator.cpp
geometry/Geo2rdr.cpp
geocode/GeocodeCov.cpp
geocode/GeocodePolygon.cpp
geometry/geometry.cpp
geometry/RTC.cpp
geometry/Topo.cpp
geometry/TopoLayers.cpp
geometry/metadataCubes.cpp
image/ResampSlc.cpp
io/gdal/Dataset.cpp
io/gdal/detail/MemoryMap.cpp
io/gdal/GeoTransform.cpp
io/gdal/SpatialReference.cpp
io/IH5.cpp
io/IH5Dataset.cpp
io/Raster.cpp
math/Bessel.cpp
math/Stats.cpp
math/polyfunc.cpp
math/RootFind1dNewton.cpp
math/RootFind1dSecant.cpp
polsar/symmetrize.cpp
product/GeoGridParameters.cpp
product/Product.cpp
product/RadarGridParameters.cpp
signal/Covariance.cpp
signal/Crossmul.cpp
signal/CrossMultiply.cpp
signal/filter2D.cpp
signal/Filter.cpp
signal/flatten.cpp
signal/Looks.cpp
signal/NFFT.cpp
signal/shiftSignal.cpp
signal/signalUtils.cpp
signal/Signal.cpp
signal/filterKernel.cpp
signal/decimate.cpp
signal/convolve.cpp
unwrap/icu/Grass.cpp
unwrap/icu/Neutron.cpp
unwrap/icu/PhaseGrad.cpp
unwrap/icu/Residue.cpp
unwrap/icu/Tree.cpp
unwrap/icu/Unwrap.cpp
unwrap/phass/ASSP.cc
unwrap/phass/BMFS.cc
unwrap/phass/CannyEdgeDetector.cc
unwrap/phass/ChangeDetector.cc
unwrap/phass/EdgeDetector.cc
unwrap/phass/PhaseStatistics.cc
unwrap/phass/Phass.cpp
unwrap/phass/PhassUnwrapper.cc
unwrap/phass/Point.cc
unwrap/phass/RegionMap.cc
unwrap/phass/Seed.cc
unwrap/phass/sort.cc
unwrap/snaphu/snaphu_cost.cpp
unwrap/snaphu/snaphu.cpp
unwrap/snaphu/snaphu_io.cpp
unwrap/snaphu/snaphu_solver.cpp
unwrap/snaphu/snaphu_tile.cpp
unwrap/snaphu/snaphu_util.cpp
)
