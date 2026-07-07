// BertecExample.cpp : this example code shows how to use the data gathering process
// with the Bertec Device DLL. The command line program allows you to use either callbacks
// or data polling, logging output to a file or to the screen for a given number of seconds.
//

#include <iostream>
#include <time.h>
#include <windows.h>
#include <string.h>
#include <vector>
#include "bertecif.h"

// Depending on the target platform, stricmp/strnicmp is called different things.
#ifdef _WIN32
#include <tchar.h>
#define stricmp _stricmp
#define strnicmp _strnicmp
#else
#define stricmp strcasecmp
#define strnicmp strncasecmp
#endif

#pragma warning(disable:4996)

struct IMFILE
{
	bool needHeader;
	FILE* pFile;
};

// the aux pin mode is used based on the -s MODE option. By default, the example code will always collect data, ignoring the AUX signal input
// (only available on external amp such as the AM6500, AM6800, and AM6817).
// Note that using the EXT or INT sync modes can override this and instead use the data stream control for the aux filtering.
// See StartDataStream for more.
bertec_DataStreamControl::AuxPinMode onlyRecordWhenAuxed = bertec_DataStreamControl::AuxPinMode::AUXPINMODE_NONE;  // set via the -s mode option; if non-zero, will only write out when the aux pin is in the correct state

bool writeHeader = true;   // this is used in the DataCallback code to write out the .CSV header for the file
bool includeSyncAux = false;  // true if the -y option is passed
bool useDeviceImmediateMode = false;	// if true, then the main callback or polling is not used and each device is written to a seperate file

int limitChannels = 0;     // set via the -l N option; if non-zero, will only write out the first N channels
bertec_DataStreamControl::SyncPinMode useDeviceSync = bertec_DataStreamControl::SyncPinMode::SYNCPINMODE_NONE;

// The data callback will be passed the already existing FILE pointer in the userData.
// This example shows how this is used by typecasting userData to the proper data type.
// The same concept can also be applied to a C++ object.
void CALLBACK DataCallback( bertec_Handle bHand, const bertec_DataFrame * dataFrame, void * userData )
{
   FILE* pFile = (FILE*)userData;
   if ((pFile != nullptr) && (dataFrame->deviceCount > 0))
   {
      if (writeHeader)
      {
         const bool hasMults = (dataFrame->deviceCount > 1);
         // For multiple devices the general convention is to append the device index as the suffix to each channel name.
         for (int devNum = 0; devNum < dataFrame->deviceCount; ++devNum)
         {
            const int suffix = devNum + 1;
            if (hasMults)
            {
               if (devNum > 0)
                  fprintf( pFile, "," );
               fprintf( pFile, "Timestamp-%d", suffix );
            }
            else
               fprintf( pFile, "Timestamp" );

				if (useDeviceSync != bertec_DataStreamControl::SyncPinMode::SYNCPINMODE_NONE)
				{
					if (hasMults)
					{
						if (devNum > 0)
							fprintf( pFile, "," );
						fprintf( pFile, ",EventCounter-%d,FrameCounter-%d", suffix, suffix );
					}
					else
						fprintf( pFile, ",EventCounter,FrameCounter" );
				}

            if (includeSyncAux)
            {
               if (hasMults)
                  fprintf( pFile, ",Sync-%d,Aux-%d", suffix, suffix );
               else
                  fprintf( pFile, ",Sync,Aux" );
            }

            char channelNames[BERTEC_MAX_CHANNELS][BERTEC_MAX_CHANNELNAME_LENGTH + 1];  /* the channel names from the device's eprom, null terminated */
            int channelCount = bertec_GetDeviceChannels( bHand, devNum, &channelNames[0][0], sizeof( channelNames ) );
            if (limitChannels > 0 && limitChannels < channelCount)
               channelCount = limitChannels;
            for (int col = 0; col < channelCount; ++col)
            {
               if (hasMults)
                  fprintf( pFile, ",%s-%d", channelNames[col], suffix );
               else
                  fprintf( pFile, ",%s", channelNames[col] );
            }
         }
         fprintf( pFile, "\n" );
         writeHeader = false;
      }


      bool needLF = false;
      for (int devNum = 0; devNum < dataFrame->deviceCount; ++devNum)
      {
         const bertec_DeviceData& devData = dataFrame->device[devNum];

         // Check if the -s <mode> was passed
         // The better way is to use the bertec_DataStreamControl::auxPinMode mode set to AUXPINMODE_RUNHIGH or AUXPINMODE_RUNLOW,
         // but this is provided to show how you can "do it yourself".
         if (onlyRecordWhenAuxed != bertec_DataStreamControl::AuxPinMode::AUXPINMODE_NONE)
         {
            // Both the SYNC and AUX pins come in as an 8-bit pattern, and they can change state at any time during the data sampling.
            // This you can have a value like 126, which is a bit pattern of 0111_1110 - meaning the signal was 'pulsed' in the middle of a data read.
            // For the purposes of this example, the code treats the AUX pin held low for the entire sample (0) or held high (255).
            // If the -s HIGH option is passed, then if the AUX bit pattern is anything other than 0, the data is recorded (meaning at some point
            // the AUX signal was raised high). If -s LOW was passed, then if the AUX pattern is anything other than 255, then the data is recorded
            // (meaning the AUX signal was pulled low at some point). Using the External Clock Mode (-e) will only present AUX as either 0 or 255;
            // in this case, the code below will still work.
            
            // The code presented here is trivial and is to be used as an example only; your project may require more sophisticated pattern handling.

            // Note that the logic here appears backwards from the above text: that's because of the continue statements being used to skip recording.
            // The logic below can also be combined into a single else statement, but was left 'expanded' to make the logic and code flow clear.
            if (onlyRecordWhenAuxed == bertec_DataStreamControl::AuxPinMode::AUXPINMODE_RUNHIGH)
            {
               if (devData.additionalData.auxData == 0)
                  continue;   // pin never went high, skip the output
            }
            else if (onlyRecordWhenAuxed == bertec_DataStreamControl::AuxPinMode::AUXPINMODE_RUNLOW)
            {
               if (devData.additionalData.auxData == 255)
                  continue;   // pin never went low, skip the output
            }
         }

         int channelCount = devData.channelData.count;

         if (limitChannels > 0 && limitChannels < channelCount)
            channelCount = limitChannels;

         if (channelCount > 0)
         {
            needLF = true;
				if (devNum > 0)
					fprintf( pFile, "," );

				fprintf( pFile, "%f", (double)devData.additionalData.timestamp );

				if (useDeviceSync != bertec_DataStreamControl::SyncPinMode::SYNCPINMODE_NONE)
					fprintf( pFile, ",%f,%f", (double)devData.additionalData.eventCounter, (double)devData.additionalData.frameCounter );

				if (includeSyncAux)
					fprintf( pFile, ",%d,%d", devData.additionalData.syncData, devData.additionalData.auxData );

            for (int col = 0; col < channelCount; ++col)
               fprintf( pFile, ",%f", devData.channelData.data[col] );
         }
      }
      if (needLF)
         fprintf( pFile, "\n" );
   }
}


