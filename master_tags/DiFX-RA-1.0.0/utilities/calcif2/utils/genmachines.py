#!/usr/bin/env python

#**************************************************************************
#   Copyright (C) 2008-2013 by Walter Brisken & Helge Rottmann            *
#                                                                         *
#   This program is free software; you can redistribute it and/or modify  *
#   it under the terms of the GNU General Public License as published by  *
#   the Free Software Foundation; either version 3 of the License, or     *
#   (at your option) any later version.                                   *
#                                                                         *
#   This program is distributed in the hope that it will be useful,       *
#   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
#   GNU General Public License for more details.                          *
#                                                                         *
#   You should have received a copy of the GNU General Public License     *
#   along with this program; if not, write to the                         *
#   Free Software Foundation, Inc.,                                       *
#   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
#**************************************************************************
#===========================================================================
# SVN properties (DO NOT CHANGE)
#
# $Id$
# $HeadURL: $
# $LastChangedRevision$
# $Author$
# $LastChangedDate$
#
#============================================================================

from string import split, strip, upper, lower
from sys import exit
from os import getenv, umask, environ
from os.path import isfile
import socket
import struct
import subprocess
import signal
import sys
from optparse import OptionParser
from xml.parsers import expat
from copy import deepcopy
try:
    from difxfile.difxmachines import *
except ImportError:
    print "ERROR: Cannot find difxmachines library. Please include $DIFXROOT/lib/python in your $PYTHONPATH environment"
    sys.exit(1)

author  = 'Walter Brisken and Helge Rottmann'
version = '2.5.0'
verdate = '20160112'
minMachinefileVersion = "1.0"	# cluster definition file must have at least this version

defaultDifxMessagePort = 50200
defaultDifxMessageGroup = '224.2.2.1'	

def getUsage():
        """
        Compile usage text for OptionParser
        """
	usage = "%prog [options] [<input1> [<input2>] ...]\n"
	usage += '\n<input> is a DiFX .input file.'
	usage += '\nA program to find required Mark5 modules and write the machines file'
	usage += '\nappropriate for a particular DiFX job.'
	usage += '\n\nNote: %prog respects the following environment variables:'
        usage +=  '\nDIFX_MACHINES: required, unless -m option is given. -m overrides DIFX_MACHINES.'
        usage +=  '\nDIFX_GROUP: if not defined a default of %s will be used.' % defaultDifxMessageGroup
        usage +=  '\nDIFX_PORT: if not defined a default of %s will be used.' % defaultDifxMessagePort
	usage +=  '\nSee http://cira.ivec.org/dokuwiki/doku.php/difx/clusterdef for documentation on the machines file format'
	
	return(usage)

class MessageParser:
    """
    Parses Mark5StatusMessage and Mark6StatusMessage
    """

    def __init__(self):
        self._parser = expat.ParserCreate()
        self._parser.StartElementHandler = self.start
        self._parser.EndElementHandler = self.end
        self._parser.CharacterDataHandler = self.data
	self.fields = {}
        self.type = 'unknown'
	self.vsnA = 'none'
	self.vsnB = 'none'
	self.state = 'Unknown'
	self.unit = 'unknown'
	self.sender = 'unknown'
	self.tmp = ''
	self.ok = False

    def feed(self, sender, data):
        self._parser.Parse(data, 0)
	self.sender = sender

    def close(self):
        self._parser.Parse("", 1) # end of data
        del self._parser # get rid of circular references

    def start(self, tag, attrs):
        if tag == 'mark5Status' or tag=='mark6Status':
		self.type = tag
		self.ok = True
    def parseMark6(self,tag):

	self.fields[tag] = self.tmp

    def parseMark5(self,tag):

        if tag == 'bankAVSN' and self.ok:
		if len(self.tmp) != 8:
			self.vsnA = 'none'
		else:
			self.vsnA = upper(self.tmp)
        if tag == 'bankBVSN' and self.ok:
		if len(self.tmp) != 8:
			self.vsnB = 'none'
		else:
			self.vsnB = upper(self.tmp)

    def end(self, tag):
	if tag == 'from':
		self.unit = lower(self.tmp)
	if tag == 'state' and self.ok:
		self.state = self.tmp
	if self.type == 'mark5Status':
		self.parseMark5(tag)
	if self.type == 'mark6Status':
		self.parseMark6(tag)

    def data(self, data):
        self.tmp = data

    def getinfo(self):
	if self.ok:
		if self.type == 'mark5Status':
			return [self.unit, self.type, self.vsnA, self.vsnB, self.state, self.sender]
		elif self.type == 'mark6Status':
                        return [self.unit, self.type, self.fields, self.state, self.sender]
	else:
		return ['unknown', 'none', 'none', 'Unknown', 'unknown']

