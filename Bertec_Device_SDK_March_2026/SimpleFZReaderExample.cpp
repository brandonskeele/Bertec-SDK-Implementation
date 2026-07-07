// This example shows a general way of starting the SDK and then waiting for the Status events to indicate devices have been connected (SimpleStatusHandler).
// Once devices are connected (BERTEC_DEVICES_READY), the SimpleStatusHandler function reads the channel names (bertec_GetDeviceChannelName) to determine
// the index of the "FZ" channel name. This index value is used by the SimpleDataHandler function to display the running FZ value along with the device timestamp.
//
// Both the SimpleStatusHandler and SimpleDataHandler functions are called from a secondary thread; the main WaitForStuff loop simply waits for the user to press a key
// and then exits.
//
// The SimpleStatusHandler code itself is important, since it shows how processing the BERTEC_DEVICES_READY signal is used to trigger the StartDataStream process.
//
// While this example code is written to be used with only a single device (device index == 0), the code does handle multiple devices; it just ignores the others.
//
// Also note while this code is not written as a C++ class, it can easily be made that way by making the data variables and functions class members.



#include <iostream>
#include <time.h>
#include <windows.h>
#include <string.h>
#include "bertecif.h"

// Depending on the target platform, stricmp/strnicmp is called different things and _getch needs defined
#ifdef _WIN32
#include <tchar.h>
#include <tchar.h>
#include <conio.h>
#define stricmp _stricmp
#define strnicmp _strnicmp
#else
#define stricmp strcasecmp
#define strnicmp strncasecmp
#include <termios.h>

static struct termios oldtermios, newtermios;

/* Initialize new terminal i/o settings */
void initTermios( int echo )
{
   tcgetattr( 0, &oldtermios ); /* grab old terminal i/o settings */
   newtermios = oldtermios; /* make new settings same as old settings */
   newtermios.c_lflag &= ~ICANON; /* disable buffered i/o */
   if (echo)
   {
      newtermios.c_lflag |= ECHO; /* set echo mode */
   }
   else
   {
      newtermios.c_lflag &= ~ECHO; /* set no echo mode */
   }
   tcsetattr( 0, TCSANOW, &newtermios ); /* use these new terminal i/o settings now */
}

/* Restore old terminal i/o settings */
void resetTermios( void )
{
   tcsetattr( 0, TCSANOW, &oldtermios );
}

/* Read 1 character without echo */
char _getch( void )
{
   char ch;
   initTermios( 0 );
   ch = getchar();
   resetTermios();
   return ch;
}

#endif


//////////////////////////////////////////////////////////////////////////

// Data for this example. If you are designing a C++ class, this is what you would keep.
static bertec_Handle theHandle = nullptr;  // the handle returned by bertec_Init
static int fzChannelIndex = -1;            // set by SimpleStatusHandler and used by SimpleDataHandler
static bool devicesAreReady = false;      // set after BERTEC_DEVICES_READY appears in the status and cleared whenever there is a problem; used by SimpleDataHandler

// The methods must be defined before the callback registration can use them.
void CALLBACK SimpleStatusHandler( bertec_Handle bHand, int status, void * userData );
void CALLBACK SimpleDataHandler(bertec_Handle bHand, const bertec_DataFrame * dataFrame, void * userData);
int SimpleInitLibrary();
void SimpleCloseLibrary();


// This is an example "work" loop. The SimpleInitLibrary call starts the SDK, connects the callbacks, and starts things running in the background.
// The middle part loops, waiting for a keypress (the "real work").
// The final part tears everything down.
// In your code, the SimpleInitLibrary might be part of the initial application setup, the keypress loop your main UI or whatever,
// and the SimpleCloseLibrary is where you start shutting everything down (ex: your main application window or framework being destroyed, a finalizer, etc).
void SimpleFZReaderExample()
{
   printf( "Bertec example code #2. Press ESC or Space to exit.\n" );

   if (SimpleInitLibrary() != 0)
      return;

   // Simply loop here waiting for the user to press escape or space

   int c = 0;
   while ((c = _getch()) != 3)
   {
      if (c == 27 || c == 32)
         break;
      Sleep( 15 );   // yield and appear to do something
   }

   SimpleCloseLibrary();
}

// Init the SDK library and set up the callback handlers.
int SimpleInitLibrary()
{
   devicesAreReady = false;
   fzChannelIndex = -1;
   theHandle = bertec_Init();

   if (theHandle == nullptr)
   {
      printf( "Unable to initialize the Bertec Device Library (possible missing FTD2XX install).\n" );
      return -1;
   }

   // This connects both the status change handler and the data handler to your functions.
   // Since there is no user data being used in this example, nullptr is passed.
   bertec_RegisterStatusCallback( theHandle, SimpleStatusHandler, nullptr );
   bertec_RegisterDataStreamCallback( theHandle, SimpleDataHandler, nullptr );

   // Start the device connection process in the background; SimpleStatusHandler and SimpleDataHandler will be called in separate threads as needed.
   bertec_Start( theHandle );

   return 0;
}

