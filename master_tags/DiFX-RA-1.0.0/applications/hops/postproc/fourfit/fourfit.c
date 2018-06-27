/*******************************************************************************/
/*                                                                             */
/* MK4FIT ... a program to perform fringe searching for MkIV VLBI data         */
/*                                                                             */
/* This is the main routine for the program, which is written for generic UNIX */
/* systems, and uses X windows for graphical output.  In addition, at some     */
/* stage, it will employ the Caltech PGPLOT graphics package for output to     */
/* arbitrary devices.  Detailed descriptions of the program architecture,      */
/* function, and use can be found in the standard documentation directories.   */
/*                                                                             */
/* This version of the program is a Mk4 version of fourfit.  It begins life    */
/* as a standard version of fourfit based on Mk3 root files, but which is      */
/* capable of reading and fringe-fitting Mk4 type-1 files created using the    */
/* utility "mk3tomk4".                                                         */
/*                                                                             */
/* main routine ... original version  CJL June 19 1991                         */
/* Simplified by farming out lots of fiddly details to subroutines             */
/* CJL, April 11 1992                                                          */
/* Converted for Mk4 use starting in February 1997 by CJL                      */
/*                                                                             */
/*******************************************************************************/

#include <stdio.h>
#include <alloca.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "vex.h"                        /* Needed for the VEX root file format */
#include "mk4_data.h"                   /* Definitions of Mk4 data structures */
#include "control.h"                    /* Definition of control structure */
#include "param_struct.h"               /* Definition of 2 structures (param & status) */
#include "pass_struct.h"                /* Pass-specific parameters */
#include "fstruct.h"                    /* For handling file specifiers on cmd line */
#include "refringe.h"                   /* Specific to -r option */
#include "fileset.h"

struct type_param param;                
struct type_status status;              /* External structure declarations */
struct mk4_fringe fringe;
struct mk4_corel cdata;
struct mk4_sdata sdata[MAXSTATIONS];
struct type_plot plot;

struct c_block *cb_head;

int baseline, base, ncorel_rec, lo_offset, max_seq_no;
int do_only_new = FALSE;
int test_mode = FALSE;
int plot_xpower = TRUE;
int write_xpower = FALSE;
int do_accounting = FALSE;
int refringe = FALSE;
int ap_per_seg = 0;
int reftime_offset = 0;

//char progname[] = "fourfit";            /* extern variables for messages */
char progname[] = FF_PROGNAME;		// fourfit or fearfit from Makefile
int msglev = 2;
char *pexec;                            // ptr to progam executable name
char version_no[] = FF_VER_NO;		// PACKAGE_VERSION from Makefile
//char version_no[4] = "3.5";             // Update this with new releases 
                                        // v2.2  ???        cjl
                                        // v3.0  2001.5.25  rjc  first ver. change
                                        //                       in a long, long time
                                        // v3.6  2011.11.21 rjc

#define MAXPASS 32
#define FALSE 0
#define TRUE 1