// The data callback will be passed the already existing FILE pointer in the userData.
// This example shows how this is used by typecasting userData to the proper data type.
// The same concept can also be applied to a C++ object.
void CALLBACK ImmediateDeviceDataCallback( bertec_Handle bHand, int deviceIndex, const char* uid, const bertec_DeviceData* deviceData, void* userData )
{
	std::vector<IMFILE>& immedFiles = *((std::vector<IMFILE>*)userData);
	if ((deviceIndex >= 0) && ((size_t)deviceIndex < immedFiles.size()))
	{
		FILE* iFile = immedFiles[deviceIndex].pFile;

		int channelCount = deviceData->channelData.count;

		if (channelCount > 0)
		{
			if (immedFiles[deviceIndex].needHeader)
			{
				fprintf( iFile, "Timestamp,EventCounter,FrameCounter" );

				if (includeSyncAux)
					fprintf( iFile, ",Sync,Aux" );

				char channelNames[BERTEC_MAX_CHANNELS][BERTEC_MAX_CHANNELNAME_LENGTH + 1];  /* the channel names from the device's eprom, null terminated */
				int channelCount = bertec_GetDeviceChannels( bHand, deviceIndex, &channelNames[0][0], sizeof( channelNames ) );
				for (int col = 0; col < channelCount; ++col)
				{
					fprintf( iFile, ",%s", channelNames[col] );
				}
				fprintf( iFile, "\n" );
				immedFiles[deviceIndex].needHeader = false;
			}


			fprintf( iFile, "%f", (double)deviceData->additionalData.timestamp );

			fprintf( iFile, ",%f,%f", (double)deviceData->additionalData.eventCounter, (double)deviceData->additionalData.frameCounter );

			if (includeSyncAux)
				fprintf( iFile, ",%d,%d", deviceData->additionalData.syncData, deviceData->additionalData.auxData );

			for (int col = 0; col < channelCount; ++col)
				fprintf( iFile, ",%f", deviceData->channelData.data[col] );
			fprintf( iFile, "\n" );
		}
	}
}

// Portable version of Windows Sleep function
void WaitForXmilliseconds(int milliseconds )
{
#ifdef _WIN32
   Sleep( milliseconds );
#else
   struct timespec ts;
   ts.tv_sec = milliseconds / 1000;
   ts.tv_nsec = (milliseconds % 1000) * 1000000;
   nanosleep( &ts, nullptr );
#endif
}