// Close the library and remove the callback handlers.
void SimpleCloseLibrary()
{
   devicesAreReady = false;
   if (theHandle != nullptr)
   {
      bertec_UnregisterStatusCallback( theHandle, SimpleStatusHandler, nullptr );
      bertec_UnregisterDataStreamCallback( theHandle, SimpleDataHandler, nullptr );
      bertec_Stop( theHandle );
      bertec_Close( theHandle );
   }
   theHandle = nullptr;
}

// For long-term connections with multiple devices, it is suggested that periodically you tell the SDK to drop the connections and restart
// to help with the multiple device synchronization and clock drift.
// The timing if this will be application dependent - for example, this could be after every data collection "session" or on demand.
// While the redetection is being preformed, you will not get any data via the SimpleDataHandler until the redetection is complete; any existing
// data handler callbacks will remain and are not removed.
void SimpleRedetectDevices()
{
   bertec_RedetectConnectedDevices( theHandle );   // returns immediately
}


// The SDK supports the ability to upsample or downsample the data coming in from the devices. Typically, you will not want to do this and instead
// provide some form of data manipulation in your own code.
// Doing so will disable the SYNC and AUX pin modes, returning the device to SAMPLED mode; the SYNC and AUX data values will always be zero.
// This feature works with any devices, even if they do not have the physical hardware for it.
// Setting resampleRate to 0 will turn off resampling, and return the SDK to the hardware data rate of 1000hz.
// Note that not all frequencies are available; the resulting value is equal to floor(8000/round(8000/input)).
// More importantly, please note that data rate resampling can introduce delays in the data.
void SimpleChangeResamplingRate( int resampleRate )
{
   const int numDevices = bertec_GetDeviceCount( theHandle );
   for (int devIndex = 0; devIndex < numDevices; ++devIndex)
      bertec_SetDataRateResampling( theHandle, devIndex, resampleRate );
}



void SimpleFindFzIndex()
{
   // A more robust process would look at each device's channel names and use separate FZ index values.
   int deviceCount = bertec_GetDeviceCount( theHandle );

   int channelCountForDevice0 = bertec_GetDeviceChannelCount( theHandle, 0 );

   fzChannelIndex = -1; // assume it cannot be found
   char nameBuffer[16];
   for (int channelIndex = 0; channelIndex < channelCountForDevice0; ++channelIndex)
   {
      bertec_GetDeviceChannelName( theHandle, 0, channelIndex, nameBuffer, sizeof( nameBuffer ) );
      // If the channel name at this index is the FZ channel we're looking for, record off that index and exit.
      if (stricmp( "FZ", nameBuffer ) == 0)
      {
         fzChannelIndex = channelIndex;
         break;
      }
   }
}

