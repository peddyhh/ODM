#!/usr/bin/env python
################################################################################
#   lidar2dems - utilties for creating DEMs from LiDAR data
#
#   AUTHOR: Matthew Hanson, matt.a.hanson@gmail.com
#
#   Copyright (C) 2015 Applied Geosolutions LLC, oss@appliedgeosolutions.com
#
#   Redistribution and use in source and binary forms, with or without
#   modification, are permitted provided that the following conditions are met:
#
#   * Redistributions of source code must retain the above copyright notice, this
#     list of conditions and the following disclaimer.
#
#   * Redistributions in binary form must reproduce the above copyright notice,
#     this list of conditions and the following disclaimer in the documentation
#     and/or other materials provided with the distribution.
#
#   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
#   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
#   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
#   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
#   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
#   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
#   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
#   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
################################################################################

# Library functions for creating DEMs from Lidar data

import os
import json as jsonlib
import tempfile
from opendm import system

import glob
from datetime import datetime
import uuid


""" JSON Functions """


def json_base():
    """ Create initial JSON for PDAL pipeline """
    return {'pipeline': []}


def json_gdal_base(fout, output, radius, resolution=1):
    """ Create initial JSON for PDAL pipeline containing a Writer element """
    json = json_base()

    if len(output) > 1:
        # TODO: we might want to create a multiband raster with max/min/idw
        # in the future
        print "More than 1 output, will only create {0}".format(output[0])
        output = [output[0]]

    json['pipeline'].insert(0, {
        'type': 'writers.gdal',
        'resolution': resolution,
        'radius': radius,
        'filename': '{0}.{1}.tif'.format(fout, output[0]),
        'output_type': output[0]
    })

    return json


def json_las_base(fout):
    """ Create initial JSON for writing to a LAS file """
    json = json_base()
    json['pipeline'].insert(0, {
        'type': 'writers.las',
        'filename': fout  
    })
    return json


def json_add_decimation_filter(json, step):
    """ Add decimation Filter element and return """
    json['pipeline'].insert(0, {
            'type': 'filters.decimation',
            'step': step
        })
    return json


def json_add_classification_filter(json, classification, equality="equals"):
    """ Add classification Filter element and return """
    limits = 'Classification[{0}:{0}]'.format(classification)
    if equality == 'max':
        limits = 'Classification[:{0}]'.format(classification)

    json['pipeline'].insert(0, {
            'type': 'filters.range',
            'limits': limits
        })
    return json


def json_add_maxsd_filter(json, meank=20, thresh=3.0):
    """ Add outlier Filter element and return """
    json['pipeline'].insert(0, {
            'type': 'filters.outlier',
            'method': 'statistical',
            'mean_k': meank,
            'multiplier': thresh
        })
    return json


def json_add_maxz_filter(json, maxz):
    """ Add max elevation Filter element and return """
    json['pipeline'].insert(0, {
            'type': 'filters.range',
            'limits': 'Z[:{0}]'.format(maxz)
        })

    return json


def json_add_maxangle_filter(json, maxabsangle):
    """ Add scan angle Filter element and return """
    json['pipeline'].insert(0, {
            'type': 'filters.range',
            'limits': 'ScanAngleRank[{0}:{1}]'.format(str(-float(maxabsangle)), maxabsangle)
        })
    return json


def json_add_scanedge_filter(json, value):
    """ Add EdgeOfFlightLine Filter element and return """
    json['pipeline'].insert(0, {
            'type': 'filters.range',
            'limits': 'EdgeOfFlightLine[{0}:{0}]'.format(value)
        })
    return json


def json_add_returnnum_filter(json, value):
    """ Add ReturnNum Filter element and return """
    json['pipeline'].insert(0, {
            'type': 'filters.range',
            'limits': 'ReturnNum[{0}:{0}]'.format(value)
        })
    return json


def json_add_filters(json, maxsd=None, maxz=None, maxangle=None, returnnum=None):
    if maxsd is not None:
        json = json_add_maxsd_filter(json, thresh=maxsd)
    if maxz is not None:
        json = json_add_maxz_filter(json, maxz)
    if maxangle is not None:
        json = json_add_maxangle_filter(json, maxangle)
    if returnnum is not None:
        json = json_add_returnnum_filter(json, returnnum)
    return json


def json_add_crop_filter(json, wkt):
    """ Add cropping polygon as Filter Element and return """
    json['pipeline'].insert(0, {
            'type': 'filters.crop',
            'polygon': wkt
        })
    return json


def json_add_reader(json, filename):
    """ Add LAS Reader Element and return """
    json['pipeline'].insert(0, {
            'type': 'readers.las',
            'filename': os.path.abspath(filename)
        })
    return json


def json_add_readers(json, filenames):
    """ Add merge Filter element and readers to a Writer element and return Filter element """
    for f in filenames:
        json_add_reader(json, f)

    if len(filenames) > 1:
        json['pipeline'].insert(0, {
                'type': 'filters.merge'
            })

    return json


def json_print(json):
    """ Pretty print JSON """
    print jsonlib.dumps(json, indent=4, separators=(',', ': '))


""" Run PDAL commands """

def run_pipeline(json, verbose=False):
    """ Run PDAL Pipeline with provided JSON """
    if verbose:
        json_print(json)

    # write to temp file
    f, jsonfile = tempfile.mkstemp(suffix='.json')
    if verbose:
        print 'Pipeline file: %s' % jsonfile
    os.write(f, jsonlib.dumps(json))
    os.close(f)

    cmd = [
        'pdal',
        'pipeline',
        '-i %s' % jsonfile
    ]
    if verbose:
        out = system.run(' '.join(cmd))
    else:
        out = system.run(' '.join(cmd) + ' > /dev/null 2>&1')
    os.remove(jsonfile)


def run_pdalground(fin, fout, slope, cellsize, maxWindowSize, maxDistance, approximate=False, initialDistance=0.7, verbose=False):
    """ Run PDAL ground """
    cmd = [
        'pdal',
        'ground',
        '-i %s' % fin,
        '-o %s' % fout,
        '--slope %s' % slope,
        '--cell_size %s' % cellsize,
        '--initial_distance %s' % initialDistance
    ]
    if maxWindowSize is not None:
        cmd.append('--max_window_size %s' %maxWindowSize)
    if maxDistance is not None:
        cmd.append('--max_distance %s' %maxDistance)

    if approximate:
        cmd.append('--approximate')

    if verbose:
        cmd.append('--developer-debug')
        print ' '.join(cmd)
    print ' '.join(cmd)
    out = system.run(' '.join(cmd))
    if verbose:
        print out

def run_pdaltranslate_smrf(fin, fout, slope, cellsize, maxWindowSize, verbose=False):
    """ Run PDAL translate  """
    cmd = [
        'pdal',
        'translate',
        '-i %s' % fin,
        '-o %s' % fout,
        'smrf',
        '--filters.smrf.cell=%s' % cellsize,
        '--filters.smrf.slope=%s' % slope,
    ]
    if maxWindowSize is not None:
        cmd.append('--filters.smrf.window=%s' % maxWindowSize)

    if verbose:
        print ' '.join(cmd)

    out = system.run(' '.join(cmd))
    if verbose:
        print out

