/******************************************************************************
* 
* CAEN SpA - Front End Division
* Via Vetraia, 11 - 55049 - Viareggio ITALY
* +390594388398 - www.caen.it
*
***************************************************************************//**
* \note TERMS OF USE:
* This program is free software; you can redistribute it and/or modify it under
* the terms of the GNU General Public License as published by the Free Software
* Foundation. This program is distributed in the hope that it will be useful, 
* but WITHOUT ANY WARRANTY; without even the implied warranty of 
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. The user relies on the 
* software, documentation and results solely at his own risk.
******************************************************************************/

/*******************************************************************************
This is a simple DAQ able to configure a discriminator and manage the readout
of a QTP board (Q=QDC, t=TDC, P=Peak sensing ADC).
Main parameters are read from a text config file (default file name = config.txt)
If the base address is not set (either for QTP or Discr), that board will be 
ignored, thus it is possible to use this program for QTP only or discr only.

Supported QTP Models
 Q = QDC		: V792, V792N, V862, V965, V965A
 T = TDC		: V775, V775N
 P = Peak ADC	: V785, V785N, V1785
Supported Discriminator Models: V812, V814, V895
*******************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include<stdbool.h>

#ifdef WIN32
	#include <sys/timeb.h>
	#include <direct.h>
	#include <windows.h>
	#include <conio.h>
	#define kbhit _kbhit
	#define getch _getch
#else
	#include <unistd.h>
	#include <sys/time.h>
	#define Sleep(x) usleep((x)*1000)
#endif

#include <CAENVMElib.h>
#include <CAENVMEtypes.h>

#include "Console.h"

char path[128];

/****************************************************/

#define MAX_BLT_SIZE		(256*1024)

#define DATATYPE_MASK		0x06000000
#define DATATYPE_HEADER		0x02000000
#define DATATYPE_CHDATA		0x00000000
#define DATATYPE_EOB		0x04000000
#define DATATYPE_FILLER		0x06000000

#define LSB2PHY				100   // LSB (= ADC count) to Physical Quantity (time in ps, charge in fC, amplitude in mV)

#define LOGMEAS_NPTS		1000

#define ENABLE_LOG			0

#define FILES_IN_LOCAL_FOLDER	1



/*******************************************************************/

// --------------------------
// Global Variables
// --------------------------
// Base Addresses
uint32_t BaseAddress;
uint32_t QTPBaseAddr = 0;
uint32_t DiscrBaseAddr = 0;

// handle for the V1718/V2718 
int32_t handle = -1; 

int VMEerror = 0;
char ErrorString[100];
FILE *logfile;


/*******************************************************************************/
/*                               READ_REG                                      */
/*******************************************************************************/
uint16_t read_reg(uint16_t reg_addr)
{
	uint16_t data=0;
	CVErrorCodes ret;
	ret = CAENVME_ReadCycle(handle, BaseAddress + reg_addr, &data, cvA32_U_DATA, cvD16);
	if(ret != cvSuccess) {
		sprintf(ErrorString, "Cannot read at address %08X\n", (uint32_t)(BaseAddress + reg_addr));
		VMEerror = 1;
	}
	if (ENABLE_LOG)
		fprintf(logfile, " Reading register at address %08X; data=%04X; ret=%d\n", (uint32_t)(BaseAddress + reg_addr), data, (int)ret);
	return(data);
}



/*******************************************************************************/
/*                                WRITE_REG                                    */
/*******************************************************************************/
void write_reg(uint16_t reg_addr, uint16_t data)
{
	CVErrorCodes ret;
	ret = CAENVME_WriteCycle(handle, BaseAddress + reg_addr, &data, cvA32_U_DATA, cvD16);
	if(ret != cvSuccess) {
		sprintf(ErrorString, "Cannot write at address %08X\n", (uint32_t)(BaseAddress + reg_addr));
		VMEerror = 1;
	}
	if (ENABLE_LOG)
		fprintf(logfile, " Writing register at address %08X; data=%04X; ret=%d\n", (uint32_t)(BaseAddress + reg_addr), data, (int)ret);
}