main (argc, argv)
int argc;
char **argv;
    {
    struct vex root;
    struct freq_corel corel[MAXFREQ];
    /* msg ("MAXFREQ == %d\n", 0, MAXFREQ); */
    struct type_pass *pass;
    char *inputname, *check_rflist();
    char oldroot[256], rootname[256];
    int i, j, k, npass, totpass, ret, nbchecked, nbtried, checked, tried;
    int successes, failures, nroots, nc, fno, fs_ret;
    fstruct *files, *fs;
    struct fileset fset;
    bsgstruct *base_sgrp;
                                        /* Start accounting */
    account ("!BEGIN");
                                        /* Get standard environment settings */
    environment();
                                        /* Trap empty argument list */
    if (argc == 1)
        {
        syntax();
        return (1);
        }
    pexec = argv[0];                    // point to executable name
                                        /* Initialize IO library allocation */
    cdata.nalloc = 0;
    fringe.nalloc = 0;
    for (i=0; i<MAXSTATIONS; i++) 
        sdata[i].nalloc = 0;
                                        /* Initialize main array for memory alloc */
    for (i=0; i<MAXFREQ; i++) 
        corel[i].data_alloc = FALSE;
                                        /* More initialization */
    msg ("fourfit calling set_defaults...",0);
    if (set_defaults() != 0) exit (1);
                                        /* Interpret and act on any command */
                                        /* line flags, and generate a list of */
                                        /* type root files to be fringe searched, */
                                        /* based on possibly wildcarded file */
                                        /* or directory specifications, or an */
                                        /* A-file list for refringing */
    msg ("fourfit calling parse_cmdline...",0);
    if (parse_cmdline (argc, argv, &files, &base_sgrp, &param) != 0)
        {
        msg ("Fatal error interpreting command line arguments", 2);
        exit(1);
        }
    if (do_accounting) account ("Interpret arguments");
                                        /* Main program loop ... fringe search */
                                        /* all selected files, one by one */
                                        /* All arguments are handled by the two */
                                        /* major data structures */
    i = 0; successes = 0; failures = 0; nroots = 0; tried = 0;
    *root.filename = 0;
    msg ("files[0].order = %d",0, files[i].order);
    while (files[i].order >= 0)
        {
        inputname = files[i++].name;
        msg ("processing %s fileset", -1, inputname);
                                        /* Performs sanity check on requested file */
                                        /* and reports internally.  Allows for */
                                        /* fringe_all=false, among other things */
                                        /* Fills in absolute pathname of root */
                                        /* we need to read */
        if (get_abs_path (inputname, rootname) != 0) continue; 
        if (get_vex (rootname, OVEX | EVEX | IVEX | LVEX, "", &root) != 0)
            {
            msg ("Error reading root for file %s, skipping", 3, inputname);
            continue;
            }
        nroots++;
        if (do_accounting) account ("Read root files");
                                        /* copy in root filename for later use */
        strncpy (root.ovex->filename, rootname, 256);
                                        /* Record the accumulation period - the */
                                        /* only information needed from evex */
        param.acc_period = root.evex->ap_length;
        param.speedup = root.evex->speedup_factor;
                                        /* Find all files belonging to this root */
        if (fileset (rootname, &fset) != 0)
            {
            msg ("Error getting fileset of '%s'", 2, rootname);
            continue;
            }
                                        /* Keep track of latest type-2 file number */
        max_seq_no = fset.maxfile;
                                        /* Read in all the type-3 files */
        if (read_sdata (&fset, sdata) != 0)
            {
            msg ("Error reading in the sdata files for '%s'", 2, rootname);
            continue;
            }
        msg ("Successfully read station data for %s", 0, rootname);
                                        /* Need to loop over all baselines in this */
                                        /* root.  Baseline filtering of data is */
                                        /* taken care of in get_corel_data */
                                        /* and, if refringing, in check_rflist */
        totpass = 0; ret = 0; nbchecked = 0; nbtried = 0;
        fno = -1;
        while (fset.file[++fno].type > 0)
            {
            fs = fset.file + fno;
            msg ("Encountered type %d file:  %s", -1, fs->type, fs->name);
                                        /* Interested only in type 1 files */
            if (fs->type != 1) continue;
                                        /* If this is a refringe, proceed only */
                                        /* if this baseline is in the list */
                                        /* rf_fglist is list of frequency */
                                        /* groups requested */
            param.rf_fglist = NULL;
            if (refringe)
                if ((param.rf_fglist = 
                        check_rflist (fs->baseline, i-1, base_sgrp)) == NULL)
                    continue;
                                        /* This reads the relevant corel file */
                                        /* non-zero return generally means this */
                                        /* baseline not required, instead of error */
                                        /* (it looks at control information) */
            if (get_corel_data (fs, root.ovex, root.filename, &cdata) != 0)  continue;
            if (do_accounting) account ("Read data files");
                                        /* Put data in useful form */
                                        /* Also, interpolate sdata info */
            msg ("Organizing data for file %s", 0, inputname);
            if (organize_data (&cdata, root.ovex, root.ivex, sdata, corel) != 0)
                {
                msg ("Error organizing data for file %s, skipping", 2, inputname);
                continue;
                }
            if (do_accounting) account ("Organize data");
                                        /* Figure out multiple passes through data */
                                        /* Put pass-specific parameters in elements */
                                        /* of the pass array */
            if (make_passes (root.ovex, corel, &param, &pass, &npass) != 0)
                {
                msg ("Error setting up fringing passes for %s, %2s, skipping",
                         2, inputname, fs->baseline);
                continue;
                }
            if (do_accounting) account ("Make passes");
                                        /* Now do the actual fringe searching. */
                                        /* Loop over all passes, accumulating */
                                        /* errors in ret.  Error reporting is */
                                        /* internal to fringe_search() */
            nbtried++;
            totpass += npass;
            for (k=0; k<npass; k++)
                {
                fs_ret = fringe_search (&root, &param, pass + k);
                if (fs_ret < 0) break;
                ret += fs_ret;
                }
                                        /* Quit request from user */
            if (fs_ret < 0) 
                {
                msg ("Quitting by request", 2);
                break;
                }
                                        /* Move to next file in fileset */
            }                           /* End of baseline loop */

                                        /* Successful fringing, update root */
//      if ((ret < totpass) && (! test_mode)) 
//          {
/*            write_root (&root, rootname);  */
//          if (do_accounting) account ("Update root files");
//          }
                                        /* Keep some statistics */
        checked += nbchecked;
        tried += nbtried;
        successes += totpass - ret;
        failures += ret;
                                        /* End of filename loop */
        }
                                        /* Tell user what we did, how it went. */
                                        /* If it went badly enough, inform the */
                                        /* shell with non-zero exit status */
/*    ret = report_actions (i, nroots, checked, tried, successes, failures); */
                                        /* Complete accounting and exit */
    if (do_accounting) account ("Report results");
    if (do_accounting) account ("!REPORT");

    exit(ret);
    }
