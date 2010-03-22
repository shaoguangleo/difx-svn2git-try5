####### DIFX VERSION ########################
export DIFX_VERSION=DiFX-1.5.3

####### ROOT PATHS ##########################
export DIFXROOT=/usr/local/difx
export DIFX_PREFIX=$DIFXROOT
export PGPLOTDIR=/usr/local/pgplot
export IPPROOT=/opt/intel/ipp/5.2/ia32

####### COMPILER ############################
export MPICXX=/usr/bin/mpicxx

####### USE GFORTRAN IN PREFERENCE TO G77? ##
####### Comment out if not desired ##########
export USEGFORTRAN="yes"

####### IPP libraries needed for linking #############
IPPLIB32="-lipps -lippvm -lippcore"
IPPLIB64="-lippsem64t -lippvmem64t -lippcoreem64t"
#Comment out the following for very new IPP (version > 6)
#IPPLIB32="${IPPLIB32} -lguide"
#IPPLIB64="${IPPLIB64} -lguide"
#Comment out the following for older (pre version 6) IPP
IPPLIB32="${IPPLIB32} -liomp5"
IPPLIB64="${IPPLIB64} -liomp5"
#Uncomment the following for very old (pre version 5) IPP
#PrependPath LD_LIBRARY_PATH  ${IPPROOT}/sharedlib/linux

####### PERL VERSION/SUBVERSION #############
perlver="5"
perlsver="5.8.8"

####### PORTS FOR DIFXMESSAGE ###############
# Uncomment these to enable DIFX_MESSAGES
#export DIFX_MESSAGE_GROUP=224.2.2.1
#export DIFX_MESSAGE_PORT=50201
#export DIFX_BINARY_GROUP=224.2.2.1
#export DIFX_BINARY_PORT=50202

####### CALC SERVER NAME - HARMLESS #########
export CALC_SERVER=swc000

####### No User configurable values below here

####### Operating System, use $OSTYPE
if [ $OSTYPE = "darwin" -o $OSTYPE = "darwin9.0" -o $OSTYPE = "linux" -o $OSTYPE = "linux-gnu" ] 
then
  OS=$OSTYPE
else
  echo "Warning unsupported O/S $OSTYPE"
  exit 1
fi
if [ $OSTYPE = "darwin9.0" ]
then
  OS="darwin"
fi
export DIFXOS=$OS

PrependPath()
{
Path="$1"
NewItem="$2"
eval CurPath=\$$Path

#################################################################
# Add the item.  If the path is currently empty, just set it to
# the new item, otherwise, prepend the new item and colon
# separator.
#################################################################
if [ -n "$CurPath" ]
then
    #################################################################
    # Check to see if the item is already in the list
    #################################################################
    if [ `expr "$CurPath" ':' ".*$NewItem\$"` -eq '0'  -a \
         `expr "$CurPath" ':' ".*$NewItem\:.*"` -eq '0' ]
    then
        eval $Path=$NewItem\:$CurPath
    fi
else
    eval export $Path=$NewItem
fi
}

####### 32/64 BIT DEPENDENT MODIFICATIONS ###
arch=(`uname -m`)
if [ $arch = "i386" -o $arch = "i686" ] #32 bit
then
  export DIFXBITS=32
  PrependPath PERL5LIB         ${DIFXROOT}/lib/perl/$perlver
  export IPPLINKLIBS="$IPPLIB32"
elif [ $arch = "x86_64" ] #64 bit
then
  export DIFXBITS=64
  PrependPath PERL5LIB         ${DIFXROOT}/lib/perl/$perlver
  export IPPLINKLIBS="$IPPLIB64"
else
  echo "Unknown architecture $arch - leaving paths unaltered"
fi

####### LIBRARY/EXECUTABLE PATHS ############
PrependPath PATH             ${DIFXROOT}/bin
if [ $DIFXOS = "darwin" ] 
then
  PrependPath DYLD_LIBRARY_PATH  ${DIFXROOT}/lib
  PrependPath DYLD_LIBRARY_PATH  ${PGPLOTDIR}
  PrependPath DYLD_LIBRARY_PATH  ${IPPROOT}/Libraries
else
  PrependPath LD_LIBRARY_PATH  ${DIFXROOT}/lib
  PrependPath LD_LIBRARY_PATH  ${PGPLOTDIR}
  PrependPath LD_LIBRARY_PATH  ${IPPROOT}/sharedlib
fi
PrependPath PKG_CONFIG_PATH  ${DIFXROOT}/lib/pkgconfig

echo " DiFX version $DIFX_VERSION is selected"