// ************************************************************************
// Discriminitor settings
// ************************************************************************
int ConfigureDiscr(uint16_t OutputWidth, uint16_t Threshold[16], uint16_t EnableMask)
{
	int i;

	BaseAddress = DiscrBaseAddr;
	// Set channel mask
	write_reg(0x004A, EnableMask);

	// set output width (same for all channels)
	write_reg(0x0040, OutputWidth);
	write_reg(0x0042, OutputWidth);

	// set CFD threshold
	for(i=0; i<16; i++)
		write_reg(i*2, Threshold[i]);

	if (VMEerror) {
		printf("Error during CFD programming: ");
		printf(ErrorString);
		getch();
		return -1;
	} else {
		printf("Discriminator programmed successfully\n");
		return 0;
	}
	BaseAddress = QTPBaseAddr;
}
	  

// ************************************************************************
// Save Histograms to files
// ************************************************************************
int SaveHistograms(uint32_t histo[32][4096], int numch)
{
	int i, j;
	for(j=0; j<numch; j++) {
       		int ithADCInput=-1;
                switch (j)
                {
                case 0:
                    ithADCInput = 0;
                    break;
                case 2:
                    ithADCInput = 1;
                    break;
                case 4:
                    ithADCInput = 2;
                    break;
                case 6:
                    ithADCInput = 3;
                    break;
                case 8:
                    ithADCInput = 4;
                    break;
                case 10:
                    ithADCInput = 5;
                    break;
                case 12:
                    ithADCInput = 6;
                    break;
                case 14:
                    ithADCInput = 7;
                    break;
                default:
                    break;
                }
		FILE *fout;
		char fname[100];
		sprintf(fname, "./Histo_det_%d.txt", ithADCInput);
		fout = fopen(fname, "w"); 
		for(i=0; i<4096; i++) 
			{
				if((j-2*ithADCInput)==0)

					fprintf(fout, "%d \t %d\n", i, (int)histo[j][i] );
			}

		fclose(fout);
	}
	return 0;
}


static void findModelVersion(uint16_t model, uint16_t vers, char *modelVersion, int *ch) {
	switch (model) {
	case 792:
		switch (vers) {
		case 0x11:
			strcpy(modelVersion, "AA");
			*ch = 32;
			return;
		case 0x13:
			strcpy(modelVersion, "AC");
			*ch = 32;
			return;
		case 0xE1:
			strcpy(modelVersion, "NA");
			*ch = 16;
			return;
		case 0xE3:
			strcpy(modelVersion, "NC");
			*ch = 16;
			return;
		default:
			strcpy(modelVersion, "-");
			*ch = 32;
			return;
		}
		break;
	case 965:
		switch (vers) {
		case 0x1E:
			strcpy(modelVersion, "A");
			*ch = 16;
			return;
		case 0xE3:
		case 0xE1:
			strcpy(modelVersion, " ");
			*ch = 32;
			return;
		default:
			strcpy(modelVersion, "-");
			*ch = 32;
			return;
		}
		break;
	case 775:
		switch (vers) {
		case 0x11:
			strcpy(modelVersion, "AA");
			*ch = 32;
			return;
		case 0x13:
			strcpy(modelVersion, "AC");
			*ch = 32;
			return;
		case 0xE1:
			strcpy(modelVersion, "NA");
			*ch = 16;
			return;
		case 0xE3:
			strcpy(modelVersion, "NC");
			*ch = 16;
			return;
		default:
			strcpy(modelVersion, "-");
			*ch = 32;
			return;
		}
		break;
	case 785:
		switch (vers) {
		case 0x11:
			strcpy(modelVersion, "AA");
			*ch = 32;
			return;
		case 0x12:
			strcpy(modelVersion, "Ab");
			*ch = 32;
			return;
		case 0x13:
			strcpy(modelVersion, "AC");
			*ch = 32;
			return;
		case 0x14:
			strcpy(modelVersion, "AD");
			*ch = 32;
			return;
		case 0x15:
			strcpy(modelVersion, "AE");
			*ch = 32;
			return;
		case 0x16:
			strcpy(modelVersion, "AF");
			*ch = 32;
			return;
		case 0x17:
			strcpy(modelVersion, "AG");
			*ch = 32;
			return;
		case 0x18:
			strcpy(modelVersion, "AH");
			*ch = 32;
			return;
		case 0x1B:
			strcpy(modelVersion, "AK");
			*ch = 32;
			return;
		case 0xE1:
			strcpy(modelVersion, "NA");
			*ch = 16;
			return;
		case 0xE2:
			strcpy(modelVersion, "NB");
			*ch = 16;
			return;
		case 0xE3:
			strcpy(modelVersion, "NC");
			*ch = 16;
			return;
		case 0xE4:
			strcpy(modelVersion, "ND");
			*ch = 16;
			return;
		default:
			strcpy(modelVersion, "-");
			*ch = 32;
			return;
		}
		break;
	case 862:
		switch (vers) {
		case 0x11:
			strcpy(modelVersion, "AA");
			*ch = 32;
			return;
		case 0x13:
			strcpy(modelVersion, "AC");
			*ch = 32;
			return;
		default:
			strcpy(modelVersion, "-");
			*ch = 32;
			return;
		}
		break;
	case 1785:
		switch (vers) {
		case 0xE3:
			strcpy(modelVersion, " ");
			*ch = 16;
			return;
		}
		break;
	}
}


