/************************************************************************/
/*									*/
/* This routine is responsible for writing a root file, given a		*/
/* complete scan_struct structure.  Because the vex library constructs	*/
/* the appropriately streamlined vex file in memory, complete, all	*/
/* this routine has to do is check for a sensible filename and dump	*/
/* the ascii image to disk.						*/
/*									*/
/*	Inputs:		vex_source	Fully formed ascii vex file	*/
/*					generated by scan_info() call	*/
/*			filename	Full pathname to write to	*/
/*									*/
/*	Output:		return value	0=OK, else bad			*/
/*									*/
/* Created 16 March 1998 by CJL						*/
/*									*/
/************************************************************************/
#include <stdio.h>
#include <string.h>
#include "fstruct.h"
#include "mk4_dfio.h"
#include "mk4_util.h"

int
write_mk4root (char *vex_source,
               char *filename)
    {
    char *ptr;
    fstruct f_info;
    FILE *fp;
					/* Check for legal and sensible */
					/* filename input */
    ptr = (char *)strrchr (filename, '/');
    if (ptr == NULL) ptr = filename - 1;
    if (check_name (ptr+1, &f_info) != 0)
	{
	msg ("Badly formed file name '%s'", 2, ptr+1);
	return (-1);
	}
    if (f_info.type != 0)
	{
	msg ("File '%s' is a type %d file, not a type 0 file", 2, 
						ptr+1, f_info.type);
	return (-1);
	}
					/* Check that the string looks */
					/* like a vex file */
    if (strncmp (vex_source, "VEX_rev", 7) != 0)
	{
	msg ("Improperly formed vex image in write_root()", 2);
	return (-1);
	}
					/* Open the output file */
    if ((fp = fopen (filename, "w")) == NULL)
	{
	msg ("Could not open file '%s'", 2, filename);
	return (-1);
	}
					/* Write it out */
    if (fprintf (fp, vex_source) <= 0)
	{
	msg ("Error writing root file '%s'", 2, filename);
	return (-1);
	}
    fclose (fp);

    return (0);
    }
