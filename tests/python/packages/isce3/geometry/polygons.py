#!/usr/bin/env python3
from isce3.geometry import (get_dem_boundary_polygon, compute_dem_overlap,
    DEMInterpolator, compute_polygon_overlap)
from isce3.io import Raster
import iscetest
import numpy.testing as npt
from pathlib import Path
import pytest
import shapely
import shapely.wkt
import tempfile

swath_wkt = """POLYGON ((117.888163837239 -84.2118220075399 2990.49633788733,
116.251653034631 -84.4162809608174 3030.64379882838,
114.575602254013 -84.6068979334439 3049.47436523433,
112.851369088676 -84.7854366535688 3063.28344742364,
111.072375877864 -84.9530867356406 3077.35302726827,
109.232228402045 -85.1107722656671 3104.32080142222,
107.328799294971 -85.2588982368456 3117.87475585805,
105.357056187039 -85.398012774732 3131.92675666838,
103.314720227112 -85.5283738306823 3138.20581054558,
101.198758246222 -85.6502393587807 3144.77661132678,
99.0060639162927 -85.7638136042832 3168.62182617078,
98.4398206418436 -85.6998465772745 3175.34643554682,
97.8901750218018 -85.635474182186 3179.30541991121,
97.3555164570419 -85.5707615365582 3190.7871093764,
96.8360306919643 -85.5056869487819 3201.78588999907,
96.3310651846861 -85.4402698231061 3213.17626952972,
95.8401063655807 -85.3745232070629 3224.62475585627,
95.3614337354962 -85.3085176214213 3249.11987175905,
94.8980034804499 -85.2421025542711 3249.18139648448,
94.4457297357417 -85.1754610534524 3263.96093750241,
94.0067719632826 -85.108481678145 3264.64599610402,
96.0155811987474 -85.0094167050841 3260.58520507749,
97.9803660402879 -84.9024206070757 3258.74804687578,
99.9017263256197 -84.7872469728022 3251.27514642212,
101.780713460056 -84.6635789772207 3232.68359372538,
103.617417924093 -84.5311198876276 3216.07666015629,
105.413845640634 -84.3893858881156 3200.97924804849,
107.189784631014 -84.2362048281896 2993.11621092928,
108.913674282977 -84.0737667905016 2993.11621093374,
110.608428896563 -83.8994327265794 2993.11621092869,
112.28005861355 -83.7116628637569 2993.11621087465,
112.800561227379 -83.7641031606533 2993.11621087494,
113.32967118027 -83.8160408124704 2993.11621087555,
113.867521079312 -83.8674630589505 2993.11621087413,
114.414242114074 -83.9183568369736 2993.1162107691,
114.964563237334 -83.9693744146496 3054.25195287572,
115.530359745602 -84.0190602445131 3043.68212766808,
116.105084204025 -84.0682151443923 3036.76489222766,
116.690376515728 -84.1166324386074 3016.16455029121,
117.28417860623 -84.1645728509571 3006.69628906015,
117.888163837239 -84.2118220075399 2990.49633788733))"""


@pytest.fixture(scope="module")
def dem():
    path = Path(iscetest.data) / "dem_south_pole.tif"
    dem = DEMInterpolator()
    dem.load_dem(Raster(str(path)))
    dem.compute_min_max_mean_height()
    return dem


def test_dem_polygon_is_counter_clockwise(dem):
    ogr_poly = get_dem_boundary_polygon(dem)
    polygon = shapely.wkt.loads(ogr_poly.ExportToWkt())
    npt.assert_(shapely.is_ccw(polygon.boundary))


def test_dem_overlap(dem):
    area_outside = compute_dem_overlap(swath_wkt, dem)
    npt.assert_(area_outside > 0.0)

    with tempfile.NamedTemporaryFile("wb", suffix=".png") as plotfile:
        compute_dem_overlap(swath_wkt, dem, plot=plotfile)


def test_polygon_overlap():
    assert_close = lambda x, y: npt.assert_allclose(x, y, rtol=1e-4, atol=1e-4)

    # unit square on the equator
    poly1 = shapely.Polygon([
        (0, 0),
        (1, 0),
        (1, 1),
        (0, 1),
        (0, 0)])

    # should intersect itself perfectly
    area = compute_polygon_overlap(poly1, poly1)
    assert_close(area, 1.0)

    # longitude cut in half
    poly2 = shapely.Polygon([
        (0, 0),
        (0.5, 0),
        (0.5, 1),
        (0, 1),
        (0, 0)])

    # (poly1 & poly2) area should be roughly half of poly1
    area = compute_polygon_overlap(poly1, poly2)
    assert_close(area, 0.5)

    # (poly1 & poly2) area should be equal to poly2
    area = compute_polygon_overlap(poly2, poly1)
    assert_close(area, 1.0)
