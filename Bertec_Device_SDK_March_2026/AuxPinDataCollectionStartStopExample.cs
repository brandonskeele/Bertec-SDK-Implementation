/// <summary>
/// This example shows how to use the Aux pin notification functionality to implement a simple data collection start and stop.
/// Conceptually, this is similar to how Flow Control is handled via SetPinFlowControl, but allows for more fine-grained control
/// based on needs and data structure.
///
/// Once devices are connected (DEVICES_READY), SetPinStateChangeNotification is called to tie the first device's aux pin
/// to a handler. This handler then waits for the pin state transition to a high state (the nowHigh parameter is true), calling the StartRecording function.
/// Inversely, when the pin state transitions to a low state (nowHigh is false), the callback method will call StopRecording.
/// The Start and Stop recording functions do nothing other that log an output message and set/clear the recordingEnabled flag.
/// While StatusHandler is the method that sets up the Pin callback handler, it does not need to do so - your code could do this notification setup
/// at any point once devices have been detected (DEVICES_READY).
///
/// A 'pulsed' start/stop is also provided, which allows the aux pin to emulate a push button start/stop (similar to a vcr remote control).
/// This handler just reacts to the nowHigh value and toggles a Running flag.
///
/// Note that if the Aux pin is already high when the SetPinStateChangeNotification is called, your callback will not get invoked until the pin *transitions*
/// from a low to high state.
/// </summary>
using System;

namespace BertecExampleNET
{
	class AuxPinDataCollectionStartStopExample
	{
		BertecDeviceNET.BertecDevice theHandle = null;  // the library object
		int fzChannelIndex = -1;                        // set by StatusHandler and used by DataHandler
		bool devicesAreaReady = false;                  // set after BERTEC_DEVICES_READY appears in the status and cleared whenever there is a problem; used by DataHandler
		bool demoImmediateDeviceDataHandler = false;    // for testing/demoing the single device data stream vs the combined data device stream.
		bool recordingEnabled = false;                  // set in StartRecording, cleared in Stop
		readonly int PINDEVICEINDEX = 0;                // this is used to select which device to select.
		readonly BertecDeviceNET.IOPins PINTOTRIGGER = BertecDeviceNET.IOPins.AUX;          // this is used to select which pin to select.

		static void Main(string[] args)
		{
			AuxPinDataCollectionStartStopExample example = new AuxPinDataCollectionStartStopExample();
			example.Run();
		}

		// This is an example "work" loop. The InitLibrary call starts the SDK, connects the event handlers, and starts things running in the background.
		// The middle part loops, waiting for a keypress (the "real work").
		// The final part tears everything down.
		// In your code, the InitLibrary might be part of the initial application setup, the keypress loop your main UI or whatever,
		// and the CloseLibrary is where you start shutting everything down (ex: your main application window or framework being destroyed, a finalizer, etc).
		public void Run()
		{
			Console.WriteLine("Bertec example code #6. Press ESC or Space to exit.\n");

			if (InitLibrary() != 0)
				return;

			// Simply loop here waiting for the user to press escape or space

			int c = 0;
			while ((c = Console.ReadKey().KeyChar) != 3)
			{
				if (c == 27 || c == 32)
					break;
				System.Threading.Thread.Sleep(15);   // yield and appear to do something
			}

			CloseLibrary();
		}


		int InitLibrary()
		{
			devicesAreaReady = false;
			fzChannelIndex = -1;
			try
			{
				theHandle = new BertecDeviceNET.BertecDevice();
			}
			catch (System.Exception ex)
			{
				Console.WriteLine("Unable to initialize the Bertec Device Library (possible missing FTD2XX install).");
				return -1;
			}

			// This connects both the status change handler and the data handler to your functions.

			theHandle.OnStatus += StatusHandler;

			// While this example shows how to connect one event sink vs the other, you can absolutly connect both at the same time;
			// if you comment out the if-else you can see it happen on the console with the [I] flashing on and off and the text getting 'stomped on'.
			// This is caused by each event sink handler getting called at the same time by different thread processes, so they run concurrently.
			// Ex: you can get 2+ invokes at the same time on your OnImmediateDeviceData handler, all while your OnDataStream handler
			// is also concurrently processing data. If your event handlers must access shared resources (files, memory), you will need
			// to use mutex locking to marshal access - the example presented here doesn't, so that is why the Console output looks like it does.

			if (demoImmediateDeviceDataHandler)
				theHandle.OnImmediateDeviceData += ImmediateDeviceDataHandler;
			else
				theHandle.OnDataStream += DataHandler;

			// Start the device connection process in the background; StatusHandler and DataHandler will be called in separate threads as needed.
			theHandle.Start();

			return 0;
		}

		// Close the library and remove the event handlers. While .Dispose() is not 100% needed, it is recommended.
		void CloseLibrary()
		{
			devicesAreaReady = false;
			if (theHandle != null)
			{
				theHandle.OnStatus -= StatusHandler;
				theHandle.OnDataStream -= DataHandler;
				theHandle.OnImmediateDeviceData -= ImmediateDeviceDataHandler;
				theHandle.ClearPinStateChangeNotification(PINTOTRIGGER);
				theHandle.Stop();
				theHandle.Dispose();
			}
			theHandle = null;
		}


