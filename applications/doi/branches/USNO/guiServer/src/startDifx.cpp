//=============================================================================
//
//   ServerSideConnection::startDifx Function (and associated functions)
//
//!  Called when an instruction to start a DiFX job is received.  This function
//!  is a member of the ServerSideConnection class.
//!
//!  Much of this code is swiped directly from the Mk5Daemon_startMpifxcorr()
//!  function of mk5daemon, with many thanks to Walter Brisken.
//
//=============================================================================
#include <ServerSideConnection.h>
#include <sys/statvfs.h>
#include <network/TCPClient.h>
#include <JobMonitorConnection.h>
#include <signal.h>

using namespace guiServer;

//-----------------------------------------------------------------------------
//!  Called in response to a user request to start a DiFX session.  This function
//!  does all of the necessary checking to assure (or at least increase the chances)
//!  that DiFX will run.  It also creates .thread and .machine files.  The actual
//!  running of DiFX is performed in a thread that this function, as its last act,
//!  spawns.
//-----------------------------------------------------------------------------
void ServerSideConnection::startDifx( DifxMessageGeneric* G ) {
	const int RestartOptionLength = 16;
	int l, n;
	int childPid;
	char filebase[DIFX_MESSAGE_FILENAME_LENGTH];
	char filename[DIFX_MESSAGE_FILENAME_LENGTH];
	char workingDir[DIFX_MESSAGE_FILENAME_LENGTH];
	char destdir[DIFX_MESSAGE_FILENAME_LENGTH];
	char machinesFilename[DIFX_MESSAGE_FILENAME_LENGTH];
	char message[DIFX_MESSAGE_LENGTH];
	char restartOption[RestartOptionLength];
	char command[MAX_COMMAND_SIZE];
	char chmodCommand[MAX_COMMAND_SIZE];
	char inLine[MAX_COMMAND_SIZE];
	FILE *out;
	const char *jobName;
	const DifxMessageStart *S;
	bool outputExists = false;
	const char *mpiOptions;
	const char *mpiWrapper;
	const char *difxProgram;
	int returnValue;
	char altDifxProgram[64];
	const char *user;
	JobMonitorConnection* jobMonitor;

	//  Cast the body of this message to a DifxMessageStart structure.
	S = &G->body.start;

	//  Open a TCP socket connection to the server that should be running for us on the
    //  host that requested this job start (the GUI, presumably).  This socket is used
    //  for diagnostic messages and to show progress on this specific job.  It can
    //  also be used for some rudimentary control of the job.
    network::TCPClient* guiSocket = new network::TCPClient( S->address, S->port );
    guiSocket->waitForConnect();
    //  Create a Job Monitor Connection out of this new socket if it connected properly.
    //  If it did not connect, we need to bail out now.
    if ( guiSocket->connected() ) {
        jobMonitor = new JobMonitorConnection( guiSocket );
        jobMonitor->sendPacket( JobMonitorConnection::JOB_STARTED, NULL, 0 );
    }
    else {
        diagnostic( ERROR, "client socket connection from guiServer to GUI failed - unable to start job" );
        return;
    }
    
    //=========================================================================
    //  Checks below are run BEFORE we start the thread that actually runs
    //  the job.  Why?  Because we can only count on the viability of the
    //  DifxMessageGeneric structure in this function call.  The thread can't
    //  use it.
    //=========================================================================
    jobMonitor->sendPacket( JobMonitorConnection::PARAMETER_CHECK_IN_PROGRESS, NULL, 0 );

	//  Make sure all needed parameters are included in the message.
	if ( S->headNode[0] == 0 ) {
    	diagnostic( ERROR, "DiFX start failed - no headnode specified." );
    	jobMonitor->sendPacket( JobMonitorConnection::FAILURE_NO_HEADNODE, NULL, 0 );
        jobMonitor->sendPacket( JobMonitorConnection::JOB_TERMINATED, NULL, 0 );
		return;
	}
	if ( S->nDatastream <= 0 ) {
    	diagnostic( ERROR, "DiFX start failed - no data sources specified." );
    	jobMonitor->sendPacket( JobMonitorConnection::FAILURE_NO_DATASOURCES, NULL, 0 );
        jobMonitor->sendPacket( JobMonitorConnection::JOB_TERMINATED, NULL, 0 );
		return;
	}
	if ( S->nProcess <= 0 ) {
    	diagnostic( ERROR, "DiFX start failed - no processing nodes  specified." );
    	jobMonitor->sendPacket( JobMonitorConnection::FAILURE_NO_PROCESSORS, NULL, 0 );
        jobMonitor->sendPacket( JobMonitorConnection::JOB_TERMINATED, NULL, 0 );
		return;
	}
	if ( S->inputFilename[0] == 0 ) {
    	diagnostic( ERROR, "DiFX start failed - no input file specified" );
    	jobMonitor->sendPacket( JobMonitorConnection::FAILURE_NO_INPUTFILE_SPECIFIED, NULL, 0 );
        jobMonitor->sendPacket( JobMonitorConnection::JOB_TERMINATED, NULL, 0 );
		return;
	}

	//  Check to make sure the input file exists
	if( access(S->inputFilename, R_OK) != 0 ) {
		diagnostic( ERROR, "DiFX start failed - input file %s not found.", S->inputFilename );
    	jobMonitor->sendPacket( JobMonitorConnection::FAILURE_INPUTFILE_NOT_FOUND, NULL, 0 );
        jobMonitor->sendPacket( JobMonitorConnection::JOB_TERMINATED, NULL, 0 );
		return;
	}

    //  Make sure the filename can fit in our allocated space for such things.
	if( strlen( S->inputFilename ) + 12 > DIFX_MESSAGE_FILENAME_LENGTH ) {
		diagnostic( ERROR, "Filename %s is too long.", S->inputFilename );
    	jobMonitor->sendPacket( JobMonitorConnection::FAILURE_INPUTFILE_NAME_TOO_LONG, NULL, 0 );
        jobMonitor->sendPacket( JobMonitorConnection::JOB_TERMINATED, NULL, 0 );
		return;
	}
	
    jobMonitor->sendPacket( JobMonitorConnection::PARAMETER_CHECK_SUCCESS, NULL, 0 );

    //  Find the "working directory" (where the .input file resides and data will be put), the
    //  "filebase" (the path of the input file with ".input") and the "job name" (the name of
    //  the input file without .input or directory path).
	strcpy( workingDir, S->inputFilename );
    l = strlen( workingDir );
    for( int i = l-1; i > 0; i-- ) {
        if( workingDir[i] == '/') {
    		workingDir[i] = 0;
    		break;
    	}
    }	
	strcpy( filebase, S->inputFilename );
	l = strlen( filebase );
	for( int i = l-1; i > 0; i-- ) {
		if( filebase[i] == '.' ) {
			filebase[i] = 0;
			break;
		}
	}
	jobName = filebase;
	for( int i = 0; filebase[i]; ++i ) {
		if( filebase[i] == '/' ) {
			jobName = filebase + i + 1;
		}
	}

	//  Make sure the working directory exists and has enough free space
	struct statvfs fiData;
	int v = statvfs( workingDir, &fiData );
	if( v == 0 ) {
		long long freeSpace;
		freeSpace = fiData.f_bsize * fiData.f_bavail;
		if( freeSpace < 100000000 ) {
			diagnostic( WARNING, 
			    "Working directory %s has only %lld bytes free - mpifxcorr will likely crash!", 
				workingDir, freeSpace );
		}
		else if( fiData.f_ffree < 3 )
		{
			diagnostic( WARNING, "%s has no free inodes - mpifxcorr will likely crash!", 
				workingDir );
		}
	}
	else {
		diagnostic( ERROR, 
		    "statvfs failed when accessing directory %s : it seems not to exist!", 
			workingDir );
		return;
	}
	
	//  See if the output file already exists.
	snprintf( filename, DIFX_MESSAGE_FILENAME_LENGTH, "%s.difx", filebase );
	if( access( filename, F_OK ) == 0 )
	    outputExists = true;
    //  Should we be over writing it?  If not, we need to bail out.
	if( outputExists && !S->force ) {
		diagnostic( ERROR, "Output file %s exists.  Aborting correlation.", 
			filename );
        jobMonitor->sendPacket( JobMonitorConnection::FAILURE_OUTPUT_EXISTS, NULL, 0 );
        jobMonitor->sendPacket( JobMonitorConnection::JOB_TERMINATED, NULL, 0 );
		return;
	}

	//  Create a structure to hold all information about this job for the thread that will
	//  run it.
	DifxStartInfo* startInfo = new DifxStartInfo;
	startInfo->ssc = this;
	startInfo->jmc = jobMonitor;
	
	//  Sanity check of the .machines file.  We use this to get the "np" value for mpirun
	//  as well.
	int procCount = 0;
    strncpy( machinesFilename, S->inputFilename, DIFX_MESSAGE_FILENAME_LENGTH );
    l = strlen( machinesFilename );
    for( int i = l-1; i > 0; i-- ) {
	    if( machinesFilename[i] == '.' ) {
		    machinesFilename[i] = 0;
		    break;
	    }
    }
    strncat( machinesFilename, ".machines", DIFX_MESSAGE_FILENAME_LENGTH );
    FILE* inp = fopen( machinesFilename, "r" );
    if ( inp != NULL ) {
        while ( fgets( inLine, MAX_COMMAND_SIZE, inp ) )
            ++procCount;
        fclose( inp );
    }
	
	snprintf( startInfo->jobName, MAX_COMMAND_SIZE, "%s", filebase );
	for ( int i = 0; filebase[i]; ++i ) {
	    if ( filebase[i] == '/' )
	        snprintf( startInfo->jobName, MAX_COMMAND_SIZE, "%s", filebase + i + 1 );
	}
	
	//  Build the "remove" command - this removes existing data if the force option is picked.
	if( S->force && outputExists ) {
		snprintf( startInfo->removeCommand, MAX_COMMAND_SIZE, "/bin/rm -rf %s.difx/", filebase );
		startInfo->force = 1;
	}
	else
	    startInfo->force = 0;
	    
	//  Include any options specified by the user - or use the defaults.
	if( S->mpiOptions[0] )
	    mpiOptions = S->mpiOptions;
 	else
 	    mpiOptions = "--mca mpi_yield_when_idle 1 --mca rmaps seq";

    //  Option to run a different version of mpirun.
	if( S->mpiWrapper[0] )
	    mpiWrapper = S->mpiWrapper;
	else
		mpiWrapper = "mpirun";

    //  Option to run different DiFX commands.
	if( S->difxProgram[0] )
		difxProgram = S->difxProgram;
	else if ( strcmp( S->difxVersion, "unknown" ) != 0 ) {
		snprintf( altDifxProgram, 63, "runmpifxcorr.%s", S->difxVersion );
		difxProgram = altDifxProgram;
	}
	else
	    difxProgram = "mpifxcorr";
	    
	//  Build the "restart" instruction.  This becomes part of the start command.
	if ( S->restartSeconds > 0.0 )
	    snprintf( restartOption, RestartOptionLength, "-r %f", S->restartSeconds );
	else
	    restartOption[0] = 0;
	    
	//  Build the actual start command.
	snprintf( startInfo->startCommand, MAX_COMMAND_SIZE, 
	          "source %s/setup.bash; %s -np %d --bynode --hostfile %s.machines %s %s %s %s 2>&1", 
              workingDir,
              mpiWrapper,
              procCount,
              filebase,
              mpiOptions,
              difxProgram,
              restartOption,
              S->inputFilename );
	
	//  Fork a process to do run the difx job.
	signal( SIGCHLD, SIG_IGN );
	childPid = fork();
	if( childPid == 0 ) {
	
        //  Delete data directories if "force" is in effect.
        if ( startInfo->force ) {
		    jobMonitor->sendPacket( JobMonitorConnection::DELETING_PREVIOUS_OUTPUT, NULL, 0 );
            system( startInfo->removeCommand );
        }
            
        //  Run the DiFX process!
        diagnostic( INFORMATION, "execute \"%s\"\n", startInfo->startCommand );
		jobMonitor->sendPacket( JobMonitorConnection::STARTING_DIFX, NULL, 0 );
        
        FILE* difxPipe = popen( startInfo->startCommand, "r" );
        if( !difxPipe ) {
            diagnostic( ERROR, "mpifxcorr process not started for job %s; popen returned NULL", startInfo->jobName );
            sprintf( message, "mpifxcorr process not started for job %s; popen returned NULL", startInfo->jobName );
		    jobMonitor->sendPacket( JobMonitorConnection::DIFX_ERROR, message, strlen( message ) );
            return;
        }

        char line[DIFX_MESSAGE_LENGTH];

        for (;;) {

            const char* rv = fgets( line, DIFX_MESSAGE_LENGTH, difxPipe );
            if ( !rv )	// eof, probably
	            break;

            for ( int i = 0; line[i]; ++i ) {
	            if(line[i] == '\n')
		            line[i] = ' ';
            }

            if ( strstr( line, "ERROR" ) != NULL ) {
	            diagnostic( ERROR, "MPI: %s", line );
	            jobMonitor->sendPacket( JobMonitorConnection::DIFX_ERROR, line, strlen( line ) );
	        }
            else if ( strstr( line, "WARNING" ) != NULL ) {
	            diagnostic( WARNING, "MPI: %s", line );
	            jobMonitor->sendPacket( JobMonitorConnection::DIFX_WARNING, line, strlen( line ) );
	        }
            else {
	            diagnostic( INFORMATION, "MPI: %s", line );
	            jobMonitor->sendPacket( JobMonitorConnection::DIFX_MESSAGE, line, strlen( line ) );
	        }

        }
        pclose( difxPipe );

        difxMessageSendDifxStatus2( startInfo->jobName, DIFX_STATE_MPIDONE, "" );
		jobMonitor->sendPacket( JobMonitorConnection::DIFX_COMPLETE, NULL, 0 );
	
		exit( EXIT_SUCCESS );
	}

	//  Start an independent thread to run it.
//	pthread_attr_t threadAttr;
//	pthread_t threadId;
//    pthread_attr_init( &threadAttr );
//    pthread_create( &threadId, &threadAttr, staticRunDifxThread, (void*)startInfo );               	
	
}

//-----------------------------------------------------------------------------
//!  Thread to run and monitor DiFX.  All of the necessary setup should be
//!  done already - the "startInfo" structure contains all information needed
//!  to run.  
//-----------------------------------------------------------------------------	
void ServerSideConnection::runDifxThread( DifxStartInfo* startInfo ) {
    

}