def sendRequest(destination, command):

	src = socket.gethostname()
	dest = '<to>%s</to>' %(destination)

	message = \
	  '<?xml version="1.0" encoding="UTF-8"?>\n' \
	  '<difxMessage>' \
	    '<header>' \
	      '<from>%s</from>' \
	      '%s' \
	      '<mpiProcessId>-1</mpiProcessId>' \
	      '<identifier>genmachines</identifier>' \
	      '<type>DifxCommand</type>' \
	    '</header>' \
	    '<body>' \
	      '<seqNumber>0</seqNumber>' \
	      '<difxCommand>' \
	        '<command>%s</command>' \
	      '</difxCommand>' \
	    '</body>' \
	  '</difxMessage>' % (src, dest, command)

	sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
	sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
	sock.sendto(message, (group, port))

	return message

def isModuleComplete(slot, message):
	"""
	Checks whether a Mark6 module has the expected number of disks
	"""
	key = slot + "Disks"	
	if key in message.keys():
		disks = int(message[key])
	key = slot + "MissingDisks"
	if key in message.keys():
                missingDisks = int(message[key])
		
	if disks ==0: 
		return False
	if missingDisks > 0:
		return False
	
	return True
	
def getVsnsByMulticast(maxtime, datastreams, verbose):
	dt = 0.2
	t = 0.0
        vsnlist = []

	count = 0
        for stream in datastreams:
	    count += 1
            if len(stream.vsn) > 0:
                vsnlist.append(stream.vsn)
	    if len(stream.msn) > 0:
		for m in stream.msn:
			vsnlist.append(m)
            
	missingVsns = deepcopy(vsnlist)


	# First send out a call for VSNs
	sendRequest("mark5","getvsn")
	sendRequest("mark6","getvsn")

	# Now listen for responses, until either time runs out or we get all we need
	s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
	s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
	s.bind(('', port))
	mreq = struct.pack("4sl", socket.inet_aton(group), socket.INADDR_ANY)
	s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
	s.settimeout(dt)
	conflicts = []
	results = []
	machines = []
	notidle = []
	incomplete = []
	while t < maxtime and len(missingVsns) > 0:
		try:
			message, address = s.recvfrom(2048)
			sender = split(socket.gethostbyaddr(address[0])[0], '.')[0]
			if verbose > 1:
				print message
			p = MessageParser()
			p.feed(sender, message)
			info = p.getinfo()
			p.close()
			if info[0] == 'unknown':
				continue
			if info[0] in machines:
				continue
			machines.append(info[0])
			results.append(info)

			if info[1] == "mark5Status":
				if info[2] in missingVsns and info[3] in missingVsns:
					conflicts.append(info)
				if info[2] in missingVsns:
					missingVsns.remove(info[2])
					if info[4] != 'Idle' and info[4] != 'Close':
						notidle.append(info[2])
				if info[3] in missingVsns:
					missingVsns.remove(info[3])
					if info[4] != 'Idle' and info[4] != 'Close':
						notidle.append(info[3])
			elif info[1] == 'mark6Status':
				for key in ['slot1MSN', 'slot2MSN', 'slot3MSN', 'slot4MSN']:
					if key in info[2].keys():
						if info[2][key] in missingVsns:
							missingVsns.remove(info[2][key])
							if not isModuleComplete(key[:5], info[2]):
								incomplete.append(info[2][key])
					
		except socket.timeout:
			t += dt
		except socket.herror:
			print 'Weird: cannot gethostbyaddr for %s' % address[0]

	results.sort()
	conflicts.sort()
	missingVsns.sort()
	notidle.sort()
	incomplete.sort()

	return results, conflicts, missingVsns, notidle, incomplete

def getDatastreamsFromInputFile(inputfile):
        """
        Parse the datastream section of the input file to
        obtain VSNs, MSNs and file paths
        """
        datastreams = []
	nds = 0
        dsindices = []
	dssources = []
	dscount = 0
        
	input = open(inputfile).readlines()
    
	for inputLine in input:
		s = split(inputLine, ':')
		if len(s) < 2:
			continue;
		key = s[0].strip()
		keyparts = key.split()
		value = s[1].strip()
                
                # find number of datastreams
		if key == 'ACTIVE DATASTREAMS':
			nds = int(value)
                        # create  empty Datastream objects
                        for i in range (0, nds):
                            stream = Datastream()
                            datastreams.append(stream)
                        
                
                # get datastream indices
                if len(keyparts) == 3 and keyparts[0] == 'DATASTREAM' and keyparts[2] == 'INDEX':
			dsindices.append(int(value))
                
                # obtain types of all datastreams
		if key == 'DATA SOURCE':
			if dscount in dsindices:
				dssources.append(value)
                                datastreams[dscount].type = value
			dscount += 1
                        
                # parse data table
		if len(keyparts) == 2 and keyparts[0] == 'FILE':
			
                        # obtain datastream index
			numDS,index = split(keyparts[1], '/')
			ds = int(numDS.strip())
			idx = int(index.strip())

			

                        if ds < nds:
                            if datastreams[ds].type == 'MODULE':
                                datastreams[ds].vsn = value
                            elif datastreams[ds].type == 'FILE':
                                if datastreams[ds].path == "":
                                    datastreams[ds].path = os.path.dirname(value)
                            elif datastreams[ds].type == 'MARK6':
                                datastreams[ds].msn.append(value)  
                             
            
	return (datastreams)