		// This is called when the StatusHandler receives a DEVICES_READY status. Change the callback to AuxPinHandler_Pulse for a toggle.
		void AuxStartPinHandler()
		{
			theHandle.SetPinStateChangeNotification(PINTOTRIGGER, AuxPinHandler_Hilo);
		}


		void FindFzIndex()
		{
			// A more robust process would look at each device's channel names and use separate FZ index values.
			int deviceCount = theHandle.DeviceCount;

			string[] channelNamesForDevice0 = theHandle.DeviceChannelNames(0);
			int channelCountForDevice0 = channelNamesForDevice0.Length;

			fzChannelIndex = -1; // assume it cannot be found
			for (int channelIndex = 0; channelIndex < channelCountForDevice0; ++channelIndex)
			{
				// If the channel name at this index is the FZ channel we're looking for, record off that index and exit.
				if (String.Equals(channelNamesForDevice0[channelIndex], "FZ", StringComparison.OrdinalIgnoreCase))
				{
					fzChannelIndex = channelIndex;
					break;
				}
			}
		}

		// The status handler will be called each time the _status value changes. The _status value will be one of the bertec_StatusErrors enums.
		// Not all enum values will be triggered through this; the ones that you can expect to see are implemented here. The rest are error values
		// returned from SDK api calls.
		void StatusHandler(BertecDeviceNET.StatusErrors status)
		{
			switch (status)
			{
				// This status value is emitted when the SDK starts probing the USB ports for connected devices.
				// At this point, there are no devices connected so the data values are reset. Your code should re-init
				// whatever processing function you're using, preparing it for use once devices are detected (BERTEC_DEVICES_READY)
				case BertecDeviceNET.StatusErrors.LOOKING_FOR_DEVICES:
					Console.WriteLine("\nSearching for connected devices");
					devicesAreaReady = false;
					fzChannelIndex = -1;
					break;

				// This status value is emitted when the SDK cannot find any connected USB devices.
				// While the data values were reset as before, we do it again here just to be sure.
				case BertecDeviceNET.StatusErrors.NO_DEVICES_FOUND:
					Console.WriteLine("\nNo devices found");
					devicesAreaReady = false;
					fzChannelIndex = -1;
					break;

				// This status value indicates that all connected devices have been successfully started and data is now being read in.
				// At this point, your DataHandler is already being called, but because the devicesAreaReady flag is set to FALSE,
				// the example DataHandler will not process the data yet. This gives the StatusHandler handler a chance to get the index
				// to the FZ channel for monitoring.
				case BertecDeviceNET.StatusErrors.DEVICES_READY:
					{
						Console.WriteLine("\nDevices found and ready");

						for (int devNum = 0; devNum < theHandle.DeviceCount; ++devNum)
						{
							Console.WriteLine("Plate serial {0}, {1}", theHandle.DeviceSerialNumber(devNum), theHandle.DeviceIDString(devNum));
						}

						// Starting with the 2.50 version of the SDK, the Library will no longer start delivering data the instant it detects devices;
						// this changes was done to improve how the system interacts with various end-user projects and to make it clear when data starts and stops.
						// The control block also allows various modes of operation - see the documentation on what all these are and how to use them.
						// For this example, the data stream is started in 'classical' non-synchronized mode.

						// Starting with version 2.56, the SDK strictly enforces calling certain functions from inside event callbacks like Data and Status.
						// Thus calling StartDataStream from inside the Status callback is not allowed and will return an error.
						// Instead, you need to either signal your main thread to perform the call, invoke a new worker thread (as shown here), or use
						// the StartDataStreamAsync which spins a thread and uses a status event handler.

						System.Threading.Tasks.Task.Run(() =>
						{
							BertecDeviceNET.DataStreamControl streamControl = new BertecDeviceNET.DataStreamControl();
							streamControl.syncPinMode = BertecDeviceNET.DataStreamControl.SyncPinMode.NONE;
							streamControl.auxPinMode = BertecDeviceNET.DataStreamControl.AuxPinMode.NONE;
							streamControl.deviceFilterBitmask = 0; // for non-zero values, any bit that is turned on will allow data through and off will not. This will affect the bertec_DataFrame structure.
							streamControl.internalClockSource = 0; // ignored unless streamControl.syncPinMode == SyncPinMode.INTCLOCK
							streamControl.internalClockFrequency = 0;// ignored unless streamControl.syncPinMode == SyncPinMode.INTCLOCK

							theHandle.StartDataStream(streamControl); // this will block until the device data stream is working; DataHandler will start being called when this exits successfully
							// Normally you will want to check the return value from StartDataStream here, but for this example we assume it works.

							FindFzIndex();
							AuxStartPinHandler();   // set the pin notification callback

							// Most of the time auto zeroing is desired, so turn that on once devices have been found.
							theHandle.AutoZeroing = true;

							devicesAreaReady = true;   // DataHandler will now process data
						});
						break;
					}


				// These two errors indicate there is some sort of problem receiving data from the device.
				// The typical problem is that either the device was powered down or unplugged.
				// The SDK will restart the device detection routine, and you will get a BERTEC_LOOKING_FOR_DEVICES event shortly.
				// Your code should issue an update to the front end or log since, and handle data processing accordingly.

				// No data has been received in over 3 seconds, cable is probably unplugged
				case BertecDeviceNET.StatusErrors.NO_DATA_RECEIVED:
					Console.WriteLine("\nNo data being received");
					devicesAreaReady = false;
					fzChannelIndex = -1;
					break;

				// A communication error with a device has occurred - this usually occurs right after BERTEC_NO_DATA_RECEIVED.
				case BertecDeviceNET.StatusErrors.DEVICE_HAS_FAULTED:
					Console.WriteLine("\nDevice has faulted");
					devicesAreaReady = false;
					fzChannelIndex = -1;
					break;


				// These are advisory status values, and can be used to inform your user interface display.
				case BertecDeviceNET.StatusErrors.AUTOZEROSTATE_WORKING:
					Console.WriteLine("\nDetermining autozero");
					break;
				case BertecDeviceNET.StatusErrors.AUTOZEROSTATE_ZEROFOUND:
					Console.WriteLine("\nAutozero found");
					break;

				// For all the others, just show the status value.
				default:
					Console.WriteLine("\nStatus: {0}", status);
					break;
			}
		}

