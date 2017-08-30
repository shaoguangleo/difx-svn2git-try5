#!/bin/sh
#
# complete set
#

cfiles='
    Changelog
    CalTable.cpp
    CalTable.h
    DataIO.cpp
    DataIO.h
    DataIOFITS.cpp
    DataIOFITS.h
    DataIOSWIN.cpp
    DataIOSWIN.h
    INSTALL
    _PolConvert.cpp
    _PolGainSolve.cpp
    polconvert.xml
    Weighter.h
    Weighter.cpp
    '

scripts='
    QA2/APP_QA2script_template_C4.py
    QA2/PolConvert_EVN_1.4.py
    QA2/README.POLCONVERT
    QA2/scriptForCalibrationAPP_C4_withTsys.py
    QA2/scriptForCalibrationAPP.py
    QA2/scriptForCalibrationAPP_v-8Jul17.py
    QA2/scriptForImagingAPP_C4.py
    QA2/scriptForImagingAPP.py
    PP/drivepolconvert.py
    PP/prepolconvert.py
    PP/runpolconvert.py
    PP/maketar.sh
    PP/README.POLCONVERT
    setup.py
    task_polconvert.py
    '

built='
    _PolConvert.so
    _PolGainSolve.so
    polconvert.py
    polconvert_cli.py
    '

version=${1-`date +%Y%m%dT%H%M%S`}

tar zcf PolConvert_$version.tar.gz $cfiles $scripts

#
# eof
#
