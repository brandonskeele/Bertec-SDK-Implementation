// This example shows how to use the Aux pin notification functionality to implement a simple data collection start and stop.
// Conceptually, this is similar to how Flow Control is handled via bertec_SetPinFlowControl, but allows for more fine-grained control
// based on needs and data structure.
//
// Once devices are connected (BERTEC_DEVICES_READY), bertec_RegisterPinStateChangeCallback is called to tie the first device's aux pin
// to a callback. This callback then waits for the pin state transition to a high state (the nowHigh parameter is true), calling the StartRecording function.
// Inversely, when the pin state transitions to a low state (nowHigh is false), the callback method will call StopRecording.
// The Start and Stop recording functions do nothing other that log an output message and set/clear the recordingEnabled flag.
// While AuxStatusHandler is the method that sets up the Pin callback handler, it does not need to do so - your code could do this callback setup
// at any point once devices have been detected (BERTEC_DEVICES_READY).
//
// A 'pulsed' start/stop is also provided, which allows the aux pin to emulate a push button start/stop (similar to a vcr remote control).
// This callback method just reacts to the nowHigh value and toggles a Running flag.
//
// Note that if the Aux pin is already high when the bertec_RegisterPinStateChangeCallback is called, your callback will not get invoked until the pin *transitions*
// from a low to high state.
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
static bertec_Handle theHandle = nullptr; // the handle returned by bertec_Init
static int fzChannelIndex = -1;           // set by AuxStatusHandler and used by AuxDataHandler
static bool devicesAreReady = false;      // set after BERTEC_DEVICES_READY appears in the status and cleared whenever there is a problem; used by AuxDataHandler
static bool recordingEnabled = false;     // set in StartRecording, cleared in Stop
static int PINDEVICEINDEX = 0;				// this is used to select which device to select.
static bertec_IOPins PINTOTRIGGER = bertec_IOPins::IO_PIN_AUX;				// this is used to select which pin to select.

// The methods must be defined before the callback registration can use them.
void CALLBACK AuxStatusHandler( bertec_Handle bHand, int status, void * userData );
void CALLBACK AuxDataHandler( bertec_Handle bHand, const bertec_DataFrame * dataFrame, void * userData );
void CALLBACK AuxPinCallbackHandler_Hilo( bertec_Handle bHand, bertec_IOPins pin, int deviceIndex, bool nowhigh, const bertec_DeviceData* data, void * userData );
void CALLBACK AuxPinCallbackHandler_Pulse( bertec_Handle bHand, bertec_IOPins pin, int deviceIndex, bool nowhigh, const bertec_DeviceData* data, void * userData );
int AuxInitLibrary();
void AuxCloseLibrary();
void StartRecording();
void StopRecording();


// This is an example "work" loop. The AuxInitLibrary call starts the SDK, connects the callbacks, and starts things running in the background.
// The middle part loops, waiting for a keypress (the "real work").
// The final part tears everything down.
// In your code, the AuxInitLibrary might be part of the initial application setup, the keypress loop your main UI or whatever,
// and the AuxCloseLibrary is where you start shutting everything down (ex: your main application window or framework being destroyed, a finalizer, etc).
void AuxPinDataCollectionStartStopExample()
{
   printf( "Bertec example code #6. Press ESC or Space to exit.\n" );

   if (AuxInitLibrary() != 0)
      return;

   // Simply loop here waiting for the user to press escape or space

   int c = 0;
   while ((c = _getch()) != 3)
   {
      if (c == 27 || c == 32)
         break;
      Sleep( 15 );   // yield and appear to do something
   }

   AuxCloseLibrary();
}

// Init the SDK library and set up the callback handlers.
int AuxInitLibrary()
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
   bertec_RegisterStatusCallback( theHandle, AuxStatusHandler, nullptr );
   bertec_RegisterDataStreamCallback( theHandle, AuxDataHandler, nullptr );

   // Start the device connection process in the background; AuxStatusHandler and AuxDataHandler will be called in separate threads as needed.
   bertec_Start( theHandle );

   return 0;
}

// Close the library and remove the callback handlers.
void AuxCloseLibrary()
{
   devicesAreReady = false;
   if (theHandle != nullptr)
   {
      bertec_UnregisterStatusCallback( theHandle, AuxStatusHandler, nullptr );
      bertec_UnregisterDataStreamCallback( theHandle, AuxDataHandler, nullptr );
      bertec_UnregisterPinStateChangeCallback( theHandle, PINTOTRIGGER, AuxPinCallbackHandler_Hilo, nullptr );
      bertec_Stop( theHandle );
      bertec_Close( theHandle );
   }
   theHandle = nullptr;
}