		// The DataHandler simply outputs the device's timestamp value along with the FZ value using the fzChannelIndex.
		// Each time this is called, the dataFrames collection will contain a single "row" of data for all the devices.
		// The dataFrames.Length value will be the same value as what calling BertecDeviceNET.DeviceCount would return.
		// For this example, only the first device is looked at; the code here loops through all the device data being
		// passed, but will only output for device index zero.
		void DataHandler(BertecDeviceNET.DataFrame[] dataFrames)
		{
			if (devicesAreaReady && recordingEnabled) // simply check if recording or not; your code could do more complex logic here
			{
				for (int deviceNumber = 0; deviceNumber < dataFrames.Length; ++deviceNumber)
				{
					// this is artificially contrived code; it's only done to show how to process other devices.
					if (deviceNumber == 0)
					{
						// make a reference alias to the device data block to keep things simple
						BertecDeviceNET.DataFrame deviceData = dataFrames[deviceNumber];

						// If the Unified Data Mode is turned OFF, you will get empty blocks of data for the device. Here we check for that.
						if (deviceData.forceData.Length > 0)
						{
							Console.Write("\r   {0} ", deviceData.timestamp);
							if (fzChannelIndex >= 0 && fzChannelIndex < deviceData.forceData.Length)
							{
								Console.Write(" {0}                  ", deviceData.forceData[fzChannelIndex]);
							}
							Console.Out.Flush(); ; // by design, .Write will only put out characters when newlines (\n) are printed.
						}
					}
				}
			}
		}

		// This event handler gets the force data from the plate after the calibration matrix and zero offsets have been applied, but
		// but before any additional calculations or processing (ex: filtering, resampling, computed channels). Each device will invoke
		// this handler when they get their data; you should process or buffer this as quick as possible and return. Device to device
		// ordering is not guaranteed, but data frame ordering within the same device is.
		// This handler is only turned on in this sample code file by setting the demoImmediateDeviceDataHandler, at the top of the file.
		void ImmediateDeviceDataHandler(int deviceIndex, string deviceUid, BertecDeviceNET.DataFrame deviceData)
		{
			if (devicesAreaReady && recordingEnabled) // simply check if recording or not; your code could do more complex logic here
			{
				Console.Write("\r[I]{0}, {1}, {2}", deviceIndex, deviceUid, deviceData.timestamp);
				if (fzChannelIndex >= 0 && fzChannelIndex < deviceData.forceData.Length)
				{
					Console.Write(": {0}                  ", deviceData.forceData[fzChannelIndex]);
				}
				Console.Out.Flush(); ; // by design, .Write will only put out characters when newlines (\n) are printed.
			}
		}

		// The handler will be called when the pin changes state from 0 to 1 or 1 to 0; nowHigh will be true for 0 to 1 and false for 1 to 0.
		// Normally you want to check both the pin enum value and the deviceIndex value to make sure that they are what you are expecting.
		void AuxPinHandler_Hilo(BertecDeviceNET.IOPins pin, int deviceIndex, bool nowhigh, BertecDeviceNET.DataFrame deviceData)
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
		void AuxPinHandler_Pulse(BertecDeviceNET.IOPins pin, int deviceIndex, bool nowhigh, BertecDeviceNET.DataFrame deviceData)
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
				Console.Write("Now recording data\n");
			}
		}

		// Stop if recording; does nothing if already stopped. You code can do something like closing a file or sending a message.
		void StopRecording()
		{
			if (recordingEnabled)
			{
				recordingEnabled = false;
				Console.Write("Done with recording data\n");
			}
		}
	}
}