// The status handler will be called each time the _status value changes. The _status value will be one of the bertec_StatusErrors enums.
// Not all enum values will be triggered through this; the ones that you can expect to see are implemented here. The rest are error values
// returned from SDK api calls.
void CALLBACK SimpleStatusHandler( bertec_Handle _bHand, int _status, void * _userData )
{
   // Typecast from an int to the enum so you can debug and see what it is.

   bertec_StatusErrors status = (bertec_StatusErrors)_status;

   switch (status)
   {
      // This status value is emitted when the SDK starts probing the USB ports for connected devices.
      // At this point, there are no devices connected so the data values are reset. Your code should re-init
      // whatever processing function you're using, preparing it for use once devices are detected (BERTEC_DEVICES_READY)
      case BERTEC_LOOKING_FOR_DEVICES:
         printf( "\nSearching for connected devices\n" );
         devicesAreReady = false;
         fzChannelIndex = -1;
         break;

      // This status value is emitted when the SDK cannot find any connected USB devices.
      // While the data values were reset as before, we do it again here just to be sure.
      case BERTEC_NO_DEVICES_FOUND:
         printf( "\nNo devices found\n" );
         devicesAreReady = false;
         fzChannelIndex = -1;
         break;

      // This status value indicates that all connected devices have been successfully started and data is now being read in.
      // At this point, your SimpleDataHandler is already being called, but because the devicesAreaReady flag is set to FALSE,
      // the example SimpleDataHandler will not process the data yet. This gives the SimpleStatusHandler handler a chance to get the index
      // to the FZ channel for monitoring.
      case BERTEC_DEVICES_READY:
      {
         printf( "\nDevices found and ready\n" );

         // Starting with the 2.50 version of the SDK, the Library will no longer start delivering data the instant it detects devices;
         // this changes was done to improve how the system interacts with various end-user projects and to make it clear when data starts and stops.
         // The control block also allows various modes of operation - see the documentation on what all these are and how to use them.
         // For this example, the data stream is started in 'classical' non-synchronized mode.

         bertec_DataStreamControl streamControl = { 0 };
         streamControl.size = sizeof( streamControl );   // if you don't set the size properly, the SDK will reject your call.
         streamControl.syncPinMode = bertec_DataStreamControl::SyncPinMode::SYNCPINMODE_NONE;  // no special control is done; the pins will be read (if hardware supports it)
         streamControl.auxPinMode = bertec_DataStreamControl::AuxPinMode::AUXPINMODE_NONE;      // and are passed in as part of the bertec_DataFrame structure.
         streamControl.deviceFilterBitmask = 0; // for non-zero values, any bit that is turned on will allow data through and off will not. This will affect the bertec_DataFrame structure.
         streamControl.internalClockSource = 0; // ignored unless streamControl.syncPinMode == SYNCPINMODE_INTCLOCK
         streamControl.internalClockFrequency = 0;// ignored unless streamControl.syncPinMode == SYNCPINMODE_INTCLOCK

			// Starting with version 2.56, the SDK strictly enforces calling certain functions from inside event callbacks like Data and Status.
			// Thus calling bertec_StartDataStream from inside the Status callback is not allowed and will return an error.
			// Instead, you need to either signal your main thread to perform the call, invoke a new worker thread, or use
			// the bertec_StartDataStreamAsync (as shown here) which spins a thread and uses a status event handler.
			bertec_StartDataStreamAsync( _bHand, &streamControl,
				[]( bertec_Handle bHand, const bertec_DataStreamControl* control, int status, void* userData )
			{
				if (status == BERTEC_STREAM_SUCCESSFUL)
				{
					SimpleFindFzIndex();

					// Most of the time auto zeroing is desired, so turn that on once devices have been found. Note that _bHand and theHandle are equal.
					bertec_SetEnableAutozero( bHand, 1 );

					devicesAreReady = true;   // SimpleDataHandler will now process data
				}
				else if (status == BERTEC_STREAM_FAILURE)
				{
					printf( "\nFailed to start data stream\n" );
				}
			}, nullptr );

         break;
      }

      // These two errors indicate there is some sort of problem receiving data from the device.
      // The typical problem is that either the device was powered down or unplugged.
      // The SDK will restart the device detection routine, and you will get a BERTEC_LOOKING_FOR_DEVICES event shortly.
      // Your code should issue an update to the front end or log since, and handle data processing accordingly.

      // No data has been received in over 3 seconds, cable is probably unplugged
      case BERTEC_NO_DATA_RECEIVED:
         printf( "\nNo data being received\n" );
         devicesAreReady = false;
         fzChannelIndex = -1;
         break;

      // A communication error with a device has occurred - this usually occurs right after BERTEC_NO_DATA_RECEIVED.
      case BERTEC_DEVICE_HAS_FAULTED:
         printf( "\nDevice has faulted\n" );
         devicesAreReady = false;
         fzChannelIndex = -1;
         break;


      // These are advisory status values, and can be used to inform your user interface display.
      case BERTEC_AUTOZEROSTATE_WORKING:
         printf( "\nDetermining autozero\n" );
         break;
      case BERTEC_AUTOZEROSTATE_ZEROFOUND:
         printf( "\nAutozero found\n" );
         break;

      // For all the others, just show the status value.
      default:
         printf( "\nStatus: %d\n", _status );
         break;
   }
}


// The SimpleDataHandler simply outputs the device's timestamp value along with the FZ value using the fzChannelIndex.
// Each time this is called, the _dataFrame pointer will contain a single "row" of data for all the devices.
// The _dataFrame->deviceCount value will be the same value as what calling bertec_GetDeviceCount would return.
// For this example, only the first device is looked at; the code here loops through all the device data being
// passed, but will only output for device index zero.
void CALLBACK SimpleDataHandler( bertec_Handle _bHand, const bertec_DataFrame * _dataFrame, void * _userData )
{
   if (devicesAreReady)
   {
      for (int deviceNumber = 0; deviceNumber < _dataFrame->deviceCount; ++deviceNumber)
      {
         // this is artificially contrived code; it's only done to show how to process other devices.
         if (deviceNumber == 0)
         {
            // make a reference alias to the device data block to keep things simple
            const bertec_DeviceData& deviceData = _dataFrame->device[deviceNumber];

            // If the Unified Data Mode is turned OFF, you will get empty blocks of data for the device. Here we check for that.
            if (deviceData.channelData.count > 0)
            {
               printf( "\r%lld ", deviceData.additionalData.timestamp );
               if (fzChannelIndex >= 0 && fzChannelIndex < deviceData.channelData.count)
               {
                  printf( " %f                  ", deviceData.channelData.data[fzChannelIndex] );
               }
               fflush( stdout ); // by design, printf will only put out characters when newlines (\n) are printed.
            }
         }
      }
   }
}