int WaitForDevicesToAppear( bertec_Handle hand )
{
	printf( "Waiting for devices..\n" );

	// This simply waits until the stats returns a state of READY. This block through bertec_SetExternalClockMode call
	// could also be made into a callback handler tied to bertec_RegisterStatusCallback, which is the preferred method since it is less
	// likely to miss a state change.
	while (bertec_GetStatus( hand ) != BERTEC_DEVICES_READY)
	{
		// Waiting for devices....
		//printf( ".\n" );
		WaitForXmilliseconds( 100 );
		if (bertec_GetDeviceCount( hand ) > 0)
			break;   // also can check like this (but we prefer status callbacks since we can get instant results)
	}
	printf( " done\n" );

	// since we're going to be using this a lot, copy it off
	const int numberOfDevices = bertec_GetDeviceCount( hand );

	for (int devNum = 0; devNum < numberOfDevices; ++devNum)
	{
		char buffer[256] = "";
		bertec_GetDeviceSerialNumber( hand, devNum, buffer, sizeof( buffer ) );
		printf( "Plate serial %s\n", buffer );
	}

	return numberOfDevices;
}

int StartDataStreaming( bertec_Handle hand, int intClockRate )
{
	// Starting with the 2.50 version of the SDK, the Library will no longer start delivering data the instant it detects devices;
// this changes was done to improve how the system interacts with various end-user projects and to make it clear when data starts and stops.
// The control block also allows various modes of operation - see the documentation on what all these are and how to use them.
// For this example, the data stream is started in 'classical' non-synchronized mode.

	bertec_DataStreamControl streamControl = { 0 };
	streamControl.size = sizeof( streamControl );   // if you don't set the size properly, the SDK will reject your call.
	streamControl.syncPinMode = bertec_DataStreamControl::SyncPinMode::SYNCPINMODE_NONE;  // no special control is done; the pins will be read as-is (if hardware supports it)
	streamControl.auxPinMode = bertec_DataStreamControl::AuxPinMode::AUXPINMODE_NONE;      // and passed in as part of the bertec_DataFrame structure.
	streamControl.deviceFilterBitmask = 0; // for non-zero values, any bit that is turned on will allow data through and off will not. This will affect the bertec_DataFrame structure.
	streamControl.internalClockSource = 0; // ignored unless streamControl.syncPinMode == SYNCPINMODE_INTCLOCK
	streamControl.internalClockFrequency = 0;// ignored unless streamControl.syncPinMode == SYNCPINMODE_INTCLOCK



	int err = bertec_CanStartDataStream( hand, &streamControl );
	if (err != BERTEC_NOERROR)
		return err;	// tried to do sync with hardware that doesn't support (or the detection failed)

	if (useDeviceSync != bertec_DataStreamControl::SyncPinMode::SYNCPINMODE_NONE)
	{

		printf( "Using SYNC pin as sync control\n" );
		// Set up the sync mode to have one device generate the clock pulses, and the other devices resample on that. 
		// By setting the clock frequency to 1000hz, the resample rate will match the actual hardware rate.
		// The clock source (the pulse generator) will be the first device; you can change this to any device index you want or need.
		streamControl.syncPinMode = useDeviceSync;
		if (useDeviceSync == bertec_DataStreamControl::SyncPinMode::SYNCPINMODE_INTCLOCK ||
			useDeviceSync == bertec_DataStreamControl::SyncPinMode::SYNCPINMODE_INTAUXCONTROL)
		{
			streamControl.internalClockFrequency = (intClockRate > 0) ? intClockRate : 1000.0f;
			printf( "Using Internal Clock Rate of %d\n", (int)streamControl.internalClockFrequency );
		}

		streamControl.auxPinMode = onlyRecordWhenAuxed;	// set up what we are using here for the sync line
		onlyRecordWhenAuxed = bertec_DataStreamControl::AuxPinMode::AUXPINMODE_NONE;	// and reset this to nothing so the capture code doesn't filter it

		if (useDeviceSync == bertec_DataStreamControl::SyncPinMode::SYNCPINMODE_INTAUXCONTROL ||
			useDeviceSync == bertec_DataStreamControl::SyncPinMode::SYNCPINMODE_EXTAUXCONTROL)
		{
			// if the aux mode is not set for the INTA or EXTA modes, default to a 'push button toggle' mode
			if (streamControl.auxPinMode == bertec_DataStreamControl::AuxPinMode::AUXPINMODE_NONE)
				streamControl.auxPinMode = bertec_DataStreamControl::AuxPinMode::AUXPINMODE_START_PULSE_ONOFF;
		}
	}
	else
	{
		printf( "Not using SYNC pin for sync control\n" );
	}

	printf( "Starting the data stream...\n" );
	return bertec_StartDataStream( hand, &streamControl );
}