/******************************************************************************/
/*                                   MAIN                                     */
/******************************************************************************/
int main(int argc, char *argv[])
{
	int i, j, ch=0, chindex, wcnt, nch, pnt, ns[32], bcnt, brd_nch = 32, cnt=0;
	int quit=0, totnb=0, nev=0, DataError=0, LogMeas=0, lognum=0;
	int link=0, bdnum=0;
	int DataType = DATATYPE_HEADER;
	int EnableHistoFiles = 0;		// Enable periodic saving of histograms (once every second)
	int EnableListFile = 0;			// Enable saving of list file (sequence of events)
	int EnableRawDataFile = 0;		// Enable saving of raw data (memory dump)
	int EnableSuppression = 1;		// Enable Zero and Overflow suppression if QTP boards
	uint16_t DiscrChMask = 0;		// Channel enable mask of the discriminator
	uint16_t DiscrOutputWidth = 10;	// Output wodth of the discriminator
	uint16_t DiscrThreshold[16] = {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5};	// Thresholds of the discriminator
	uint16_t QTP_LLD[32] =	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	char c;
	char tmpConfigFileName[100] = "/config.txt";	// configuration file name
	char ConfigFileName[255] = "/config.txt";	// configuration file name
	char histoFileName[255];
	char modelVersion[3];
	uint16_t fwrev, vers, sernum, model;
	uint16_t Iped = 255;			// pedestal of the QDC (or resolution of the TDC)
	uint32_t histo[32][4096];		// histograms (charge, peak or TAC)
	uint32_t buffer[MAX_BLT_SIZE/4];// readout buffer (raw data from the board)
	uint16_t ADCdata[32];			// ADC data (charge, peak or TAC)
	uint16_t fSpecChannelPos;
	long CurrentTime, PrevPlotTime, PrevKbTime, ElapsedTime, fCurrentTime, tm;	// time of the PC
	float rate = 0.0;				// trigger rate
	FILE *of_list;				// list data file
	FILE *of_raw=NULL;				// raw data file
	FILE *f_ini;					// config file
	FILE *gnuplot=NULL;				// gnuplot (will be opened in a pipe)
	FILE *fh;						// plotting data file 

	printf("\n");
	printf("****************************************************************************\n");
	printf("                    QDC-PADC-TAC-Dicr DAQ        (BETA VERSION)             \n");
	printf("****************************************************************************\n");

#if FILES_IN_LOCAL_FOLDER
	sprintf(path,".");
#else
#ifdef  WIN32	
	sprintf(path,"%s\\QTPD_DAQ", getenv("USERPROFILE"));
	_mkdir(path);
#else
	sprintf(path,".");
#endif	
#endif

	// ************************************************************************
	// Read configuration file
	// ************************************************************************
	if (argc > 1)
	 	strcpy(tmpConfigFileName, argv[1]);
	sprintf(ConfigFileName,"%s%s", path, tmpConfigFileName);
	if ( (f_ini = fopen(ConfigFileName, "r")) == NULL ) {
		printf("Can't open Configuration File %s\n", ConfigFileName);
		getch();
		goto QuitProgram;
	}


	printf("Reading Configuration File %s\n", ConfigFileName);
	while(!feof(f_ini)) {
		char str[500];
		int data;
		
		str[0] = '#';
		fscanf(f_ini, "%s", str);
		if (str[0] == '#')
			fgets(str, 1000, f_ini);
		else {

			// Output Files
			if (strstr(str, "ENABLE_LIST_FILE")!=NULL) fscanf(f_ini, "%d", &EnableListFile);
			if (strstr(str, "ENABLE_HISTO_FILES")!=NULL) fscanf(f_ini, "%d", &EnableHistoFiles);
			if (strstr(str, "ENABLE_RAW_DATA_FILE")!=NULL) fscanf(f_ini, "%d", &EnableRawDataFile);

			// Base Addresses
			if (strstr(str, "QTP_BASE_ADDRESS")!=NULL)
				fscanf(f_ini, "%x", &QTPBaseAddr);
			if (strstr(str, "DISCR_BASE_ADDRESS")!=NULL)
				fscanf(f_ini, "%x", &DiscrBaseAddr);

			// I-pedestal
			if (strstr(str, "IPED")!=NULL) {
				fscanf(f_ini, "%d", &data);
				Iped = (uint16_t)data;
			}

			// Discr_ChannelMask
			if (strstr(str, "DISCR_CHANNEL_MASK")!=NULL) {
				fscanf(f_ini, "%x", &data);
				DiscrChMask = (uint16_t)data;
			}

			// Discr_OutputWidth
			if (strstr(str, "DISCR_OUTPUT_WIDTH")!=NULL) {
				fscanf(f_ini, "%d", &data);
				DiscrOutputWidth = (uint16_t)data;
			}

			// Discr_Threshold
			if (strstr(str, "DISCR_THRESHOLD")!=NULL) {
				int ch, thr;
				fscanf(f_ini, "%d", &ch);
				fscanf(f_ini, "%d", &thr);
				if (ch < 0) {
					for(i=0; i<16; i++)
						DiscrThreshold[i] = thr;
				} else if (ch < 16) {
					DiscrThreshold[ch] = thr; 
				}
			}

			// LLD for the QTP 
			if (strstr(str, "QTP_LLD")!=NULL) {
				int ch, lld;
				fscanf(f_ini, "%d", &ch);
				fscanf(f_ini, "%d", &lld);
				if (ch < 0) {
					for(i=0; i<32; i++)
						QTP_LLD[i] = lld;
				} else if (ch < 32) {
					QTP_LLD[ch] = lld; 
				}
			}


			// I-pedestal
			if (strstr(str, "ENABLE_SUPPRESSION")!=NULL) {
				fscanf(f_ini, "%d", &EnableSuppression);
			}
			

		}
	}
	fclose (f_ini);

	// open VME bridge (V1718 or V2718)
	if (CAENVME_Init(cvV1718, link, bdnum, &handle) != cvSuccess) {
		if (CAENVME_Init(cvV2718, link, bdnum, &handle) != cvSuccess) {
			printf("Can't open VME controller\n");
			Sleep(1000);
			goto QuitProgram;
		}
	}


	// Open output files
	if (EnableListFile) {
		char tmp[255];
		sprintf(tmp, "./List.txt");
		if ((of_list=fopen(tmp, "w")) == NULL) 
			printf("Can't open list file for writing\n");

	}
	if (EnableRawDataFile) {
		char tmp[255];
		sprintf(tmp, "./RawData.txt");
		if ((of_raw=fopen(tmp, "wb")) == NULL)
			printf("Can't open raw data file for writing\n");

	}

	// Program the discriminator (if the base address is set in the config file)
	if (DiscrBaseAddr > 0) {
		int ret;
		printf("Discr Base Address = 0x%08X\n", DiscrBaseAddr);
		ret = ConfigureDiscr(DiscrOutputWidth, DiscrThreshold, DiscrChMask);
		if (ret < 0) {
			printf("Can't access to the discriminator at Base Address 0x%08X\n", DiscrBaseAddr);
			printf("Skipping Discriminator configuration\n");
		}
	}

	// Check if the base address of the QTP board has been set (otherwise exit)
	if (QTPBaseAddr == 0) {
		printf("No Base Address setting found for the QTP board.\n");
		printf("Skipping QTP readout\n");
		getch();
		goto QuitProgram;
	}
	printf("QTP Base Address = 0x%08X\n", QTPBaseAddr);
	BaseAddress = QTPBaseAddr;

	// Open log file (for debugging)
	if (ENABLE_LOG) {
		char tmp[255];
		sprintf(tmp, "./qtp_log.txt");
		
		printf("Log file is enabled\n");
		logfile = fopen(tmp,"w");
		
	}

	// Open gnuplot (as a pipe)
#ifdef LINUX
	gnuplot = popen("/usr/bin/gnuplot", "w");
#else
	{	
		char tmp[255];
		sprintf(tmp, "%s\\pgnuplot.exe", path);
		gnuplot = _popen(tmp, "w");
	}
#endif
	if (gnuplot == NULL) {
		printf("Can't open gnuplot\n\n");
		exit (0);
	}

	// clear histograms
	for(i=0; i<32; i++) {
		ns[i]=0;
		memset(histo[i], 0, sizeof(uint32_t)*4096);
	}


	// ************************************************************************
	// QTP settings
	// ************************************************************************
	// Reset QTP board
	write_reg(0x1016, 0);
	if (VMEerror) {
		printf("Error during QTP programming: ");
		printf(ErrorString);
		getch();
		goto QuitProgram;
	}

	// Read FW revision
	fwrev = read_reg(0x1000);
	if (VMEerror) {
		printf(ErrorString);
		getch();
		goto QuitProgram;
	}

	model = (read_reg(0x803E) & 0xFF) + ((read_reg(0x803A) & 0xFF) << 8);
	// read version (> 0xE0 = 16 channels)
	vers = read_reg(0x8032) & 0xFF;

	findModelVersion(model, vers, modelVersion, &brd_nch);


	printf("Model = V%d%s\n", model, modelVersion);

	// Read serial number
	sernum = (read_reg(0x8F06) & 0xFF) + ((read_reg(0x8F02) & 0xFF) << 8);
	printf("Serial Number = %d\n", sernum);

	printf("FW Revision = %d.%d\n", (fwrev >> 8) & 0xFF, fwrev & 0xFF);

	write_reg(0x1060, Iped);  // Set pedestal
	write_reg(0x1010, 0x60);  // enable BERR to close BLT at and of block

	// Set LLD (low level threshold for ADC data)
	write_reg(0x1034, 0x100);  // set threshold step = 16
	for(i=0; i<brd_nch; i++) {
		if (brd_nch == 16)	write_reg(0x1080 + i*4, QTP_LLD[i]/16);
		else				write_reg(0x1080 + i*2, QTP_LLD[i]/16);
	}

	if (!EnableSuppression) {
		write_reg(0x1032, 0x0010);  // disable zero suppression
		write_reg(0x1032, 0x0008);  // disable overrange suppression
		write_reg(0x1032, 0x1000);  // enable empty events
	}

	//printf("Ctrl Reg = %04X\n", read_reg(0x1032));  
	printf("QTP board programmed\n");
	printf("Press any key to start\n");
	getch();
	printf("Acquisition Started...");


	// ------------------------------------------------------------------------------------
	// Acquisition loop
	// ------------------------------------------------------------------------------------
	pnt = 0;  // word pointer
	wcnt = 0; // num of lword read in the MBLT cycle
	buffer[0] = DATATYPE_FILLER;

	// clear Event Counter
	write_reg(0x1040, 0x0);
	// clear QTP
	write_reg(0x1032, 0x4);
	write_reg(0x1034, 0x4);

	PrevPlotTime = get_time();
	long TimestatDAQ=get_time();
	PrevKbTime = PrevPlotTime;
	while(!quit)  {

		CurrentTime = get_time(); // Time in milliseconds
		if ((CurrentTime - PrevKbTime) > 200) {
			c = 0;
			if (kbhit()) c=getch();
			if (c == 'r') {
				for(i=0; i<32; i++) {
					ns[i]=0;
					memset(histo[i], 0, sizeof(uint32_t)*4096);
				}
			}
			if(c == 'q') {
				quit = 1;
			}
			if(c == 'c') {
				printf("Enter new channel : ");
				scanf("%d", &ch);
			}
			if(c == 's') {
				SaveHistograms(histo, brd_nch);
				printf("Saved histograms to output files\n");
				printf("Press any key to quit DAQ\n");
				getch();
				quit =1;
			}
			PrevKbTime = CurrentTime;
		}

		// Log statistics on the screen and plot histograms
		ElapsedTime = CurrentTime - PrevPlotTime;
		long timeDAQ= get_time() - TimestatDAQ;
		//read single cycle 


		ushort ncyc=0 ;		// Number of cycles
		ushort autoinc ;	// Auto increment address
		uint32_t i, data, old_data=0;
		int Id, ith;
		CVErrorCodes    ret, old_ret=cvSuccess;
		CVDataWidth dtsize = cvD16;
		//uint16_t NEvts, EvtNo;


		bool DataReady;
		DataReady=false;
       		for (i=0;i<1000;++i)
             	{
            		 //CheckDataReady(BHandle,&DataReady);
             		CAENVME_ReadCycle(handle,BaseAddress+0x110E,&data,cvA24_U_DATA,cvD32);
             		if (data&1) {DataReady=true; break;}
	     		//Come out of the loop when there is at least one event in the Output Buffer.
             	}
		if (DataReady==false) 
		{
			printf("No event in the Output Buffer\n");
			break;
		}
		if (ncyc==0)
		{
			ret = CAENVME_ReadCycle(handle,BaseAddress,&data,cvA32_U_DATA,dtsize);	
			CurrentTime = get_time(); //milisecond
			//if((i==0) || (ret != old_ret))
			{
				switch (ret)
				{

					FILE*of_list; 
					char tmp[255];
					sprintf(tmp, "./List.txt");
					of_list = fopen(tmp, "w+");	


					case cvSuccess: printf(" Cycle(s) completed normally\n");
					if((i==0) || (old_data != data))  
                                   	{ 
							int Poschannel = data & 0x1FFF;
							histo[ith][Poschannel]++;
							ns[ith]++;
							printf("Data Read : 0x%04X \t %d \n",data, Poschannel);
/*
             					FILE*of_list; 
						char tmp[255];
						sprintf(tmp, "./List.txt");
						of_list = fopen(tmp, "w+");	
*/
                                    		//printf(" Data Read : 0x%08X  \n",data);

						Id=(int) ((data>>24)&0x7);
						printf("Id: %d\n", Id);
						if (Id==0){ //digitized data
	
							ith = 	(int)((data>>17)&0x3F);  //16 channels
							printf("ith: %d\n", ith);
							int ithADCInput = -1;
               
                					switch (ith)
                						{
                						case 0: ithADCInput = 0;
                    							break;
						                case 2: ithADCInput = 1;
                   							break;
								case 4: ithADCInput = 2;
                   							break;
								case 6: ithADCInput = 3;
								    	break;
								case 8: ithADCInput = 4;
								    	break;
								case 10:ithADCInput = 5;
								    	break;
								case 12:ithADCInput = 6;
								    	break;
								case 14:ithADCInput = 7;
								    	break;
								default:
								    	break;
								}
							

							printf( "%d \t %d \t 0x%08X \t %d\n",i,ithADCInput,data, Poschannel);


						}
						else if (Id==1) 
							printf("Valid Channels=%d\n",(data&0xFFF));                   //Header
						else if (Id==3) 
							printf("%d Event Number=%d\n",i,(data&0x3FFFFFFF));     //End Of Event 
						else printf("Invalid readout\n");                                //Invalid Id 
		
						break ;

					}

					case cvBusError:printf("Bus Error!\n");
						break;
					case cvCommError:printf("Communication Error!\n"); 
						break;
					default:printf("Unknown Error!\n"); 
						break;
				}
			}

				old_data = data;
				old_ret = ret;

				BaseAddress += dtsize;                       // Increment address (+1 or +2 or +4) 

				
			//fclose(of_list);

    		}
    
		

		


		if (ElapsedTime > 1000) {
			rate = (float)nev / ElapsedTime;
			ClearScreen();
			printf("Acquired %d events on det %d\n", ns[ch], ch/2);
			printf("Time of DAQ (min) %0.2f\n", timeDAQ/(1000.*60.));
			if (nev > 1000)
				printf("Trigger Rate = %.2f KHz\n", (float)nev / ElapsedTime);
			else
				printf("Trigger Rate = %.2f Hz\n", (float)nev * 1000 / ElapsedTime);
			if (totnb > (1024*1024))
				printf("Readout Rate = %.2f MB/s\n", ((float)totnb / (1024*1024)) / ((float)ElapsedTime / 1000));
			else
				printf("Readout Rate = %.2f KB/s\n", ((float)totnb / 1024) / ((float)ElapsedTime / 1000));

			nev = 0;
			totnb = 0;
		
			printf("\n\n");
			sprintf(histoFileName, "./histo.txt");
			fh = fopen(histoFileName,"w");
			for(i=0; i<4096; i++) {
				fprintf(fh, "%d \t %d\n", i, (int)histo[ch][i]);
			}
			fclose(fh);
			fprintf(gnuplot, "set ylabel 'Counts'\n");			
			fprintf(gnuplot, "set xlabel 'ADC channels'\n");
			fprintf(gnuplot, "set yrange [0:]\n");
			fprintf(gnuplot, "set grid\n");
			fprintf(gnuplot, "set title 'Det. %d (Rate = %.3fKHz, counts = %d)'\n", ch/4, rate, ns[ch]);
			fprintf(gnuplot, "plot './histo.txt' with step\n");
			fflush(gnuplot);
			printf("[q] quit  [r] reset statistics  [s] save histograms [c] change plotting channel\n");
			PrevPlotTime = CurrentTime;
			if (EnableHistoFiles) SaveHistograms(histo, brd_nch);
		}

	//fclose(of_list);	
	}
//fclose(of_list);
	if (EnableHistoFiles) {
		SaveHistograms(histo, brd_nch);	
		printf("Saved histograms to output files\n");
	}

//fclose(of_list);
// ------------------------------------------------------------------------------------

QuitProgram:
	if (of_list != NULL) fclose(of_list);
	if (of_raw != NULL) fclose(of_raw);
	if (gnuplot != NULL) fclose(gnuplot);
	if (handle >= 0) CAENVME_End(handle);
}