def writethreads(basename, threads):
        """
        Write the threads file to be used by mpifxcor
        """
	o = open(basename+'threads', 'w')
	o.write('NUMBER OF CORES:    %d\n' % len(threads))
	for t in threads:
		o.write('%d\n' % t)
	o.close()

def writemachines(basename, hostname, results, datastreams, overheadcores, verbose):
        """
        Write machines file to be used by mpirun
        """
        
	dsnodes = []
	threads = []
        
	for stream in datastreams:
            if stream.type == "FILE":
                # check if path for this datastream matches storage area defined in the cluster definition file
                # strip off last directory for matching
                #path = stream.path[:rfind(stream.path, "/")]
                        
                matchCount = 0
                matchNode = ""
                for node in difxmachines.getStorageNodes():
                    for url in node.fileUrls:
                        if stream.path.startswith(url):
                            matchCount += 1
                            matchNode = node.name
                            break
                if matchCount > 1:
                    print "ERROR: identical storage area is associated with different hosts: %s" % path
                    sys.exit(1)
                elif matchCount == 1:
                    dsnodes.append(matchNode)
                else:
                    # use compute node             
                    for node in difxmachines.getComputeNodes():
                        # skip if already used as datastream node
                        if node.name in dsnodes:
                            continue
                            
                        dsnodes.append(node.name)
                        break
                
            elif stream.type == "MODULE":    
                matchNode = ""
                for r in results:
                    # find  module either in bank A or B
                    if r[2] == stream.vsn or r[3] == stream.vsn:
                            if r[0] in difxmachines.getMk5NodeNames():
                                matchNode = r[0]
                            else:
                                # use message sending host
                                matchNode = r[5]

                if matchNode in difxmachines.getMk5NodeNames():
                    dsnodes.append(matchNode)
                else:
                    print '%s not listed as an active mark5 host in machines file' % matchNode
                    return []

            elif stream.type == "MARK6":    
		matchNode = ""	
		for r in results:
                    # find  module either in bank A or B
                            if r[0] in difxmachines.getMk6NodeNames():
                                matchNode = r[0]
                            else:
                                # use message sending host
                                matchNode = r[4]

                if matchNode in difxmachines.getMk6NodeNames():
                    dsnodes.append(matchNode)
                else:
                    print '%s not listed as an active mark6 host in machines file' % matchNode
                    return []
                
	# write machine file
	o = open(basename+'machines', 'w')
        
        # head node
        o.write('%s\n' % (hostname))
        
        # datastream nodes
        for node in dsnodes:
            o.write('%s\n' % (node))
            
        # compute nodes
        for node in difxmachines.getComputeNodes():
            usedThreads = 0
            # if compute node is also used as datastream nodes reduce number of threads
            if node.name in dsnodes:
                usedThreads = dsnodes.count(node.name)
          
            # if head node is also used as compute nodes reduce number of threads by one
            if node.name in hostname:
                usedThreads = 1
                
            o.write('%s\n' % (node.name))
            threads.append(node.threads-usedThreads)
        
	return threads

def uniqueVsns(datastreams):
        """
        Check for duplicate datastreams VSNs. Returns 1 if duplicates are found, 0 otherwise
        """
        
        d = {}
        vsns = []
        for stream in datastreams:
            if len(stream.vsn) > 0: 
                vsns.append(stream.vsn)
             
	for v in vsns:
		d[v] = 1
	if len(d) != len(vsns):
		return 0
	else:
		return 1

