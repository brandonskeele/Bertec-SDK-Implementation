/// <summary>
/// This sample program for the .NET interface for the Bertec DLL shows how to use the data gathering process
/// with the Bertec Device .NET DLL. The command line program allows you to use either callbacks
/// or data polling, logging output to a file or to the screen for a given number of seconds.
/// </summary>
using System;
using System.Diagnostics;
using System.IO;

namespace BertecExampleNET
{
	// the aux pin mode is used based on the -s MODE option. By default, the example code will always collect data, ignoring the AUX signal input
	// (only available on external amp such as the AM6500, AM6800, and AM6817).
	enum AuxPinMode
	{
		NoAuxPinMode = 0,    // the DataCallback will always write data
		OnlyRecordWhenHigh,  // the DataCallback will only write data when the additionalData.auxData value is set to any value other than 0 (signal was set at some point during the sampling)
		OnlyRecordWhenLow    // the DataCallback will only write data when the additionalData.auxData value is set to any value other than 255 (signal was cleared at some point during the sampling)
	};


	/// <summary>
	/// The CallbackDataHandlerClass is where the data from the device is handled. In this example, the
	/// output is just simply written as a trivial CSV file.
	/// In order to make it a little simpler, the member pFile is exposed as public, so that it can be
	/// directly used outside of the class. This means that this class is created even if data polling
	/// (not callbacks) are used.
	/// </summary>
	class CallbackDataHandlerClass
	{
		/// <summary>
		/// The output file. This is also used when doing data polling.
		/// </summary>
		public StreamWriter pFile = null;

		public bool writeHeader = true;
		public bool includeSyncAux = false;
		public AuxPinMode onlyRecordWhenAuxed = AuxPinMode.NoAuxPinMode;  // set via the -s mode option; if non-zero, will only write out when the aux pin is in the correct state
		public int iLimitNChannels = 0;
		public BertecDeviceNET.DataStreamControl.SyncPinMode useDeviceSync = BertecDeviceNET.DataStreamControl.SyncPinMode.NONE;

		public BertecDeviceNET.BertecDevice hand = null;

		/// <summary>
		/// The data callback event is called each time there is data. This is enabled by using the
		/// BertecDeviceNET.DataEventHandler event notification mechanism.
		/// The dataFrames will consist of frames of data, one for each device. Thus if there are two devices, the dataFrames array will have a length of 2.
		/// </summary>
		public void OnDataCallback(BertecDeviceNET.DataFrame[] dataFrames)
		{
			if (dataFrames.Length > 0)
			{
				if (writeHeader)
				{
					bool hasMults = (dataFrames.Length > 1);
					// For multiple devices the general convention is to append the device index as the suffix to each channel name.
					for (int devNum = 0; devNum < dataFrames.Length; ++devNum)
					{
						int suffix = devNum + 1;
						if (hasMults)
						{
							if (devNum > 0)
								pFile.Write(",");
							pFile.Write("Timestamp-{0}", suffix);
						}
						else
							pFile.Write("Timestamp");

						if (useDeviceSync != BertecDeviceNET.DataStreamControl.SyncPinMode.NONE)
						{
							if (hasMults)
							{
								if (devNum > 0)
									pFile.Write(",");
								pFile.Write(",EventCounter-{0},FrameCounter-{0}", suffix);
							}
							else
								pFile.Write(",EventCounter,FrameCounter");
						}

						if (includeSyncAux)
						{
							if (hasMults)
								pFile.Write(",Sync-{0},Aux-{0}", suffix);
							else
								pFile.Write(",Sync,Aux");
						}

						string[] names = hand.DeviceChannelNames(devNum);
						int channelCount = names.Length;

						if (iLimitNChannels > 0 && iLimitNChannels < channelCount)
							channelCount = iLimitNChannels;

						for (int col = 0; col < channelCount; ++col)
						{
							if (hasMults)
								pFile.Write(",{0}-{1}", names[col], suffix);
							else
								pFile.Write(",{0}", names[col]);
						}

					}
					pFile.Write("\r\n");

					writeHeader = false;
				}

				bool needLF = false;
				for (int devNum = 0; devNum < dataFrames.Length; ++devNum)
				{
					BertecDeviceNET.DataFrame devData = dataFrames[devNum];

					// Check if the -s <mode> was passed
					// The better way is to use the DataStreamControl.auxPinMode mode set to RUNHIGH or RUNLOW,
					// but this is provided to show how you can "do it yourself".

					if (onlyRecordWhenAuxed != AuxPinMode.NoAuxPinMode)
					{
						// Both the SYNC and AUX pins come in as an 8-bit pattern, and they can change state at any time during the data sampling.
						// This you can have a value like 126, which is a bit pattern of 0111_1110 - meaning the signal was 'pulsed' in the middle of a data read.
						// For the purposes of this example, the code treats the AUX pin held low for the entire sample (0) or held high (255).
						// If the -s HIGH option is passed, then if the AUX bit pattern is anything other than 0, the data is recorded (meaning at some point
						// the AUX signal was raised high). If -s LOW was passed, then if the AUX pattern is anything other than 255, then the data is recorded
						// (meaning the AUX signal was pulled low at some point).
						// The code presented here is trivial and is to be used as an example only; your project may require more sophisticated pattern handling.

						// Note that the logic here appears backwards from the above text: that's because of the continue statements being used to skip recording.
						if (onlyRecordWhenAuxed == AuxPinMode.OnlyRecordWhenHigh)
						{
							if (devData.auxData == 0)
								continue;   // pin never went high, skip the output
						}
						else if (onlyRecordWhenAuxed == AuxPinMode.OnlyRecordWhenLow)
						{
							if (devData.auxData == 255)
								continue;   // pin never went low, skip the output
						}
					}

					int channelCount = devData.forceData.Length;

					if (iLimitNChannels > 0 && iLimitNChannels < channelCount)
						channelCount = iLimitNChannels;

					if (channelCount > 0)
					{
						needLF = true;

						if (devNum > 0)
							pFile.Write(",");

						pFile.Write("{0:f3}", (double)devData.timestamp);

						if (useDeviceSync != BertecDeviceNET.DataStreamControl.SyncPinMode.NONE)
							pFile.Write( ",{0},{0}", devData.eventCounter, devData.frameCounter);


						if (includeSyncAux)
							pFile.Write(",{0},{1}", devData.syncData, devData.auxData);

						for (int col = 0; col < channelCount; ++col)
							pFile.Write(",{0}", devData.forceData[col]);

					}
				}

				if (needLF)
					pFile.Write("\r\n");
			}
		}

