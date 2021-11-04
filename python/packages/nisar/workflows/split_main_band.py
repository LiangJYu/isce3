#!/usr/bin/env python3

import os
import pathlib
import time
import h5py
import journal
import numpy as np

import isce3
from nisar.products.readers import SLC
from nisar.workflows import h5_prep
from nisar.workflows.bandpass_insar_runconfig import BandpassRunConfig
from nisar.workflows.yaml_argparse import YamlArgparse
from nisar.h5 import cp_h5_meta_data
from isce3.splitspectrum import splitspectrum


def run(cfg: dict):
    '''
    run bandpass
    '''
    # pull parameters from cfg
    ref_hdf5 = cfg['InputFileGroup']['InputFilePath']
    sec_hdf5 = cfg['InputFileGroup']['SecondaryFilePath']
    freq_pols = cfg['processing']['input_subset']['list_of_frequencies']
    blocksize = cfg['processing']['ionosphere_correction']['lines_per_block']
    method = cfg['processing']['ionosphere_correction']['method']
    fft_size = cfg['processing']['ionosphere_correction']['range_fft_size']
    window_function = cfg['processing']['ionosphere_correction']['window_function']
    window_shape = cfg['processing']['ionosphere_correction']['window_shape']
    low_bandwidth = cfg['processing']['ionosphere_correction']['low_bandwidth']
    high_bandwidth = cfg['processing']['ionosphere_correction']['high_bandwidth']

    scratch_path = pathlib.Path(cfg['ProductPathGroup']['ScratchPath'])

    error_channel = journal.error('split_main_band.run')
    info_channel = journal.info("split_main_band.run")
    info_channel.log("starting split_main_band")

    t_all = time.time()

    # check if ionospheric correction method is split_main_band
    if method == 'main_side':
        return None
    elif method == 'split_main_band':
        split_band_path = pathlib.Path(f"{scratch_path}/bandpass/")
        split_band_path.mkdir(parents=True, exist_ok=True)
    else:
        err_str = f"{method} invalid method for ionospheric estimation"
        error_channel.log(err_str)
        raise ValueError(err_str)

    common_parent_path = 'science/LSAR'
    freq = 'A'
    pol_list = freq_pols[freq]

    for hdf5_ind, hdf5_str in enumerate([ref_hdf5, sec_hdf5]):
        # reference SLC
        if hdf5_ind == 0:
            low_band_output = f"{split_band_path}/ref_low_band_slc.h5"
            high_band_output = f"{split_band_path}/ref_high_band_slc.h5"
        # secondary SLC
        else:
            low_band_output = f"{split_band_path}/sec_low_band_slc.h5"
            high_band_output = f"{split_band_path}/sec_high_band_slc.h5"            
        print(ref_hdf5)
        print(low_band_output)
        print(high_band_output)

        # init parameters shared by frequency A and B
        slc_product = SLC(hdf5file=hdf5_str)

        # meta data extraction 
        meta_data = splitspectrum.bandpass_meta_data.load_from_slc(
            slc_product=slc_product, 
            freq=freq)
        bandwidth_half = 0.5 * meta_data.rg_bandwidth
        low_frequency_slc = meta_data.center_freq - \
                             bandwidth_half
        high_frequency_slc = meta_data.center_freq + \
                              bandwidth_half

        low_frequencies_spectrum = np.array([low_frequency_slc, high_frequency_slc - high_bandwidth])
        high_frequencies_spectrum = np.array([low_frequency_slc + low_bandwidth, high_frequency_slc])
        print('low freq : ',low_frequencies_spectrum)
        print('high freq : ',high_frequencies_spectrum)

        sub_band_center_freq = (low_frequencies_spectrum + high_frequencies_spectrum) / 2

        # Initialize bandpass instance
        # Specify meta parameters of SLC to be bandpassed 
        split_spectrum = splitspectrum.SplitSpectrum(
            rg_sample_freq=meta_data.rg_sample_freq,
            rg_bandwidth=meta_data.rg_bandwidth,
            center_frequency=meta_data.center_freq,
            slant_range=meta_data.slant_range,  
            freq=freq)
       
        dest_freq_path = f"/science/LSAR/SLC/swaths/frequency{freq}"
        with h5py.File(hdf5_str, 'r', libver='latest', swmr=True) as src_h5, \
                h5py.File(low_band_output, 'w') as dst_h5_low, \
                h5py.File(high_band_output, 'w') as dst_h5_high:
            # Copy HDF 5 file to split low and high bands    
            cp_h5_meta_data(src_h5, dst_h5_low, f'{common_parent_path}')
            cp_h5_meta_data(src_h5, dst_h5_high, f'{common_parent_path}')

            for pol in pol_list:
                
                raster_str = f'HDF5:{hdf5_str}:/{slc_product.slcPath(freq, pol)}'
                slc_raster = isce3.io.Raster(raster_str)   
                rows = slc_raster.length   
                cols = slc_raster.width         
                nblocks = int(np.ceil(rows / blocksize))
  
                for block in range(0, nblocks):
                    print("-- split_spectrum block: ", block)
                    row_start = block * blocksize
                    if ((row_start + blocksize) > rows):
                        block_rows_data = rows - row_start
                    else:
                        block_rows_data = blocksize
                    
                    dest_pol_path = f"{dest_freq_path}/{pol}"
                    target_slc_image = np.empty([block_rows_data, cols], 
                                                dtype=complex)

                    src_h5[dest_pol_path].read_direct(
                        target_slc_image, 
                        np.s_[row_start : row_start + block_rows_data, :])

                    bandpass_slc_low, bandpass_meta_low = split_spectrum.bandpass_shift_spectrum(
                        slc_raster=target_slc_image,
                        low_frequency=low_frequencies_spectrum[0],
                        high_frequency=high_frequencies_spectrum[0],
                        new_center_frequency=sub_band_center_freq[0],
                        fft_size=fft_size, 
                        window_shape=window_shape, 
                        window_function=window_function,
                        resampling=False
                        ) 

                    bandpass_slc_high, bandpass_meta_high = split_spectrum.bandpass_shift_spectrum(
                        slc_raster=target_slc_image,
                        low_frequency=low_frequencies_spectrum[1],
                        high_frequency=high_frequencies_spectrum[1],
                        new_center_frequency=sub_band_center_freq[1],
                        fft_size=fft_size, 
                        window_shape=window_shape, 
                        window_function=window_function,
                        resampling=False
                        ) 

                    if block == 0:
                        del dst_h5_low[dest_pol_path]
                        del dst_h5_high[dest_pol_path]
                        # Initialize the raster with updated shape in HDF5
                        dst_h5_low.create_dataset(dest_pol_path, 
                                              [rows, cols], 
                                              np.complex64,
                                              chunks=(128, 128))
                        dst_h5_high.create_dataset(dest_pol_path, 
                                              [rows, cols], 
                                              np.complex64,
                                              chunks=(128, 128))
                    # Write bandpassed SLC to HDF5
                    dst_h5_low[dest_pol_path].write_direct(bandpass_slc_low,
                        dest_sel=np.s_[row_start : row_start + block_rows_data, :])
                    dst_h5_high[dest_pol_path].write_direct(bandpass_slc_high,
                        dest_sel=np.s_[row_start : row_start + block_rows_data, :])
                    
                dst_h5_low[dest_pol_path].attrs['description'] = f"Split-spectrum SLC image ({pol})"
                dst_h5_low[dest_pol_path].attrs['units'] = f""
 
                dst_h5_high[dest_pol_path].attrs['description'] = f"Split-spectrum SLC image ({pol})"
                dst_h5_high[dest_pol_path].attrs['units'] = f""

            # update meta information for bandpass SLC
            data = dst_h5_low[f"{dest_freq_path}/processedCenterFrequency"]
            data[...] = bandpass_meta_low['center_frequency']
            data = dst_h5_low[f"{dest_freq_path}/processedRangeBandwidth"]
            data[...] = bandpass_meta_low['rg_bandwidth'] 
            data = dst_h5_high[f"{dest_freq_path}/processedCenterFrequency"]
            data[...] = bandpass_meta_high['center_frequency']
            data = dst_h5_high[f"{dest_freq_path}/processedRangeBandwidth"]
            data[...] = bandpass_meta_high['rg_bandwidth'] 
 
    t_all_elapsed = time.time() - t_all
    print('total processing time: ', t_all_elapsed, ' sec')
    info_channel.log(
        f"successfully ran split-main-band in {t_all_elapsed:.3f} seconds")


if __name__ == "__main__":
    '''
    run bandpass from command line
    '''
    # load command line args
    bandpass_parser = YamlArgparse()
    args = bandpass_parser.parse()
    # get a runconfig dict from command line args
    bandpass_runconfig = BandpassRunConfig(args)
    # run bandpass
    run(bandpass_runconfig.cfg)