// This is called when the AuxStatusHandler receives a BERTEC_DEVICES_READY status. Change the callback to AuxPinCallbackHandler_Pulse for a toggle.
void AuxStartPinCallback()
{
   bertec_RegisterPinStateChangeCallback( theHandle, PINTOTRIGGER, AuxPinCallbackHandler_Hilo, nullptr );
}

void AuxFindFzIndex()
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
void CALLBACK AuxStatusHandler( bertec_Handle _bHand, int _status, void * _userData )
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
         // At this point, your AuxDataHandler is already being called, but because the devicesAreaReady flag is set to FALSE,
         // the example AuxDataHandler will not process the data yet. This gives the AuxStatusHandler handler a chance to get the index
         // to the FZ channel for monitoring.
      case BERTEC_DEVICES_READY:
      {
         printf( "\nDevices found and ready\n" );

         // Starting with the 2.50 version of the SDK, the Library will no longer start delivering data the instant it detects devices;
         // this changes was done to improve how the system interacts with various end-user projects and to make it clear when data starts and stops.
         // The control block also allows various modes of operation - see the documentation on what all these are and how to use them.
         // For this example, the data stream is started in 'classical' non-synchronized mode.

         bertec_DataStreamControl streamControl{};
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
					AuxFindFzIndex();
					AuxStartPinCallback();   // set the pin notification callback

					// Most of the time auto zeroing is desired, so turn that on once devices have been found. Note that _bHand and theHandle are equal.
					bertec_SetEnableAutozero( bHand, 1 );

					devicesAreReady = true;   // AuxDataHandler will now process data
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


// The AuxDataHandler simply outputs the device's timestamp value along with the FZ value using the fzChannelIndex.
// Each time this is called, the _dataFrame pointer will contain a single "row" of data for all the devices.
// The _dataFrame->deviceCount value will be the same value as what calling bertec_GetDeviceCount would return.
// For this example, only the first device is looked at; the code here loops through all the device data being
// passed, but will only output for device index zero.
// For this example, the handler also checks the recordingEnabled flag and suppresses output if false.
void CALLBACK AuxDataHandler( bertec_Handle _bHand, const bertec_DataFrame * _dataFrame, void * _userData )
{
   if (devicesAreReady && recordingEnabled) // simply check if recording or not; your code could do more complex logic here
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

// The callback will be called when the pin changes state from 0 to 1 or 1 to 0; nowHigh will be true for 0 to 1 and false for 1 to 0.
// Normally you want to check both the pin enum value and the deviceIndex value to make sure that they are what you are expecting.
void CALLBACK AuxPinCallbackHandler_Hilo( bertec_Handle bHand, bertec_IOPins pin, int deviceIndex, bool nowhigh, const bertec_DeviceData* data, void * userData )
{
	if (pin == PINTOTRIGGER && deviceIndex == PINDEVICEINDEX)
	{
		if (nowhigh)
			StartRecording();
		else
			StopRecording();
	}
}

// Pulse simple toggles the running state whenever it detects a 0 to 1 transition. You code may wish to detect the falling edge (1 to 0), or implement
// a more complex version where it looks for a 0->1->0 pattern
void CALLBACK AuxPinCallbackHandler_Pulse( bertec_Handle bHand, bertec_IOPins pin, int deviceIndex, bool nowhigh, const bertec_DeviceData* data, void * userData )
{
	if (pin == PINTOTRIGGER && deviceIndex == PINDEVICEINDEX)
	{
		if (nowhigh)
		{
			if (recordingEnabled)
				StopRecording();
			else
				StartRecording();
		}
	}
}

// Start recording if not already recording. You code could do something more complex, like signaling another thread or opening an output file.
void StartRecording()
{
   if (!recordingEnabled)
   {
      recordingEnabled = true;
      printf( "Now recording data\n" );
   }
}

// Stop if recording; does nothing if already stopped. You code can do something like closing a file or sending a message.
void StopRecording()
{
   if (recordingEnabled)
   {
      recordingEnabled = false;
      printf( "Done with recording data\n" );
   }
}