def run(files, machinesfile, overheadcores, verbose, dothreads, useDifxDb):
	ok = True

        # check if host is an allowed headnode
	hostname = socket.gethostname()
	if not hostname in difxmachines.getHeadNodeNames():
		print 'ERROR: hostname (%s) is not an allowed headnode in the machines file : %s' % (hostname, machinesfile)
		exit(1)

	infile = files[0]

	basename = infile[0:-5]
	if basename + 'input' != infile:
		print 'expecting input file'
		exit(1)
				
        datastreams =  getDatastreamsFromInputFile(infile)
 
	if not uniqueVsns(datastreams):
		print 'ERROR: at least one duplicate VSN exists in %s !' % infile
		exit(1)
                
	results, conflicts, missing, notidle, incomplete = getVsnsByMulticast(5, datastreams, verbose)

	if verbose > 0:
		print 'Found modules:'
		for r in results:
			print '  %-10s : %10s %10s   %s' % (r[0], r[2], r[3], r[4])

	if len(conflicts) > 0:
		ok = False
		print 'Module conflicts:'
		for c in conflicts:
			print '  %-10s : %10s %10s' % (c[0], c[1], c[2])
	
	if len(missing) > 0:
		ok = False
		print 'Missing modules:'
		for m in missing:

			slot = "unknown"
			if useDifxDb:
				child = subprocess.Popen(["getslot", m], stdout=subprocess.PIPE)
				(slot, stderr) = child.communicate()

			
			print '  %s (slot = %s )' % (m, strip(slot))

	if len(notidle) > 0:
		ok = False
		print 'Modules not ready:'
		for n in notidle:
			print '  %s' % n

	if len(incomplete) > 0:
		ok = False
		print 'Incomplete modules:'
		for i in incomplete:
			print '  %s' % i

	if not ok:
		return 1

        t = writemachines(basename, hostname, results, datastreams, overheadcores, verbose)
        
	if len(t) == 0:
		return 1

	if dothreads:
		writethreads(basename, t)

	return 0

def signalHandler(signal, frame):
        print 'You pressed Ctrl+C!'
        sys.exit(8)
        
class Datastream:
        """
        Storage class containing datastream description read from the input file
        NETWORK datastreams not yet supported
        """
	def __init__(self):
		self.type = ""	# allowed types MODULE FILE MARK6
		self.vsn = ""	# module VSN in case of MODULE datasource
		self.msn = []	# module MSN in case of MARK6 datasource
		self.path = ""    	# path in case of FILE datasource

class Mark6Datastream(Datastream):
	def __init__(self):
		self.msn = []
		self.group = []
		self.disks = []
		self.missingDisks = []
        
if __name__ == "__main__":

	test = 1
	# catch ctrl+c
	signal.signal(signal.SIGINT, signalHandler)

	usage = getUsage()

	parser = OptionParser(version=version, usage=usage)
	parser.add_option("-v", "--verbose", action="count", dest="verbose", default=0, help="increase verbosity level");
	#parser.add_option("-o", "--overheadcores", dest="overheadcores", type="int", default=1, help="set overheadcores, default = 1");
	parser.add_option("-m", "--machines", dest="machinesfile", default="", help="use MACHINESFILE instead of $DIFX_MACHINES")
	parser.add_option("-n", "--nothreads", dest="dothreads", action="store_false", default=True, help="don't write a .threads file")
	parser.add_option("-d", "--difxdb", dest="usedifxdb", action="store_true", default=False, help="use difxdb to obtain data location")

	(options, args) = parser.parse_args()

	if len(args) == 0:
		parser.print_usage()
		sys.exit(1)

	#overheadcores = options.overheadcores
        overheadcores = 0
	verbose = options.verbose
	dothreads = options.dothreads
	useDifxDb = options.usedifxdb

	# assign the cluster definition file
	if len(options.machinesfile) == 0:
		try:
			machinesfile = environ['DIFX_MACHINES']
		except:
			print ('DIFX_MACHINES environment has to be set. Use -m option instead')
			sys.exit(1)
	else:
		machinesfile = options.machinesfile


	# check that cluster definition file exist
	if not isfile(machinesfile):
		sys.exit("Cluster definition file not found: %s" % machinesfile)

	if getenv('DIFX_GROUP_ID'):
		umask(2)

	# list of input files to process
	files = args

	quit = False
	for f in files:
		if not isfile(f):
			print 'File %s not found' % f
			quit = True
	if quit:
		print 'genmachines quitting.'
		exit(1)

	if verbose > 0:
		print 'DIFX_MACHINES -> %s' % machinesfile

	port = getenv('DIFX_MESSAGE_PORT')
	if port == None:
		port = defaultDifxMessagePort
	else:
		port = int(port)
	group = getenv('DIFX_MESSAGE_GROUP')
	if group == None:
		group = defaultDifxMessageGroup

	
        # read machines file
	difxmachines = DifxMachines(machinesfile)
        
        # compare version
        fail = False
        major, minor = difxmachines.getVersion()
        reqMaj,reqMin = minMachinefileVersion.split(".")
        if (reqMaj < major):
            fail = False
        elif (reqMaj > major):
            fail = True
        else:
            if reqMin > minor:
                fail = True
            elif reqMin < minor:
                fail = False
            else:
                fail = False
        if fail:
            print "ERROR: This version of genmachines requires a cluster defintion file of version > %s. Found version is: %s.%s" % (minMachinefileVersion, major,minor)
            exit(1)
        
	v = run(files, machinesfile, overheadcores, verbose, dothreads, useDifxDb)
	if v != 0:
		exit(v)