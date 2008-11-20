/* Classic Ladder Project */
/* Copyright (C) 2001-2007 Marc Le Douarain */
/* http://www.multimania.com/mavati/classicladder */
/* http://www.sourceforge.net/projects/classicladder */
/* February 2001 */
/* ------------------------------ */
/* The main for Linux Application */
/* ------------------------------ */
/* This library is free software; you can redistribute it and/or */
/* modify it under the terms of the GNU Lesser General Public */
/* License as published by the Free Software Foundation; either */
/* version 2.1 of the License, or (at your option) any later version. */

/* This library is distributed in the hope that it will be useful, */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU */
/* Lesser General Public License for more details. */

/* You should have received a copy of the GNU Lesser General Public */
/* License along with this library; if not, write to the Free Software */
/* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

// This is an adaption of classicladder V7.124 For EMC
// most of this file is very different from the original
// it uses RTAPI realtime code and HAL code for memory allocation and input/output.
// this adaptation was started Jan 2008 by Chris Morley  

#ifdef MODULE
#include <linux/string.h>
#else
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#endif

#include "hal.h"
#include "classicladder.h"
#include "global.h"
#include "files_project.h"
#include "calc.h"
#include "vars_access.h"
#include "manager.h"
#include "calc_sequential.h"
#include "files_sequential.h"
#include "config.h"
// #include "hardware.h"
#include "socket_server.h"
#include "socket_modbus_master.h"

#if !defined( MODULE )
#include "classicladder_gtk.h"
#include "manager_gtk.h"
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <getopt.h>
#ifdef __WIN32__
#include <windows.h>
#else
#include <pthread.h>
#endif
#ifdef __XENO__
#include <sys/mman.h>
#endif
#endif

#ifdef GTK_INTERFACE
#include <gtk/gtk.h>
#endif

#ifdef MAT_CONNECTION
#include "../../lib/plc.h"
#endif

int cl_remote;
int nogui = 0,modmaster=0,modserver=0;
int ModbusServerPort = 9502; // Standard "502" requires root privileges...
int CyclicThreadRunning = 0;


void ClassicLadderEndOfAppli( void )
{
	CyclicThreadRunning = 0;
	if(modmaster)  
		{
	    		CloseSocketModbusMaster( );
			CloseSocketServer( );
		}
}

void HandlerSignalInterrupt( int signal_id )
{
	printf("End of application asked\n");
	ClassicLadderEndOfAppli( );
	exit( 0 );
}


void display_help (void)
{
	printf("\nClassicLadder v"CL_RELEASE_VER_STRING"\n"CL_RELEASE_DATE_STRING"\n"
	       "Copyright (C) 2001-2004 Marc Le Douarain\nmavati@club-internet.fr\n"
	       "Adapted to EMC\n"
			"\n"
	       "ClassicLadder comes with NO WARRANTY\n"
	       "to the extent permitted by law.\n"
	       "\n"
	       "You may redistribute copies of ClassicLadder\n"
	       "under the terms of the GNU Lesser General Public Licence.\n"
	       "See the file `lesserGPL.txt' for more information.\n");	
	
	printf("This version of Classicladder is adapted for use with EMC and HAL\n"
               "\nUsage: classicladder [OPTIONS] [PATH]\n"
	       "eg: loadusr -w classicladder  ladtest.clp\n"
	       "eg: loadusr -w classicladder  --nogui ladtest.clp\n"
	       "eg: loadusr -w classicladder  --modmaster ladtest.clp\n"
               "\n"
	       "   --nogui            do not create a GUI, only load a configuration\n"
               "   --config=filename  initilize modbus master I/O & load config file-( deprecated- use --modmaster)\n" 
               "   --modmaster        initilize modbus master I/O ( modbus config is loaded with other objects )\n" 
               "   --modbus_port=portnumber  used for modbus slave using TCP ( ethernet )\n"
               "Please also note that the classicladder realtime module must be loaded first\n"
               "eg: loadrt classicladder_rt    for default number of ladder objects\n"  
	                    );
	hal_exit(compId); // add for emc
	exit(0);
}



void process_options (int argc, char *argv[])
{
	int error = 0;

	for (;;)
	{
		int option_index = 0;
		static const char *short_options = "c:";
		static const struct option long_options[] = {
			{"nogui", no_argument, 0, 'n'},
			{"config", required_argument, 0, 'c'},
                        {"modmaster",no_argument,0,'m'},
                        {"modserver",no_argument,0,'s'},
			{"modbus_port", required_argument, 0, 'p'},
			{0, 0, 0, 0},
		};

		int c = getopt_long(argc, argv, short_options,
				    long_options, &option_index);
		if (c == EOF)
			break;

		switch (c)
		{
			
			case 'n':
				nogui = 1;
				break;
			case 'c':
                                read_config (optarg);
				modmaster=1;
				break;
                        case 'm':
                                modmaster=1;
                                break;
                        case 's':
                                modserver=1;
                                break; 
			case 'p':
				ModbusServerPort = atoi( optarg );
				break;
			case '?':
				error = 1;
				break;
		}
	}

	if (error)
		display_help ();

	if ((argc - optind) != 0)
		VerifyDirectorySelected (argv[optind]);
}
//for EMC: do_exit
static void do_exit(int unused) {
		hal_exit(compId);
		printf("ERROR CLASSICLADDER-   Error intializing classicladder user module.\n");
		exit(0);
}
void DoPauseMilliSecs( int Time )
{
	struct timespec time;
        time.tv_sec = 0;
        if (Time>=1000) 
                       {
                        time.tv_sec=Time/1000;
                        Time=Time%1000;
                       }
	time.tv_nsec = Time*1000000;
	nanosleep( &time, NULL );
	//usleep( Time*1000 );
}
void StopRunIfRunning( void )
{
	if (InfosGene->LadderState==STATE_RUN)
	{
		InfosGene->LadderStoppedToRunBack = TRUE;
		InfosGene->LadderState = STATE_STOP;
		while( InfosGene->UnderCalculationPleaseWait==TRUE )
		{
			DoPauseMilliSecs( 100 );
		}
	}
}
void RunBackIfStopped( void )
{
	if ( InfosGene->LadderStoppedToRunBack )
	{
		InfosGene->LadderState = STATE_RUN;
		InfosGene->LadderStoppedToRunBack = FALSE;
	}
}
// after processing options and intiallising HAL, MODBUS and registering shared memory
// the main function is divided into  NOGUI true or NOGUI FALSE
// The difference between them is mostly about checking if a program has already been loaded (only when GUI is to be shown)
// if rungs are used then a program is already in memory so we dont re initallize memory we just start the GUI

int main( int   argc, char *argv[] )
{
	int used=0, NumRung;

	compId=hal_init("classicladder"); //emc
	if (compId<0) return -1; //emc
	signal(SIGTERM,do_exit); //emc
        InitModbusMasterBeforeReadConf( );
	process_options (argc, argv);
        

	if (ClassicLadder_AllocAll())
	{
		char ProjectLoadedOk=TRUE;		
			
		if (nogui==TRUE)
                {
		        rtapi_print("INFO CLASSICLADDER-   No ladder GUI requested-Realtime runs till HAL closes.\n");
			ClassicLadder_InitAllDatas( );
			ProjectLoadedOk = LoadProjectFiles( CurrentProjectFileName );
			InfosGene->LadderState = STATE_RUN;
			ClassicLadder_FreeAll(TRUE);
			hal_ready(compId);
			hal_exit(compId);	
			return 0; 
		} else {	
                                		
				for(NumRung=0;NumRung<NBR_RUNGS;NumRung++)   {   if ( RungArray[NumRung].Used ) used++;   }
				if((used==0) || ( (argc - optind) != 0) )
                                            {	
						ClassicLadder_InitAllDatas( );
						ProjectLoadedOk = LoadProjectFiles( CurrentProjectFileName );
                                                InitGtkWindows( argc, argv );
				                UpdateAllGtkWindows();
                                                MessageInStatusBar( ProjectLoadedOk?"Project loaded and running":"Project failed to load...");
                                                if (!ProjectLoadedOk) 
                                                {  
                                                           ClassicLadder_InitAllDatas( );   
                                                           if (modmaster) {    PrepareModbusMaster( );    }
					        }
                                            }else{
                                                           InitGtkWindows( argc, argv );
                                                           UpdateAllGtkWindows();
                                                           MessageInStatusBar("GUI reloaded with existing ladder program");
                                                           if (modmaster) {    PrepareModbusMaster( );    }
                                                  } 
							
                                if (modserver)         {   InitSocketServer( 0/*UseUdpMode*/, ModbusServerPort/*PortNbr*/);  }
				InfosGene->LadderState = STATE_RUN;
				hal_ready(compId);
				gtk_main();
				rtapi_print("INFO CLASSICLADDER-   Ladder GUI closed. Realtime runs till HAL closes\n");
				ClassicLadder_FreeAll(TRUE);
				hal_exit(compId);
				return 0;
			}		
	}
	 rtapi_print("ERROR CLASSICLADDER-   Ladder memory allocation error\n");
	ClassicLadder_FreeAll(TRUE);
	hal_exit(compId);		
	return 0;
}
