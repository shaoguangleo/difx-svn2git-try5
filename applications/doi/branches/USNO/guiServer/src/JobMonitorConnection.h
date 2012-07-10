#ifndef GUISERVER_JOBMONITORCONNECTION_H
#define GUISERVER_JOBMONITORCONNECTION_H
//=============================================================================
//
//   guiServer::JobMonitorConnection Class
//
//!  Handles a (client) TCP connection with the GUI to send it job monitoring
//!  diagnostics and to respond to some instructions.
//
//=============================================================================
#include <PacketExchange.h>

namespace guiServer {

    class JobMonitorConnection : public PacketExchange {
    
    public:

        //-----------------------------------------------------------------------------
        //  These are packet types used for sending diagnostic and progress messages from
        //  running jobs to the GUI.  These get rather highly specific.  There are a
        //!  few types that are used to obtain data from the GUI as well.
        //-----------------------------------------------------------------------------
        static const int JOB_TERMINATED                     = 100;
        static const int JOB_ENDED_GRACEFULLY               = 101;
        static const int JOB_STARTED                        = 102;
        static const int PARAMETER_CHECK_IN_PROGRESS        = 103;
        static const int PARAMETER_CHECK_SUCCESS            = 104;
        static const int FAILURE_NO_HEADNODE                = 105;
        static const int FAILURE_NO_DATASOURCES             = 106;
        static const int FAILURE_NO_PROCESSORS              = 107;
        static const int FAILURE_NO_INPUTFILE_SPECIFIED     = 108;
        static const int FAILURE_INPUTFILE_NOT_FOUND        = 109;
        static const int FAILURE_INPUTFILE_NAME_TOO_LONG    = 110;
        static const int FAILURE_OUTPUT_EXISTS              = 111;
        static const int DELETING_PREVIOUS_OUTPUT           = 112;
    
        JobMonitorConnection( network::GenericSocket* sock ) : PacketExchange( sock ) {
        }
        
        ~JobMonitorConnection() {
        }
        

    protected:
            
    };

}

#endif