		public void StatusEvent(BertecDeviceNET.StatusErrors status)
		{
			Console.WriteLine("Status event {0}", status);
		}
	}


	class BertecExampleNET
	{
		static void ShowHelp()
		{
			Console.WriteLine("Bertec Device Example (.NET):");
			Console.WriteLine("-f <filename>   output to the given filename");
			Console.WriteLine("-t <seconds>    run for the given # of seconds");
			Console.WriteLine("-l <num>        limit to first num channels per row");
			Console.WriteLine("-y              include the sync/aux values\n");
			Console.WriteLine("-c              do callbacks");
			Console.WriteLine("-p              do polling");
			Console.WriteLine("-a              turn on autozeroing");
			Console.WriteLine("-z              zero load before data gather");
			Console.WriteLine("-m <mode> <n>   turn on multiple device sync mode. Requires amps with the SYNC line connected. Mode is EXT or INT (1000hz unless n is set)");
			Console.WriteLine("-e <mode>       use the external sync clock. Mode is either NONE, RISE, FALL, or BOTH");
			Console.WriteLine("-s <mode>       use aux pin to control data sample. Mode is either NONE, HIGH, or LOW");
			Console.WriteLine("-r <value>      use the given resample rate <value> to downsample or upsample the data (ignores -e and -s)");
			Console.WriteLine("-X <cmd>        executes the given command line process once the data stream is started.");
			Console.WriteLine("-R <value>      retries the data stream <value> times in case it fails. This will also re-init devices if the detected capabilities do not match request");
		}

		static int WaitForDevicesToAppear(BertecDeviceNET.BertecDevice hand)
		{
			Console.WriteLine("Waiting for devices..");

			// This simply waits until the stats returns a state of READY. This block through hand.SetExternalClockMode call
			// could also be made into an event handler tied to BertecDeviceNET.BertecDevice.OnStatus, which is the preferred method since it is less
			// likely to miss a state change.
			while (hand.Status != BertecDeviceNET.StatusErrors.DEVICES_READY)
			{
				// Waiting for devices...
				//Console.Write(".");
				System.Threading.Thread.Sleep(100);
				if (hand.DeviceCount > 0)
					break;   // also can check like this (but we prefer status callbacks since we can get instant results)
			}
			Console.WriteLine(" done");

			// since we're going to be using this a lot, copy it off
			int numberOfDevices = hand.DeviceCount;

			for (int devNum = 0; devNum < numberOfDevices; ++devNum)
			{
				Console.WriteLine("Plate serial {0}, {1}", hand.DeviceSerialNumber(devNum), hand.DeviceIDString(devNum));
			}

			return numberOfDevices;
		}