//////////////////////////////////////////////////////////////////////////
#ifdef _WIN32
int _tmain( int argc, _TCHAR* argv[] )
#else
int  main( int argc, char* argv[] )
#endif
{
   char filename[MAX_PATH] = "";
   FILE* pFile = nullptr;
	std::vector<IMFILE> immedFiles;

   bool perfTestMode = false;

   bool useCallbacks = false;
   bool usePolling = false;
   int runTimeSeconds = 0;
   bool useAutozeroing = false;
   bool startWithZeroLoad = false;
   int intClockRate = 1000;
   int resampleRate = 0;
	int retryStreamStartTimes = 0;
	char cmdLineExec[MAX_PATH] = "";

   bertec_ClockSourceFlags extClock = bertec_ClockSourceFlags::CLOCK_SOURCE_INTERNAL;

   limitChannels = 0;
   includeSyncAux = false;
   writeHeader = true;

   if (argc < 2)
   {
showhelp:
      printf( "Bertec Device Example:\n" );
      printf( "-f <filename>   output to the given filename\n" );
      printf( "-t <seconds>    run for the given # of seconds\n" );
      printf( "-l <num>        limit to first num channels per row\n" );
      printf( "-y              include the sync/aux values\n" );
      printf( "-c              do callbacks\n" );
      printf( "-p              do polling\n" );
      printf( "-a              turn on autozeroing\n" );
      printf( "-z              zero load before data gather\n" );
		printf( "-!              use immediate data mode to output plate data to seperate files\n" );
		printf( "-m <mode> <n>   turn on multiple device sync mode. Requires amps with the SYNC line connected. Mode is EXT or INT (1000hz unless n is set)\n" );
      printf( "-e <mode>       use the external sync clock. Mode is either NONE, RISE, FALL, or BOTH\n" );
      printf( "-s <mode>       use aux pin to control data sample. Mode is either NONE, HIGH, or LOW\n" );
      printf( "-r <value>      use the given resample rate <value> to downsample or upsample the data (ignores -e and -s)\n" );
		printf( "-X <cmd>        executes the given command line process once the data stream is started.\n" );
		printf( "-R <value>      retries the data stream <value> times in case it fails. This will also re-init devices if the detected capabilities do not match request\n" );
		return 0;
   }

   int i = 1;
   while (i < argc)
   {
      char *item = argv[i];
      char *parm = argv[i + 1];
      ++i;
      if (item[0] == '-')
      {
         switch (item[1])
         {
            case '@':
               perfTestMode = true;
               break;
            case 'f':
               if (parm != nullptr)
               {
                  strncpy( filename, parm, sizeof( filename ) );
                  ++i;
               }
               break;
            case 't':
               if (parm != nullptr)
               {
                  runTimeSeconds = atoi( parm );
                  ++i;
               }
               break;
            case 'l':
               if (parm != nullptr)
               {
                  limitChannels = atoi( parm );
                  ++i;
               }
               break;
            case 'y':
               includeSyncAux = true;
               break;
            case 'c':
               useCallbacks = true;
               usePolling = false;
               break;
            case 'p':
               useCallbacks = false;
               usePolling = true;
					break;
				case '!':
					useCallbacks = true;
					usePolling = false;
					useDeviceImmediateMode = true;
					break;
            case 'a':
               useAutozeroing = true;
               break;
            case 'z':
               startWithZeroLoad = true;
               break;
            case 'm':
               if (parm != nullptr)
               {
                  if ((stricmp( parm, "INT" ) == 0) || (stricmp( parm, "INTA" ) == 0))
                  {
                     useDeviceSync = bertec_DataStreamControl::SyncPinMode::SYNCPINMODE_INTCLOCK;
							if (stricmp( parm, "INTA" ) == 0)
								useDeviceSync = bertec_DataStreamControl::SyncPinMode::SYNCPINMODE_INTAUXCONTROL;
                     if (i+1 < argc)
                     {
                        if (argv[i + 1][0] !='-')
                        {
                           int nRate = atoi( argv[i + 1] );
                           if (nRate != 0)
                              intClockRate = nRate;
                           ++i;
                        }
                     }

                  }
						else if ((stricmp( parm, "EXT" ) == 0) || (stricmp( parm, "EXTA" ) == 0))
						{
							useDeviceSync = bertec_DataStreamControl::SyncPinMode::SYNCPINMODE_EXTCLOCK;
							if (stricmp( parm, "EXTA" ) == 0)
								useDeviceSync = bertec_DataStreamControl::SyncPinMode::SYNCPINMODE_EXTAUXCONTROL;
						}
                  ++i;
               }
               break;
            case 'e':
               if (parm != nullptr)
               {
                  if (stricmp( parm, "RISE" ) == 0)
                     extClock = CLOCK_SOURCE_EXT_RISE;
                  else if (stricmp( parm, "FALL" ) == 0)
                     extClock = CLOCK_SOURCE_EXT_FALL;
                  else if (stricmp( parm, "BOTH" ) == 0)
                     extClock = CLOCK_SOURCE_EXT_BOTH;
                  else
                     extClock = CLOCK_SOURCE_INTERNAL;
                  ++i;
               }
               break;
            case 's':
               if (parm != nullptr)
               {
						if (stricmp( parm, "HIGH" ) == 0)
							onlyRecordWhenAuxed = bertec_DataStreamControl::AuxPinMode::AUXPINMODE_RUNHIGH;
                  else if (stricmp( parm, "LOW" ) == 0)
                     onlyRecordWhenAuxed = bertec_DataStreamControl::AuxPinMode::AUXPINMODE_RUNLOW;
						else if (stricmp( parm, "PULSE" ) == 0)	// these are only for the data stream control for EXTA/INTA sync modes. This is the default for those.
							onlyRecordWhenAuxed = bertec_DataStreamControl::AuxPinMode::AUXPINMODE_START_PULSE_ONOFF;
						else if (stricmp( parm, "PULSEON" ) == 0)	// both PULSE and PULSEON are not supported/ignored for non-sync modes.
							onlyRecordWhenAuxed = bertec_DataStreamControl::AuxPinMode::AUXPINMODE_START_PULSE_ON;
						else
                     onlyRecordWhenAuxed = bertec_DataStreamControl::AuxPinMode::AUXPINMODE_NONE;
                  ++i;
               }
               break;
            case 'r':
               if (parm != nullptr)
               {
                  resampleRate = atoi( parm );
                  ++i;
               }
               break;
				case 'X':
					if (parm!=nullptr)
					{
						strncpy( cmdLineExec, parm, sizeof( cmdLineExec ) );
						++i;
					}
				break;
				case 'R':
				{
					if (parm != nullptr)
					{
						retryStreamStartTimes = atoi( parm );
						++i;
					}
				}
				break;
         }
      }
   }

	if (retryStreamStartTimes < 1)
		retryStreamStartTimes = 1;

   if (perfTestMode)
   {
      runTimeSeconds = 30;
      usePolling = false;
      useCallbacks = false;
		useDeviceImmediateMode = false;
      filename[0] = 0;
      printf( "Performing no-data-capture perftest for %d seconds.\n", runTimeSeconds );
   }
   else
   {
      if (runTimeSeconds < 1)
         goto showhelp;
      if (usePolling == useCallbacks)
         goto showhelp;
      if (filename[0] == 0)
         goto showhelp;

		if (filename[0] == '!')
		{
			printf( "Performing no-data-capture test for %d seconds.\n", runTimeSeconds );
		}
		else
		{
			pFile = fopen( filename, "wt" );
			if (pFile == nullptr)
			{
				printf( "Unable to open filename %s for writing.\n", filename );
				return -1;
			}
		}
   }

   printf( "Initializing the library...\n" );

   bertec_Handle hand = bertec_Init();

   if (hand == nullptr)
   {
      if (pFile != nullptr)
         fclose( pFile );
      printf( "Unable to initialize the Bertec Device Library (possible missing FTD2XX install).\n" );
      return -1;
   }

   printf( "Calling set autozero with a flag of %d...\n", useAutozeroing ? 1 : 0 );

   bertec_SetEnableAutozero( hand, useAutozeroing ? 1 : 0 );

   printf( "Calling Start...\n" );

   bertec_Start( hand );
   // At this point the SDK will attempt to find any connected USB force devices and start reading data from them.
   // Data will buffer as soon as it becomes available, will can be read by data polling or callback functionality.

	int startStreamResult = BERTEC_GENERIC_ERROR;
	for (int streamRetry = 0; streamRetry < retryStreamStartTimes; ++streamRetry)
	{
		// since we're going to be using this a lot, copy it off
		const int numberOfDevices = WaitForDevicesToAppear( hand );

		if (numberOfDevices > 0 && useDeviceImmediateMode)
		{
			printf( "Using immediate device data callbacks to gather data.\n" );
			const int numberOfDevices = bertec_GetDeviceCount( hand );
			for (int i = 0; i < numberOfDevices; ++i)
			{
				char imfilename[MAX_PATH] = "";
				char* dot = strrchr( filename, '.' ); // find last dot
				if (dot)
				{
					auto pos = dot - filename; // calculate position
					strncpy( imfilename, filename, pos ); // copy filename up to dot
					sprintf( imfilename + pos, "-%d%s", i + 1, dot ); // append dash, number and extension
				}
				else
				{
					sprintf( imfilename, "%s-%d", filename, i + 1 ); // no extension, append dash and number at the end
				}
				immedFiles.push_back( { true,fopen( imfilename, "wt" ) } );
			}

			//NOTE: this will only write what will be received, not any buffered data. This means that between this call and the call to
			// the standard bertec_RegisterDataStreamCallback or device polling will result in slightly different points in time in regards
			// to the data being output or written; the bertec_RegisterDataStreamCallback and polling will always have any data that was collected
			// prior to that call, up to the point of the Max Buffer Size (see bertec_ChangeMaxBufferedDataSize, below), while ImmediateDeviceDataCallback
			// will only get FRESH data before it is passed into the internal buffer + DataStreamCallback. 
			//NOTE: since this is being called BEFORE the bertec_StartDataStream (see StartDataStreaming), this callback will ALSO get all
			// the data that is being handled during the data stream setup process. At this point you are now getting immediate device data BEFORE
			// the data stream handler itself, so instead being behind the buffer, you are now ahead of it.
			bertec_RegisterImmediateDeviceDataCallback( hand, ImmediateDeviceDataCallback, &immedFiles );
		}

		// A special case handling for the command line parm regarding the buffered data is here:
		// by default the buffer uses a small size (100 to 500 samples) before it starts spilling data; this can be a
		// problem when the command line parm program takes a long time to complete and return - and using polling
		// instead of callbacks will make it worse. For cases like this, setting the buffer size to the max limit
		// of 10000 (10 seconds) is suggested.
		if (cmdLineExec[0])
		{
			int bufferSize = 10000;
			printf( "Setting buffer size to %d samples\n", bufferSize );
			bertec_ChangeMaxBufferedDataSize( hand, bufferSize );	// calling this will cause your memory footprint to
			// grow in size by roughly 170*#ofDevices*bufferSize bytes.
		}

		// Large device counts can cause issues on some systems that lack appropriate CPUs to handle the needed workload.
		// To work around this, increase the buffer size from the default 500 (or 10,000) to the abs max of 60,000
		// This bombs memory hard, so avoid doing this on a 32 bit system if you can.
		if (numberOfDevices >= 32)
		{
			int bufferSize = 60000;
			printf( "Setting buffer size to %d samples\n", bufferSize );
			bertec_ChangeMaxBufferedDataSize( hand, bufferSize );
		}

		// StartDataStreaming will check if the passed parms can be used with the devices; if it cannot, then BERTEC_UNSUPPORTED_COMMAND will be returned
		// In this case, it is assumed the user knows the devices can support the sync mode but something is preventing the hardware from telling the SDK this.
		// The best solution is to simply re-detect all the devices and try again.
		startStreamResult = StartDataStreaming( hand, intClockRate );
		
		if (startStreamResult == BERTEC_NOERROR || startStreamResult == BERTEC_STREAM_SUCCESSFUL)
			break;
		else if (startStreamResult == BERTEC_UNSUPPORTED_COMMAND)
		{
			printf( "Unable to start the data stream due to incompatible settings. Assuming hardware is supposed to support this, so redetecting devices and trying again in 5 seconds\n");
			bertec_RedetectConnectedDevices( hand );
		}
		else
			printf( "Unable to start the data stream, error was %d (probably due to noise on the sync line). Waiting 5 seconds and trying again\n", startStreamResult );
		Sleep( 5000 );
	}

	if (startStreamResult != BERTEC_NOERROR && startStreamResult != BERTEC_STREAM_SUCCESSFUL)
	{
		printf( "Unable to start the data stream, quitting\n" );
		return startStreamResult;
	}

	// Running a program at this point of data collection is provided primarily as an example; your code would obviously not do this,
	// but instead trigger some other function or signaling event that would perform additional work.
	// Note that the system() call will block until cmdLineExec exits; this includes a UI block if the cmdLineExec
	// program crashes or throws an exception and a debugger is showing you the exception ui.
	// You can replace this with a more OS-specific version (ex: ShellExecuteEx) that does an asynchronous call if you want.
	if (cmdLineExec[0])
	{
		printf( "Executing command line %s\n", cmdLineExec );
		system( cmdLineExec );
	}

	if (useDeviceSync == bertec_DataStreamControl::SyncPinMode::SYNCPINMODE_INTCLOCK ||
		useDeviceSync == bertec_DataStreamControl::SyncPinMode::SYNCPINMODE_INTAUXCONTROL)
   {
      printf( "Internal sync mode set\n" );
   }

   if (startWithZeroLoad)
   {
      printf( "Zeroing load..\n" );
      bertec_ZeroNow( hand );
      int cnt = 0;
      bertec_AutozeroStates st = AUTOZEROSTATE_NOTENABLED;
      while ((st = bertec_GetAutozeroState( hand )) != AUTOZEROSTATE_ZEROFOUND)
      {
         // Waiting for zero to happen.
         ++cnt;
         if (cnt >= 2)
         {
            printf( ".\n" );
            cnt = 0;
         }
         WaitForXmilliseconds( 100 );
      }
      printf( " done\n" );
   }

   if (resampleRate > 0)
   {
      // Set the resampling rate. This will automatically reset the clock and aux modes to zero.
      printf( "Setting the resampling rate to %d\n", resampleRate );
		const int numberOfDevices = bertec_GetDeviceCount( hand );
      for (int devNum = 0; devNum < numberOfDevices; ++devNum)
      {
         bertec_SetDataRateResampling( hand, devNum, resampleRate );
      }

      onlyRecordWhenAuxed = bertec_DataStreamControl::AuxPinMode::AUXPINMODE_NONE;
   }
   else
   {
      // Set the external clock mode, if any. If the device does not support external clocking modes, then this
      // function call will return BERTEC_UNSUPPORED_COMMAND. Normally you will not want do this and instead use the bertec_DataStreamControl,
      // but this is still provided.
      if (extClock != bertec_ClockSourceFlags::CLOCK_SOURCE_INTERNAL)
      {
         printf( "Setting external clock mode to %d\n", extClock );
			const int numberOfDevices = bertec_GetDeviceCount( hand );
         for (int devNum = 0; devNum < numberOfDevices; ++devNum)
         {
            bertec_SetExternalClockMode( hand, devNum, extClock );

            // Set the aux pin mode to from the aux pin or the zero pin, depending on the amp - this is the default state, so this is only provided as example code.
            bertec_SetAuxPinMode( hand, devNum, AUX_NONE_AUX_IN_ZERO ); // for AM6500's, this is the aux pin. For AM6800/AM6817, this will be the zero pin.
         }
      }
   }

	// If you wish to use the AUX pin as an OUTPUT, you would pass AUX_OUT_INSTANT and then call bertec_SetSyncAuxPinValues with your values.

	if (onlyRecordWhenAuxed == bertec_DataStreamControl::AuxPinMode::AUXPINMODE_RUNHIGH)
		printf( "Only recording data when the AUX pin is high\n" );
	if (onlyRecordWhenAuxed == bertec_DataStreamControl::AuxPinMode::AUXPINMODE_RUNLOW)
		printf( "Only recording data when the AUX pin is low\n" );

   // When using callbacks, we can simply use the Windows sleep function call to suspend our main thread
   // while the callback thread does all the work for us. Note the use of pFile being passed as the user
   // data value.
   // Also note that callbacks SHOULD be set up before calling bertec_Start() so that they are available
   // for use as soon as data is present. In your own code you can use a program-specific flag to determine
   // if data should be retained or discarded. Callbacks are also the method of choice when using a GUI
   // system or any other main-thread blocking functionality (ex: heavy database usage or network functionality).
	// As a further note, setting the callback will cause all existing buffered data to be immediately sent to the
	// callback (so no data will be lost, up to the capacity of the data buffer - see bertec_ChangeMaxBufferedDataSize
   if (useCallbacks)
   {
      printf( "Using callbacks to gather data.\n" );
      bertec_RegisterDataStreamCallback( hand, DataCallback, pFile );	// note that this will instantly dump anything buffered from when the data stream was started

      // This is a very poor way to capture the data for a set amount of time; the Windows Sleep() function is
      // fairly coarse-grained, so it might not be the exact amount.
      // A much better way would be in your code to use the timestamp value in the data frame to determine when
      // the device sent the data, or count the number of samples (one sample equals 1ms, unless you have elected
      // to enable the downsampling option or the external clock modes).

      WaitForXmilliseconds( 1000 * runTimeSeconds ); // in your code, you could do real work here.
   }

   // When polling, this is slightly more work since we need to go and check the timer ourselves.
   else if (usePolling)
   {
      printf( "Using polling to gather data.\n" );

      // When polling, your code will need to allocate enough buffer space for the currently connected devices.
      // The SDK provides a helper function bertec_AllocateReadBufferedData that simply gets the device count and
      // allocates enough room so that bertec_ReadBufferedData will be able to copy the buffer in.
      // If you set the USE_SDK_ALLOCATOR to TRUE then this SDK function will be used; set this to FALSE to use the
      // provided example code that is the same as the SDK code but it fixed to only allocate 4 devices (the SDK
      // code reads the # of devices count and allocates using that value instead).
      
      // As long as the buffer is big enough, bertec_ReadBufferedData will fill it - for example, the sample code
      // allocates a buffer sized for 4 devices but if only 1 is connected, then bertec_ReadBufferedData will only
      // populate the first device results, leaving the device count at 1 and the other blocks untouched.
      // However - and this is important - if there are **5** devices connected and the provided buffer is only
      // capable of holding the results of 4, then bertec_ReadBufferedData will return an BERTEC_INVALID_PARAMETER error (-201)
      
      // Always check the return codes value from bertec_ReadBufferedData and react accordingly.
      
      // Note that use the Callback method does not require this kind of logic; the data pointer your callback is passed
      // is always allocated properly and you are expect to capture or ignore the data as needed.

      const bool USE_SDK_ALLOCATOR = true;  // set this to FALSE to use the example buffer allocator, below.
      size_t datasize = 0;
      bertec_DataFrame* pData = nullptr;

      if (USE_SDK_ALLOCATOR)
         pData = bertec_AllocateReadBufferedData( hand, &datasize );
      else
      {
         // The example allocator rounds the needed buffer to a 8-byte boundary, which is needed on some processor types to avoid a segfault.
         // (ex: Arm7's). It does this by adding padding and then allocating 64-bit in values, which C++ will always align on the proper
         // 4 or 8 byte boundary, depending on the processor.
         // This is the same code that the bertec_AllocateReadBufferedData function uses, just that instead of using a fixed value of 4
         // for the deviceCount, the SDK function instead uses the value from bertec_GetDeviceCount, bounding the lower limit to 1

         const int deviceCount = 4; // instead of the fixed value of 4, try bertec_GetDeviceCount(hand) instead
         datasize = sizeof( bertec_DataFrame ) + (sizeof( bertec_DeviceData ) * (size_t)deviceCount);
         constexpr size_t aligner = sizeof( int64_t );   // will be 8
         const size_t alignersNeeded = ((datasize + aligner - 1) / aligner);  // round up
         pData = (bertec_DataFrame*)(new int64_t[alignersNeeded]); // this will always align on an 8-byte boundary (the size of int64's)
      }


      // Loop through reading whatever data from the buffer until the timer runs out. You code could do something similar, but instead of
      // waiting 100ms between calls you could do "real work".
      // During this loop you should be checking the return codes from bertec_GetBufferedDataAvailable and bertec_ReadBufferedData, along
      // with periodically calling bertec_GetStatus to see if things have changed (such as devices faulting, being disconnected, etc).

      // The standard clock() returns the wall clock in milliseconds.
      const clock_t targetTime = clock() + (CLOCKS_PER_SEC * runTimeSeconds);

      while (clock() <= targetTime)
      {
         // Read all the data out of the buffer
         while ((bertec_GetBufferedDataAvailable( hand ) > 0) && (clock() <= targetTime))
         {
            // bertec_ReadBufferedData will read a single frame of data, returning either an error (negative value), a 0 (there was no data to be read),
            // or 1 (read one frame, but there may be more). In this example code, since bertec_GetBufferedDataAvailable is being called prior
            // to bertec_ReadBufferedData, the bertec_ReadBufferedData call should never return zero (this is being checked anyways for example reasons)
            const int rc = bertec_ReadBufferedDataStream( hand, pData, datasize );
            if (rc < 0)
            {
               printf( "bertec_ReadBufferedData returned error code %d\n", rc );
            }
            else if (rc > 0)
            {
               DataCallback( hand, pData, (void*)pFile );  // call the same code as the callback would do. Write out a single frame of data
            }
            else // rc==0; You should never get here since bertec_GetBufferedDataAvailable is being called.
            {
               printf( "bertec_ReadBufferedData returned no data frames read, even though bertec_GetBufferedDataAvailable said there was.\n" );
            }
         }

         // Buffered data is empty, so pretend to 'do something' here
         WaitForXmilliseconds( 25 );  // if your main process is in a tight loop like this one, you can overrun the device by continually polling when there is no data there
      }

      // Release the memory that was allocated.
      if (USE_SDK_ALLOCATOR)
         bertec_FreeAllocatedReadBufferedData( hand, pData );
      else
         delete pData;

      pData = nullptr;
   }
   else
   if (perfTestMode)
   {
      printf( "Perftest block for %d seconds.\n", runTimeSeconds );
      WaitForXmilliseconds( 1000 * runTimeSeconds );
   }

   bertec_Stop( hand );

   printf( "Data gather complete, shutting down.\n" );

   bertec_Close( hand );

   if (pFile != nullptr)
      fclose( pFile );

	for (size_t i = 0; i < immedFiles.size(); ++i)
		fclose( immedFiles[i].pFile );
	immedFiles.clear();

   return 0;
}

