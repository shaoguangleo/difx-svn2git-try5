#!/bin/sh
#
# $Id: chk_fringex.sh 3326 2021-09-04 13:05:05Z gbc $
#
# check that fringex runs in refringe mode
#

verb=false
[ -n "$testverb" ] && verb=true

[ -d "$srcdir" ] || { echo srcdir not set; exit 1; }
${HOPS_SETUP-'false'} || . $srcdir/chk_env.sh
export DATADIR=`pwd`
$verb && echo DATADIR=$DATADIR

rdir="2843/321-1701_0552+398"
targ="0552+398"
time=oifhak

umask 0002
[ -d $rdir ] || mkdir -p $rdir
[ -d $srcdir/testdata/$rdir ] || { echo Missing 2843 data; exit 2; }
[ -f alist-aedit.out ] || { echo Run chk_aedit.sh first; exit 3; }

$verb && echo cp $srcdir/testdata/$rdir/*.$time $rdir
cp $srcdir/testdata/$rdir/*.$time $rdir
chmod +w $rdir/*
files=`ls -1 *.$time 2>&1 | wc -l`
[ "$files" -gt 1 ] && mv *.$time $rdir &&
$verb && echo moved $files files to $rdir

grep -v ' S06 ' alist-aedit.out > alist-aedit-X0X.out

$verb && echo \
fringex -i all -c -r alist-aedit-X0X.out \> fringex.avg
fringex -i all -c -r alist-aedit-X0X.out  > fringex.avg

lines=`cat fringex.avg | wc -l`
$verb && echo lines is $lines
scans=`grep endofscan fringex.avg | wc -l`
$verb && echo scans is $scans

# smaller number is without aline header, larger is with.
[ "$lines" -ge 447 -a "$lines" -le 450 -a "$scans" -eq 21 ]

#
# eof
#