		static int StartDataStreaming(BertecDeviceNET.BertecDevice hand, BertecDeviceNET.DataStreamControl.SyncPinMode useDeviceSync, int intClockRate)
		{


			// Starting with the 2.50 version of the SDK, the Library will no longer start delivering data the instant it detects devices;
			// this changes was done to improve how the system interacts with various end-user projects and to make it clear when data starts and stops.
			// The control block also allows various modes of operation - see the documentation on what all these are and how to use them.
			// For this example, the data stream is started in 'classical' non-synchronized mode.

			BertecDeviceNET.DataStreamControl streamControl = new BertecDeviceNET.DataStreamControl();
			streamControl.syncPinMode = BertecDeviceNET.DataStreamControl.SyncPinMode.NONE;  // no special control is done; the pins will be read (if hardware supports it)
			streamControl.auxPinMode = BertecDeviceNET.DataStreamControl.AuxPinMode.NONE;      // and are passed in as part of the bertec_DataFrame structure.
			streamControl.deviceFilterBitmask = 0; // for non-zero values, any bit that is turned on will allow data through and off will not. This will affect the bertec_DataFrame structure.
			streamControl.internalClockSource = 0; // ignored unless streamControl.syncPinMode == SyncPinMode.INTCLOCK
			streamControl.internalClockFrequency = 0;// ignored unless streamControl.syncPinMode == SyncPinMode.INTCLOCK

			if (useDeviceSync != BertecDeviceNET.DataStreamControl.SyncPinMode.NONE)
			{
				Console.WriteLine("Using SYNC pin as sync control");
				// Set up the sync mode to have one device generate the clock pulses, and the other devices resample on that. 
				// By setting the clock frequency to 1000hz, the resample rate will match the actual hardware rate.
				// The clock source (the pulse generator) will be the first device; you can change this to any device index you want or need.
				streamControl.syncPinMode = useDeviceSync;
				if (useDeviceSync == BertecDeviceNET.DataStreamControl.SyncPinMode.INTCLOCK)
				{
					streamControl.internalClockFrequency = (intClockRate > 0) ? intClockRate : 1000.0f;
					Console.WriteLine("Using Internal Clock Rate of {0}", (int)streamControl.internalClockFrequency);
				}
			}
			else
			{
				Console.WriteLine("Not using SYNC pin for sync control");
			}



			Console.WriteLine("Starting the data stream...");
			return hand.StartDataStream(streamControl);
		}



		static int Main(string[] args)
		{
			string filename = "";
			bool useCallbacks = false;
			bool usePolling = false;
			int runTimeSeconds = 0;
			AuxPinMode onlyRecordWhenAuxed = AuxPinMode.NoAuxPinMode;  // set via the -s mode option; if non-zero, will only write out when the aux pin is in the correct state
			int limitChannels = 0;
			bool useAutozeroing = false;
			bool startWithZeroLoad = false;
			bool includeSyncAux = false;
			BertecDeviceNET.DataStreamControl.SyncPinMode useDeviceSync = BertecDeviceNET.DataStreamControl.SyncPinMode.NONE;
			int intClockRate = 1000;
			int resampleRate = 0;
			int retryStreamStartTimes = 0;
			string cmdLineExec = "";


			BertecDeviceNET.ClockSourceFlags extClock = BertecDeviceNET.ClockSourceFlags.INTERNAL;

			if (args.Length < 1)
			{
				ShowHelp();
				return -1;
			}

			bool nextIsParm = false;
			string parm = "", command = "";
			for (int i = 0; i < args.Length; ++i)
			{
				string item = args[i];
				if (nextIsParm)
				{
					parm = item;
					nextIsParm = false;
				}
				else if (item.StartsWith("-"))
				{
					command = item.Substring(1);
					if (command == "f" || command == "t" || command == "l" || command == "e" || command == "s" || command == "X" || command == "r" || command == "m")
					{
						nextIsParm = true;
						continue;
					}
				}

				switch (command)
				{
					case "f":
						if (parm.Length > 0)
							filename = parm;
						break;
					case "t":
						if (parm.Length > 0)
							runTimeSeconds = System.Convert.ToInt32(parm);
						break;
					case "l":
						if (parm.Length > 0)
							limitChannels = System.Convert.ToInt32(parm);
						break;
					case "c":
						useCallbacks = true;
						usePolling = false;
						break;
					case "p":
						useCallbacks = false;
						usePolling = true;
						break;
					case "a":
						useAutozeroing = true;
						break;
					case "z":
						startWithZeroLoad = true;
						break;
					case "m":
						if (parm.Length > 0)
						{
							if (parm == "INT")
							{
								useDeviceSync = BertecDeviceNET.DataStreamControl.SyncPinMode.INTCLOCK;
								if (i + 1 < args.Length)
								{
									if (!args[i + 1].StartsWith("-"))
									{
										Int32.TryParse(args[i + 1], out intClockRate);
										++i;
									}
								}

							}
							else if (parm == "EXT")
								useDeviceSync = BertecDeviceNET.DataStreamControl.SyncPinMode.EXTCLOCK;
							++i;
						}
						break;
					case "y":
						includeSyncAux = true;
						break;
					case "r":
						if (parm.Length > 0)
							Int32.TryParse(parm, out resampleRate);
						break;
					case "e":
						if (parm.Length > 0)
						{
							if (parm.ToUpper() == "RISE")
								extClock = BertecDeviceNET.ClockSourceFlags.EXT_RISE;
							else if (parm.ToUpper() == "FALL")
								extClock = BertecDeviceNET.ClockSourceFlags.EXT_FALL;
							else if (parm.ToUpper() == "BOTH")
								extClock = BertecDeviceNET.ClockSourceFlags.EXT_BOTH;
							else
								extClock = BertecDeviceNET.ClockSourceFlags.INTERNAL;
						}
						break;
					case "s":
						if (parm.Length > 0)
						{
							if (parm.ToUpper() == "HIGH")
								onlyRecordWhenAuxed = AuxPinMode.OnlyRecordWhenHigh;
							else if (parm.ToUpper() == "LOW")
								onlyRecordWhenAuxed = AuxPinMode.OnlyRecordWhenLow;
							else
								onlyRecordWhenAuxed = AuxPinMode.NoAuxPinMode;
						}
						break;
					case "X":
						if (parm.Length > 0)
							cmdLineExec = parm;
						break;
					case "R":
						if (parm.Length > 0)
							retryStreamStartTimes = System.Convert.ToInt32(parm);
						break;
				}

				parm = "";
			}

			if (retryStreamStartTimes < 1)
				retryStreamStartTimes = 1;

			if ((runTimeSeconds < 1) || (usePolling == useCallbacks) || (filename.Length < 1))
			{
				ShowHelp();
				return -1;
			}

			// We create the handler class here, even if not doing callbacks, in order to use the pFile member.
			CallbackDataHandlerClass callbackClass = new CallbackDataHandlerClass();
			callbackClass.iLimitNChannels = limitChannels;
			callbackClass.includeSyncAux = includeSyncAux;
			callbackClass.onlyRecordWhenAuxed = onlyRecordWhenAuxed;
			callbackClass.useDeviceSync = useDeviceSync;

			callbackClass.pFile = File.CreateText(filename);

			Console.WriteLine("Initializing the library...");

			// This will actually connect to the devices and work with them. The BertecDeviceNET.BertecDevice
			// object gives you all the functionality you need.
			BertecDeviceNET.BertecDevice hand;
			try
			{
				hand = new BertecDeviceNET.BertecDevice();
			}
			catch (System.Exception ex)
			{
				Console.WriteLine("Unable to initialize the Bertec Device Library (possible missing FTD2XX install).");
				return -1;
			}
			callbackClass.hand = hand;

			hand.AutoZeroing = useAutozeroing;

			Console.WriteLine("Calling Start..");

			hand.Start();

			int startStreamResult = (int)BertecDeviceNET.StatusErrors.GENERIC_ERROR;
			for (int streamRetry = 0; streamRetry < retryStreamStartTimes; ++streamRetry)
			{

				// since we're going to be using this a lot, copy it off
				int numberOfDevices = WaitForDevicesToAppear(hand);

				// A special case handling for the command line parm regarding the buffered data is here:
				// by default the buffer uses a small size (100 to 500 samples) before it starts spilling data; this can be a
				// problem when the command line parm program takes a long time to complete and return - and using polling
				// instead of callbacks will make it worse. For cases like this, setting the buffer size to the max limit
				// of 10000 (10 seconds) is suggested.
				if (cmdLineExec.Length > 0)
				{
					int bufferSize = 10000;
					Console.WriteLine("Setting buffer size to {0} samples", bufferSize);
					hand.MaxBufferedDataSize = bufferSize;   // changing this will cause your memory footprint to
																		  // grow in size by roughly 170*#ofDevices*bufferSize bytes.
				}

				// StartDataStreaming will check if the passed parms can be used with the devices; if it cannot, then BERTEC_UNSUPPORTED_COMMAND will be returned
				// In this case, it is assumed the user knows the devices can support the sync mode but something is preventing the hardware from telling the SDK this.
				// The best solution is to simply re-detect all the devices and try again.
				startStreamResult = StartDataStreaming(hand, useDeviceSync, intClockRate);

				if (startStreamResult == (int)BertecDeviceNET.StatusErrors.NOERROR || startStreamResult == (int)BertecDeviceNET.StatusErrors.STREAM_SUCCESSFUL)
					break;
				else if (startStreamResult == (int)BertecDeviceNET.StatusErrors.UNSUPPORTED_COMMAND)
				{
					Console.WriteLine("Unable to start the data stream due to incompatible settings. Assuming hardware is supposed to support this, so redetecting devices and trying again in 5 seconds");
					hand.RedetectConnectedDevices();
				}
				else
					Console.WriteLine("Unable to start the data stream, error was {0} (probably due to noise on the sync line). Waiting 5 seconds and trying again", startStreamResult);
				System.Threading.Thread.Sleep(5000);
			}

			if (startStreamResult != (int)BertecDeviceNET.StatusErrors.NOERROR && startStreamResult != (int)BertecDeviceNET.StatusErrors.STREAM_SUCCESSFUL)
			{
				Console.WriteLine("Unable to start the data stream, quitting");
				return startStreamResult;
			}

			// Running a program at this point of data collection is provided primarily as an example; your code would obviously not do this,
			// but instead trigger some other function or signaling event that would perform additional work.
			// Note that Process.Start.WaitForExit will block until cmdLineExec exits; this includes a UI block if the cmdLineExec
			// program crashes or throws an exception and a debugger is showing you the exception ui. You can remove WaitForExit(),
			// which will turn the Process.Start call into an asynchronous operation.
			if (cmdLineExec.Length > 0)
			{
				Console.WriteLine("Executing command line {0}", cmdLineExec);
				var p = cmdLineExec.Split(null, 2); // uses whitespace characters to break, and limit to max 2 pieces
				if (p.Length == 1)
					Process.Start(p[0]).WaitForExit();
				else
					Process.Start(p[0], p[1]).WaitForExit();
			}

			if (useDeviceSync == BertecDeviceNET.DataStreamControl.SyncPinMode.INTCLOCK)
			{
				Console.WriteLine("Internal sync mode set");
			}

			// When starting with a zero load, we need to zero after we start; if you call stop after zeroing, it will reset
			// the zero load values.
			if (startWithZeroLoad)
			{
				Console.WriteLine("Zeroing Load...");
				hand.ZeroNow();
				while (hand.AutoZeroState != BertecDeviceNET.AutoZeroStates.ZEROFOUND)
				{
					Console.WriteLine(".");
					System.Threading.Thread.Sleep(100);
				}
				Console.WriteLine(" done");
			}

			if (resampleRate > 0)
			{
				// Set the resampling rate. This will automatically reset the clock and aux modes to zero.
				Console.WriteLine("Setting the resampling rate to {0}", resampleRate);
				for (int devNum = 0; devNum < hand.DeviceCount; ++devNum)
				{
					hand.SetDataRateResampling(devNum, resampleRate);
				}

				onlyRecordWhenAuxed = AuxPinMode.NoAuxPinMode;
			}
			else
			{
				// Set the external clock mode, if any. If the device does not support external clocking modes, then this
				// function call will return UNSUPPORED_COMMAND. Normally you will not want do this and instead use the bertec_DataStreamControl,
				// but this is still provided.
				if (extClock != BertecDeviceNET.ClockSourceFlags.INTERNAL)
				{
					Console.WriteLine("Setting external clock mode to {0}", extClock);
					for (int devNum = 0; devNum < hand.DeviceCount; ++devNum)
					{
						hand.SetExternalClockMode(devNum, extClock);

						// Set the aux pin mode to from the aux pin or the zero pin, depending on the amp - this is the default state, so this is only provided as example code.
						hand.SetAuxPinMode(devNum, BertecDeviceNET.AuxModeFlags.NONE_IN_ZERO); // for AM6500's, this is the aux pin. For AM6800/AM6817, this will be the zero pin.
					}
				}
			}

			// If you wish to use the AUX pin as an OUTPUT, you would pass AUX_OUT_INSTANT and then call SetSyncAuxPinValues with your values.

			// The better way is to use the DataStreamControl.auxPinMode mode set to RUNHIGH or RUNLOW,
			// but this is provided to show how you can "do it yourself".
			if (onlyRecordWhenAuxed != AuxPinMode.NoAuxPinMode)
			{
				Console.WriteLine("Only recording data when the AUX pin is {0}", (onlyRecordWhenAuxed == AuxPinMode.OnlyRecordWhenHigh) ? "high" : "low");
			}

			// When using callbacks (.NET events), we can simply use the Sleep function call to suspend our main thread
			// while the callback thread does all the work for us. 
			// Also note that callbacks SHOULD be set up before calling Start() so that they are available
			// for use as soon as data is present. In your own code you can use a program-specific flag to determine
			// if data should be retained or discarded. Callbacks are also the method of choice when using a GUI
			// system or any other main-thread blocking functionality (ex: heavy database usage or network functionality).
			// As a further note, setting the callback will cause all existing buffered data to be immediately sent to the
			// callback (so no data will be lost, up to the capacity of the data buffer - see ChangeMaxBufferedDataSize
			if (useCallbacks)
			{
				Console.WriteLine("Using callbacks to gather data.");
				hand.OnDataStream += callbackClass.OnDataCallback;
				hand.OnStatus += callbackClass.StatusEvent;

				// This is a very poor way to capture the data for a set amount of time; the Thread.Sleep() function is
				// fairly coarse-grained, so it might not be the exact amount.
				// A much better way would be in your code to use the timestamp value in the data frame to determine when
				// the device sent the data, or count the number of samples (one sample equals 1ms, unless you have elected
				// to enable the downsampling option or the external clock modes).

				System.Threading.Thread.Sleep(1000 * runTimeSeconds); // in your code, you could do real work here.
			}

			// Loop through reading whatever data from the buffer until the timer runs out. You code could do something similar, but instead of
			// waiting 100ms between calls you could do "real work".
			// During this loop you should be checking the return codes from bertec_GetBufferedDataAvailable and bertec_ReadBufferedData, along
			// with periodically calling bertec_GetStatus to see if things have changed (such as devices faulting, being disconnected, etc).

			if (usePolling)
			{
				Console.WriteLine("Using polling to gather data.");

				// allocate an empty array that will get created and filled by ReadBufferedDataStream
				BertecDeviceNET.DataFrame[] dataFrames = new BertecDeviceNET.DataFrame[0];

				int targetTime = Environment.TickCount + (1000 * runTimeSeconds);

				while (Environment.TickCount <= targetTime)
				{
					// Read all the data out of the buffer
					while ((hand.BufferedDataAvailable > 0) && (Environment.TickCount <= targetTime))
					{
						int rc = hand.ReadBufferedDataStream(ref dataFrames);
						if (rc < 0)
						{
							Console.WriteLine("bertec_ReadBufferedData returned error code {0}", rc);
						}
						else if (rc > 0)
						{
							callbackClass.OnDataCallback(dataFrames);  // call the same code as the callback would do. Write out a single frame of data
						}
						else // rc==0; You should never get here since BufferedDataAvailable is being used.
						{
							Console.WriteLine("ReadBufferedDataStream returned no data frames read, even though BufferedDataAvailable said there was.");
						}
					};

					// Buffered data is empty, so pretend to 'do something' here
					System.Threading.Thread.Sleep(25);  // if your main process is in a tight loop like this one, you can overrun the device by continually polling when there is no data there
				}
			}

			hand.Stop();
			Console.WriteLine("Data gather complete, shutting down.");

			// And we close the file.
			callbackClass.pFile.Close();
			callbackClass.pFile = null;

			// Always a good idea to call Dispose on any item that has it. Otherwise, wrapper the block with using().
			hand.Dispose();
			hand = null;

			return 0;
		}
	}
}
