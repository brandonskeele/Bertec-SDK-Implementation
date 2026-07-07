/***************************************************************************
 * Bertec device interface library header                                  *
 *  v. 2.56                                                                *
 *  Copyright (C) 2008-2025 Bertec Corporation                             *
 ***************************************************************************/

#ifndef BERTECIF_H
#define BERTECIF_H

#include <stdint.h>
#include <stddef.h>

// The compiler will typically warn about a zero-sized array, so to avoid noise pollution we disable this warning in MSVC
#pragma warning(push)
#pragma warning(disable:4200)

#ifndef BIFH_EXPORT
   #if defined(WIN32) || defined(_WIN32)
      #ifdef BERTECDEVICEDLL_BUILD
         #define BIFH_EXPORT __declspec(dllexport)
      #else
         #define BIFH_EXPORT __declspec(dllimport)
      #endif
   #else
      #ifdef BERTECDEVICEDLL_BUILD
         #define BIFH_EXPORT extern "C" __attribute__((visibility("default")))
      #else
         #define BIFH_EXPORT
      #endif
   #endif
#endif

#ifndef CALLBACK
   #if defined(WIN32) || defined(_WIN32)
      #define CALLBACK __stdcall
   #else
      #define CALLBACK 
   #endif
#endif


#ifdef  __cplusplus
   extern "C" {
#endif

/** The version define is the version # of this Device DLL. You can check it via bertec_LibraryVersion
    If it doesn't match what this one is, then structures and/or functions may have changed,
    and you should proceed with caution. */
#define BERTEC_LIBRARY_VERSION   (0x256)

/** This defines how many channels the DLL can support. Currently each device can support up to 32 channels. */
#define BERTEC_MAX_CHANNELS            (32)
#define BERTEC_MAX_SERIAL_LENGTH       (31)
#define BERTEC_MAX_MODELNAME_LENGTH    (31)
#define BERTEC_MAX_CHANNELNAME_LENGTH  (15)

/** library handle */
typedef const void * bertec_Handle;

#pragma pack(push)
#pragma pack(8)
/** information about a device */
struct bertec_DeviceInfo
{
   unsigned short fwVersion;     /* firmware revision of the hardware  */
   unsigned char prVersionMajor; /* protocol revision being used */
   unsigned char prVersionMinor;
   int   status;                 /* status of the device, separate from the overall state. Zero is good, negative bad. */
   unsigned short samplingFreq;  /* native sampling frequency in Hertz. Note that this may not be the delivered freq if certain clock modes are used. */
   unsigned short hwid;          /* hardware id of the device (see bertec_HardwareIDs) */
   char  serial[BERTEC_MAX_SERIAL_LENGTH+1];       /* the serial # of the device */
   char  model[BERTEC_MAX_MODELNAME_LENGTH+1];     /* the model # of the device */
   int   channelCount;           /* how many output channels there are */
   bool  has64BitTimestamp;      /* if set, then the device has an internal 64-bit timestamp value which can be reset (1ms accuracy) */
   bool  hasAuxSyncPins;         /* if set, then the device has full control over the aux and sync pins and the data block will reflect this. */
   float dimensionWidth;         /* width of the plate in mm, if known */
   float dimensionHeight;        /* height of the plate in mm, if known */
   float offsetX;                /* X offset from the center origin in meters, if known. 0 == dead center */
   float offsetY;                /* Y offset from the center origin in meters, if known. 0 == dead center */
   float offsetZ;                /* vetical Z offset from the center origin in meters, if known. 0 == physical top of plate */
   char  channelNames[BERTEC_MAX_CHANNELS][BERTEC_MAX_CHANNELNAME_LENGTH+1];  /* the channel names from the device's eprom, null terminated */
};


/** Channel data that is part of the frame of data. */
struct bertec_ChannelData
{
   int   count;  // how much channel data is in this structure. Copied from bertec_DeviceInfo::channelCount
   float data[BERTEC_MAX_CHANNELS];
};

/** Additional data that is part of the frame of data. 
    Note that the auxData and syncData are only valid on devices with firmware that support both pins (bertec_DeviceInfo::hasAuxSyncPins is set); will be zero otherwise.
    The timestamp value is always present, and will be computed by the SDK if the device hardware or firmware does not support it (bertec_DeviceInfo::has64BitTimestamp). */
struct bertec_AdditionalData
{
   uint64_t       timestamp;     // the hardware timestamp when this frame of data was generated. May repeat or skip depending on the external clocking mode - see bertec_ExternalClockMode.
   uint64_t       eventCounter;  // event counter from the device. This can be affected by the current bertec_SyncModeFlags. 
   uint64_t       frameCounter;  // the sequence value of this frame of data. Monotonic, incremented when each new frame is output by the SDK.
   unsigned char  auxData;       // rolling 8-bit value of the AUX pin status. MSB is the current value. Valid in all AUX pin modes. Sampled at 8x 1000hz.
   unsigned char  syncData;      // rolling 8-bit value of the SYNC pin status. MSB is the current value. Valid in all SYNC pin modes. Sampled at 8x 1000hz.
};

/** A single device's block of data, both the channel data and the additional timestamp/sync data. This is part of the bertec_DataFrame */
struct bertec_DeviceData
{
   bertec_ChannelData      channelData;
   bertec_AdditionalData   additionalData;
};

/** A single block of data as sent via bertec_DataStreamCallback or retrieved via bertec_ReadBufferedData. The frame contains a single sample of data from all of the devices. */
struct bertec_DataFrame
{
   int                deviceCount;   // same as bertec_DeviceCount
   bertec_DeviceData  device[0];     // the device data is allocated per device and contains up to deviceCount data blocks
};
#pragma pack(pop)

/** Sync pin mode flags */
enum bertec_SyncModeFlags
{
   SYNC_IN_SAMPLED     = 0x00, // The SYNC pin is an input, but its value is not interpreted in any way. This is the default power-up mode.
   SYNC_NONE           = 0x00, // alias for SYNC_IN_SAMPLED for legacy code.
   SYNC_OUT_MASTER     = 0x01, // The SYNC pin is outputting a 1kHz square wave clock with a reference mark embedded every 2000ms.
   SYNC_IN_SLAVE       = 0x02, // The SYNC pin is inputting a 1kHz square wave clock with optional reference marks.
   SYNC_OUT_PATGEN     = 0x04, // The SYNC pin is outputting a random pattern. This is useful for debugging.
   SYNC_IN_CONTINUOUS  = 0x05, // The SYNC pin is inputting a continuous 1kHz square wave clock without reference marks.
   SYNC_OUT_CONTINUOUS = 0x07, // The SYNC pin is outputting a continuous 1kHz square wave clock without reference marks.
   SYNC_OUT_INSTANT    = 0x08, // The SYNC pin is outputting the value most recently set via the bertec_SetSyncAuxPinValues function.
   SYNC_OUT_FREQGEN    = 0x09  // The SYNC pin is acting as a frequency generator, its frequency set via the bertec_SetFrequencyGeneration function.
};

/** Aux pin mode flags */
enum bertec_AuxModeFlags
{
   AUX_NONE_AUX_IN_ZERO = 0x00, // AM6500: The AUX pin is an input, but its value is not interpreted in any way.
                                // AM6800/AM6817: the input is taken from the ZERO pin, and a logic low level keeps the analog output signals zeroed.
                                // This is the default power-up mode.
   AUX_IN_SAMPLED =       0x01, // The AUX/ZERO pin is an input, and its value is not interpreted in any way.
   AUX_OUT_INSTANT =      0x02, // The AUX is outputting the value most recently set via the bertec_SetSyncAuxPinValues function.
   AUX_OUT_PATGEN =       0x04  // The AUX pin is outputting a random pattern. This is useful for debugging.
};

/** Internal/External Clock Source flags */
enum bertec_ClockSourceFlags
{
   CLOCK_SOURCE_INTERNAL = 0x00, // This is the default state and the SDK will present data at the native device rate (1000hz).
                                 // Averaging will affect this. All Sync and Aux modes are available, including multiple device sync.
   CLOCK_SOURCE_EXT_RISE = 0x01, // This will cause data to be presented whenever the SYNC pin changes from low (0) to high (1),
                                 // which can be higher (up to 4000hz) or lower (down to 1hz).
                                 // Averaging is disabled, and the SYNC mode is forced to SYNC_NONE. All other Aux modes are available,
                                 // but multiple device sync is disabled.
   CLOCK_SOURCE_EXT_FALL = 0x02, // This will cause data to be presented whenever the SYNC pin changes from high (1) to low (0),
                                 // which can be higher (up to 4000hz) or lower (down to 1hz).
                                 // Averaging is disabled, and the SYNC mode is forced to SYNC_NONE. All other Aux modes are available,
                                 // but multiple device sync is disabled.
   CLOCK_SOURCE_EXT_BOTH = 0x03, // This will cause data to be presented whenever the SYNC pin changes from either a low-to-high or high-to-low state.
                                 // Averaging is disabled, and the SYNC mode is forced to SYNC_NONE. All other Aux modes are available,
                                 // but multiple device sync is disabled.
   CLOCK_SOURCE_MANUAL_HOLD = 0x10, // this will cause the SDK to block data coming from the device until the clock source is changed to one
                                    // of the other modes.
   CLOCK_SOURCE_NO_INTERPOLATE = 0x80, // By default the ClockSource logic will attempt to perform a fractional delay on the input data.
                                       // This can cause the data signal to appear to be delayed by up to 4.875ms. If such a delay would
                                       // cause problems with your code path you will need to pass this bit flag along with the clock source
                                       // to change from a fractional delay to a simpler skip-and-fill. Skip-and-fill will either omit or
                                       // duplicate channel data depending on when the edge signal occurs in the data flow.
};

/** Flow control settings that can be used with either SYNC or AUX pins to allow the SDK to control the delivery of data through the
    data callback or polling function. See bertec_SetPinFlowControl */
enum bertec_FlowControl
{
   FLOWCONTROL_NONE = 0,   /* The default state; the pin will not be used to control the data flow (suggested mode when using int/ext clock mode).
                              Current pin mode will not be changed */
   FLOWCONTROL_RUNHIGH = 0x20, /* The data stream will only be delivered when the selected pin is at a high (1) state.
                                  The device's pin mode will be set to IN_SAMPLED; do not use this with the SYNC pin if also using int/ext clock mode.
                                  Requires updated firmware. */
   FLOWCONTROL_RUNLOW,       /* The data stream will only be delivered when the selected pin is at a low (0) state.
                                The device's pin mode will be set to IN_SAMPLED; do not use this with the SYNC pin if also using int/ext clock mode.
                                Requires updated firmware. */
   FLOWCONTROL_START_PULSE_ON,  /* The data stream will only start once there is a low-high-low transition on the selected pin.
                                   Once triggered, the data stream will not stop and will continue to run; additional pulses will be ignored.
                                   The device's pin mode will be set to SYNC_IN_SAMPLED; do not use this with the SYNC pin if also using int/ext clock mode.
                                   Requires updated firmware. */
   FLOWCONTROL_START_PULSE_ONOFF,  /* The data stream will only start once there is a low-high-low transition on the selected pin,
                                      and then continue to run until the next low-high-low transition. Each pulse pair will cause data to start and then stop.
                                      The device's pin mode will be set to SYNC_IN_SAMPLED; do not use this with the SYNC pin if also using int/ext clock mode.
                                      Requires updated firmware */
};

/** IO pin constants - used to select a io pin for control using the pin mode functions. Only 1 value can be used at a time. */
enum bertec_IOPins
{
   IO_PIN_SYNC = 0x00,  // the SYNC pin, used for e.g. synchronizing the data sampling; available on AM6500, AM6800, AM6817
   IO_PIN_AUX = 0x01,   // the AUX/ZERO pin, used for general-purpose I/O; available on AM6500 as the bidirectional AUX pin, and on AM6800 and AM6817 as the input-only ZERO pin
   IO_PIN_CH8 = 0x04,   // the CH8 output pin, available on AM6817E and higher
   IO_PIN_CH7 = 0x05,   // the CH7 output pin, available on AM6817E and higher
   IO_PIN_NONE = 0xff,  // special value used in query requests to indicate that no particular pin is being queried; results in an error if used to set a pin
};

/** Pin modes - used to set the operating mode for a given IOPin. This is a superset of both bertec_SyncModeFlags and bertec_AuxModeFlags */
enum bertec_PinModes
{
   PINMODE_IN_SAMPLED = 0x00,       // The pin is an input, and its value is not interpreted in any way.
   PINMODE_OUT_SYNC_MARK = 0x01,    // The pin is outputting a 1kHz square wave clock with a reference mark embedded every 2000ms.
   PINMODE_IN_SYNC_MARK = 0x02,     // The pin is inputting a 1kHz square wave clock with optional reference marks.
   PINMODE_OUT_RANDPAT = 0x04,      // The pin is outputting a random pattern. This is useful for debugging.
   PINMODE_IN_CONTINUOUS = 0x05,    // The pin is inputting a continuous 1kHz square wave clock without reference marks.
   PINMODE_OUT_CONTINUOUS = 0x07,   // The pin is outputting a continuous 1kHz square wave clock without reference marks.
   PINMODE_OUT_INSTANT = 0x08,      // The pin is outputting the value most recently set via the bertec_SetSyncAuxPinValues function.
   PINMODE_OUT_FREQGEN = 0x09,      // The pin is acting as a frequency generator, its frequency set via the bertec_SetFrequencyGeneration function.
   PINMODE_IN_ZERO = 0x0A,          // The pin input controls the zeroing of analog outputs. A low logic level keeps the analog output signals zeroed. (depends on hardware support)
   PINMODE_OUT_LOAD = 0x0B,         // The pin outputs the analog value of the load from the transducer. (depends on hardware support)
};

/** Autozero states as returned by bertec_GetAutozeroState */
enum bertec_AutozeroStates
{
   AUTOZEROSTATE_NOTENABLED= 0,  // Not enabled yet
   AUTOZEROSTATE_WORKING   = 1,  // Autozero is enabled, but has not yet been achieved
   AUTOZEROSTATE_ZEROFOUND = 2   // Autozero has been achieved and will continue to automatically rezero as needed.
};

/** Computed channels options. This is a bit field array. Note that the Sway Angle requires a set subject height to be correct */
enum bertec_ComputedChannelFlags
{
   NO_COMPUTED_CHANNELS = 0,
   COMPUTE_COP_VALUES = 0x01,
   COMPUTE_COG_VALUES = 0x02,
   COMPUTE_SWAY_ANGLE = 0x04,
   COMPUTE_ALL_VALUES = 0x07
};

/** AggregateDeviceMode flags for dual plate setups. Passed to bertec_SetAggregateDeviceMode in order to control additional options */
enum bertec_AggregateDeviceMode
{
   NO_AGGREGATEDEVICEMODE = 0,   // the default mode - no special processing is done
   FRONT_TO_BACK_AGGMODE = 1,    // the plates are arranged length-wise, front to back, with the front plate rotated 180 degrees.
   SIDE_BY_SIDE_AGGMODE = 2,       // the plates are arranged side-by-side (ie: treadmill), with the right plate rotated 180 degrees
};

/** Common plate dimensions for the standard Bertec plates (Essential, Functional, and Sport). Other plate sizes exist; see the bertec_DeviceInfo structure.
    These are all expressed in millimeters.
    The placement of the Medial Malleolus line on the Functional plate aligns with an Essential when both plates are aligned on the front; to map the Y position
    of the line between a Functional and Essential (normalize the position between the two), you will need to add in a constant value of
    155mm (ESSENTIAL_MALLEOLUS_OFFSET_Y-FUNCTIONAL_MALLEOLUS_OFFSET_Y) to the computed Functional COPY value.
    Note that you DO NOT NEED TO DO THIS unless you care about the stance position and the markings on the plate; if your application just cares about
    the raw COP position, then you should NOT add any constant offsets and instead use the as-computed COP values.
    The Lateral Calcaneus X offset values are provided as informational use only and will be negative for the left side of the plate and positive for the right.
    */
enum bertec_CommonDimensions
{
   ESSENTIAL_WIDTH_MM = 508,
   ESSENTIAL_HEIGHT_MM = 457,
   ESSENTIAL_MALLEOLUS_OFFSET_Y = 65, // this is where the foot placement center line is located on the plate, offset from the center. 
   ESSENTIAL_CALCANEUS_OFFSET_X_MINOR = 11, // this is the first hash mark, offset from the center. 
   ESSENTIAL_CALCANEUS_OFFSET_X_MIDDLE = 13, // this is the second hash mark, offset from the center. 
   ESSENTIAL_CALCANEUS_OFFSET_X_MAJOR = 15, // this is the third and largest hash mark, offset from the center. 

   FUNCTIONAL_WIDTH_MM = 508,
   FUNCTIONAL_HEIGHT_MM = 762,  // note that when Aggregate Device Mode is enabled, you should consider the virtual plate size 2x of this (1524mm)
   FUNCTIONAL_MALLEOLUS_OFFSET_Y = -90, // this is where the foot placement center line is located on the plate, offset from the center. Not valid for Aggregate Device Mode 
   FUNCTIONAL_CALCANEUS_OFFSET_X_MINOR = 11, // this is the first hash mark, offset from the center. 
   FUNCTIONAL_CALCANEUS_OFFSET_X_MIDDLE = 13, // this is the second hash mark, offset from the center. 
   FUNCTIONAL_CALCANEUS_OFFSET_X_MAJOR = 15, // this is the third and largest hash mark, offset from the center. 

   SPORT_WIDTH_MM = 762,
   SPORT_HEIGHT_MM = 457,
   SPORT_MALLEOLUS_OFFSET_Y = 65, // this is where the foot placement center line is located on the plate, offset from the center. 
   SPORT_CALCANEUS_OFFSET_X_MINOR = 11, // this is the first hash mark, offset from the center. 
   SPORT_CALCANEUS_OFFSET_X_MIDDLE = 13, // this is the second hash mark, offset from the center. 
   SPORT_CALCANEUS_OFFSET_X_MAJOR = 15, // this is the third and largest hash mark, offset from the center. 
};

/**
   The hardware id (HWID) is used to determine the type of hardware that is being communicating with. This the type of the hardware that the PC
   is directly communicating with, NOT the type of any further downstream device (ex: plate, pylon).
   If the PC is connected to a transducer, either via USB or directly, the hardware ID will be that of the transducer.
   If the PC is connected to a signal converter, the hardware ID will be that of the signal converter.
   Note that the id values are grouped in blocks of 10, and that the lower digit should be masked off. For example, any hardware id with a value
   in the 90-99 range should be considered an AM6817
*/
enum bertec_HardwareIDs
{
   HWID_UNKNOWN= 0, /** the device firmware did not provide a hardware id value */

   HWID_AM6500 = 20, /** serial-to-USB signal converter */
   HWID_AM6500D = 20,
   HWID_AM6500E = 21,
   HWID_AM6500F = 23, /** 22 intentionally skipped */
   HWID_AM6500G = 24,
   HWID_AM6500H = 25,
   HWID_AM6500J = 26,
   HWID_AM6500K = 27,
   HWID_AM6500L = 28,

   HWID_AM6143 = 40, /** 1st generation USB preamplifier */
   HWID_AM6143B = 40,
   HWID_AM6143C = 41,
   HWID_AM6143D = 42,

   HWID_TM0801B = 50,
   HWID_DR1910A = 60,

   HWID_AM6514 = 80,
   HWID_AM6514B = 80,
   HWID_AM6514C = 81,
   HWID_AM6514D = 82,
   HWID_AM6514E = 83,
   HWID_AM6514F = 84,
   HWID_AM6514G = 85,
   HWID_AM6514H = 86,

   HWID_AM6817 = 90, /** serial-to-USB/analog signal converters. Different hardware ids refer to different generations of the 6817 series */
   HWID_AM6817A = 90,
   HWID_AM6817B = 91,
   HWID_AM6817C = 92,
   HWID_AM6817D = 93,
   HWID_AM6817E = 94,

   HWID_AM6580A = 100,

   HWID_AM6145 = 110, /** force plate preamplifier via a direct connection without a signal converter */
   HWID_AM6145A = 110,
   HWID_AM6145B = 111,
   HWID_AM6145C = 112,

   HWID_AM6146 = 120, /** custom USB preamplifier project, not publicly released. */

   HWID_AM6504 = 140,
   HWID_AM6504A = 140,
   HWID_AM6504B = 141,
   HWID_AM6504C = 142,
   HWID_AM6504D = 143,
   HWID_AM6504E = 144,
   HWID_AM6504F = 145,
   HWID_AM6504G = 146,
   HWID_AM6504H = 147,

   HWID_AM6800 = 150, /** serial-to-USB/analog signal converter */
   HWID_AM6800A = 150,
   HWID_AM6800B = 151,
   HWID_AM6800C = 152,
   HWID_AM6800D = 153,
   HWID_AM6800E = 154,
   HWID_AM6800F = 155,

   HWID_AM6147 = 160, /** 2nd generation USB preamplifier */
   HWID_AM6147A = 160,
   HWID_AM6147B = 161,
   HWID_AM6147C = 162,
   HWID_AM6147D = 163,
   HWID_AM6147E = 164,

   HWID_UNPROGRAMMED = -1, /** Should never be presented */
};

/** Defined errors and status values */
enum bertec_StatusErrors
{
   BERTEC_NOERROR                = 0,/** Generic no error */
   BERTEC_NO_BUFFERS_SET         = -2,/** no data buffers were allocated */
   BERTEC_DATA_BUFFER_OVERFLOW   = -4,/** the internal buffer has become saturated; either data polling isn't occurring often/fast enough,
                                          or else your callback is blocking for too long. Old data is now lost. */

   BERTEC_NO_DEVICES_FOUND       = -5,/** there are apparently no devices attached */

   BERTEC_DATA_READ_NOT_STARTED  = -6,/** didn't start the data process - call Start */

   BERTEC_NO_DATA_RECEIVED       = -11,/** no data is being received from the devices, check the cables */

   BERTEC_DEVICE_HAS_FAULTED     = -12,/** the device has failed in some manner - power off the device, check all connections, power back on */

   BERTEC_UNABLE_TO_START_STARTED   = -30, /** bertec_Start() was called twice; the second call was ignored */
   BERTEC_UNABLE_TO_START_STOPPING  = -31, /** bertec_Start() was called while the last bertec_Stop() call was still being processed; the bertec_Start() call was ignored. */
   BERTEC_UNABLE_TO_STOP_NOTRUNNING = -32, /** bertec_Stop() was called but bertec_Start() was not called first; the library is already stopped. */
   BERTEC_UNABLE_TO_STOP_STOPPING   = -33, /** bertec_Stop() was called twice; the second call was ignored */
   BERTEC_UNABLE_TO_STOP_STARTING   = -34, /** bertec_Stop() was called while the last bertec_Start() call was still being processed; the bertec_Stop() call may not take effect. */

   BERTEC_UNABLE_TO_START_FAILED = -35,   /** Internal error - device threads have failed to spin up */
   BERTEC_UNABLE_TO_STOP_FAILED  = -36, /** Internal error - device threads have failed to shut down */

   BERTEC_LOOKING_FOR_DEVICES    = -45,/** the sdk is scanning for devices; the next status will be either BERTEC_NO_DEVICES_FOUND or BERTEC_INITILIZING_DEVICES */
	BERTEC_INITIALIZING_DEVICES   = -46,/** the sdk is currently connecting to and initializing the devices; this phase can take from 0.1 to 70.0 seconds, depending on device count */
													/** the next status will be either BERTEC_NO_DEVICES_FOUND or BERTEC_DEVICES_READY */
   BERTEC_DEVICES_READY          = -50,/** there are devices connected */

   BERTEC_AUTOZEROSTATE_WORKING  = -51,/** currently finding the zero values */

   BERTEC_AUTOZEROSTATE_ZEROFOUND= -52,/** the zero leveling value was found */

   BERTEC_ERROR_INVALIDHANDLE    = -100,/** handle is invalid */

   BERTEC_UNABLE_TO_LOCK_MUTEX   = -101,/** internal logic error */

   BERTEC_UNSUPPORTED_COMMAND    = -200,/** the firmware doesn't support the command that was attempted to be used */

   BERTEC_INVALID_PARAMETER      = -201,
   BERTEC_INDEX_OUT_OF_RANGE     = -202,

   BERTEC_FUNCTION_BUSY          = -203,/** either the method or a sub-method is busy - possibly calling the same function from multiple threads. */


	BERTEC_INVALID_STATUS			= -204,

	/** Various StartDataStream status event values. During the StartDataStream call, you will get these status events in your Status Callback;
	    if you are using StartDataStream_Async you will also get these as part of the callback signal. You can use these to monitor both the
		 progress of the data stream setup and handle failure/success results. */
	BERTEC_STREAM_SYNC_NOT_STABLE = -500,/** During the stream setup, the sync pins were not in a consistent state (pulses on sync pin) */
	BERTEC_STREAM_SYNC_NOT_HELD   = -501,/** During the stream setup for the sync RUNHIGH/RUNLOW modes, the sync pins were not in the correct lo/hi state */
	BERTEC_STREAM_AUX_NOT_HELD		= -502,/** During the stream setup for the aux RUNHIGH/RUNLOW modes, the aux pins were not in the correct lo/hi state */
	BERTEC_STREAM_SYNC_COUNT_OFF  = -503,/** During the stream setup, the # of bits on the sync pins diverged too much (variable pulses on different sync pins) */
	BERTEC_STREAM_AUX_COUNT_OFF   = -504,/** During the stream setup, the # of bits on the aux pins diverged too much (variable pulses on different aux pins) */
	BERTEC_STREAM_BAD_SAMPLE_COUNT= -505,/** During the stream setup, unable to collect data */
	BERTEC_STREAM_UNABLE_TO_START = -506,/** the stream setup process was unable to start for some reason */
	BERTEC_STREAM_STARTING			= -550,/** indicates the stream setup is about to start */
	BERTEC_STREAM_SUCCESSFUL		= -551,/** indicates the stream setup completed successfully */
	BERTEC_STREAM_FAILURE			= -552,/** indicates the stream setup failed (would have gotten other errors, above) */

   BERTEC_GENERIC_ERROR          = -32767
};


/** returns the version of the library. This should match BERTEC_LIBRARY_VERSION */
BIFH_EXPORT unsigned int bertec_LibraryVersion();

/** initialize the library, returns a handle */
BIFH_EXPORT bertec_Handle bertec_Init(void);

/** close the library when it's no longer needed. Returns BERTEC_NOERROR if succeeds */
BIFH_EXPORT int bertec_Close(bertec_Handle bHand);

/** verifies that the handle passed is a legitimate bertec_Handle item. Returns FALSE if not. */
BIFH_EXPORT bool bertec_CheckHandle( bertec_Handle bHand );

/** start the device detection and data collection */
BIFH_EXPORT int bertec_Start(bertec_Handle bHand);

/** stops all data collection and disconnects from all devices */
BIFH_EXPORT int bertec_Stop(bertec_Handle bHand);

/** returns the current status */
BIFH_EXPORT int bertec_GetStatus(bertec_Handle bHand);

/** returns the number of devices connected to the system. Only valid once start has been called and devices have been found. */
BIFH_EXPORT int bertec_GetDeviceCount(bertec_Handle bHand);

/** copies the current device info at the given index to the passed buffer. If there is no device at that index returns OUT OF RANGE */
BIFH_EXPORT int bertec_GetDeviceInfo(bertec_Handle bHand, int deviceIndex, bertec_DeviceInfo * info, size_t infoSize);

/** convenience function to access the device's serial number */
BIFH_EXPORT int bertec_GetDeviceSerialNumber(bertec_Handle bHand,int deviceIndex,char *buffer,size_t bufferSize);

/** convenience function to access the device's model number */
BIFH_EXPORT int bertec_GetDeviceModelNumber( bertec_Handle bHand, int deviceIndex, char *buffer, size_t bufferSize );

/** convenience function to access the device's channels. Returns # of channels and fills the buffer with a copy of bertec_DeviceInfo::channelNames */
BIFH_EXPORT int bertec_GetDeviceChannels(bertec_Handle bHand,int deviceIndex,char *buffer,size_t bufferSize);

BIFH_EXPORT int bertec_GetDeviceChannelCount( bertec_Handle bHand, int deviceIndex );

BIFH_EXPORT int bertec_GetDeviceChannelName(bertec_Handle bHand,int deviceIndex,int channelIndex,char *buffer,size_t bufferSize);

// Similar to the serial #, but returns the low-level USB device id. Can be used for a unique identifier for the device if needed.
BIFH_EXPORT int bertec_GetDeviceIDString( bertec_Handle bHand, int deviceIndex, char *buffer, size_t bufferSize );


/** The Library will not start to deliver data on the DataStream callback or DataStream buffer polling until 
    you call bertec_StartDataStream with the desired mode; data will continue to be delivered on the Immediate monitoring callback on a per-device status.
    If you wish to make the Library behave as previous versions, use SYNCPINMODE_NONE and AUXPINMODE_NONE as the control parameters which will
    deliver data without any additional processing or control.
    Starting the data stream is only valid *AFTER* devices have been detected; your application's code should check the status of the connection
    by either polling bertec_GetStatus or using bertec_StatusCallback to check for BERTEC_DEVICES_READY. Once the devices are ready, then you can
    start data streaming.
*/

struct bertec_DataStreamControl
{
   enum SyncPinMode
   {
      SYNCPINMODE_NONE = 0,  /* The default state; the devices do no synchronization and do not handle the external clock signal.
                                All devices' SYNC pin modes will be set to SYNC_IN_SAMPLED. */
      SYNCPINMODE_CLASSIC,   /* This is a basic mode and will work with older firmware; one device delivers the master reference clock on the SYNC pin
                                and the other devices respond to it. In this mode it is not possible to start or stop the data stream or resample the incoming data. */
      SYNCPINMODE_INTCLOCK,  /* One device is generating a clock on the SYNC pin, taking the place of the external clock. Data cannot be start or stopped based
                                on the clock signal, but the frequency can be changed via bertec_SetFrequencyGeneration. The internalClockSource index value
                                must be set to the device that will perform the frequency generation; this device will have it's SYNC pin mode set to SYNC_OUT_FREQGEN
                                and all other device's SYNC pin mode set to SYNC_IN_SAMPLED.
                                Requires updated firmware. */
      SYNCPINMODE_EXTCLOCK,  /* The devices are getting an external clock connected to the SYNC pins, and the Library will resample the data to match.
                                Data can be started and stopped based on the external clock input. All devices' SYNC pin modes will be set to SYNC_IN_SAMPLED.
                                Requires updated firmware. */
      SYNCPINMODE_INTAUXCONTROL, /* the sync pin's 'quiet period' will be driven by the aux pin mode; only valid if auxPinMode is anything other than AUXPINMODE_NONE*/
      SYNCPINMODE_EXTAUXCONTROL,
		SYNCPINMODE_EXTUNCONTROLLED,	/* The sync pin's input is not expected to be held in a steady state during the init phase, but is expected to be properly controlled.
												   This still allows for leading-edge realignment, and pauses in the sync clock will act as a dynamic quiet period/edge realignment.
													This is the currently preferred mode for most setups and should be used in preference of classic SYNCPINMODE_EXTCLOCK. */
      SYNCPINMODE_RUNHIGH = 0x20, /* The data stream will only be delivered when the SYNC pin is at a high (1) state. All devices' SYNC pin modes will be set to SYNC_IN_SAMPLED.
                                     Requires updated firmware. */
      SYNCPINMODE_RUNLOW,       /* The data stream will only be delivered when the SYNC pin is at a low (0) state. All devices' SYNC pin modes will be set to SYNC_IN_SAMPLED.
                                   Requires updated firmware. */
      SYNCPINMODE_START_PULSE_ON,  /* The data stream will only start once there is a low-high-low transition on the SYNC pin.
                                      Once triggered, the data stream will not stop and will continue to run; additional pulses will be ignored.
                                      All devices' SYNC pin modes will be set to SYNC_IN_SAMPLED. Requires updated firmware. */
      SYNCPINMODE_START_PULSE_ONOFF,  /* The data stream will only start once there is a low-high-low transition on the SYNC pin,
                                         and then continue to run until the next low-high-low transition. Each pulse pair will cause data to start and then stop.
                                         All devices' SYNC pin modes will be set to SYNC_IN_SAMPLED. Requires updated firmware. */
   };

   enum AuxPinMode
   {
      AUXPINMODE_NONE = 0,       /* The default state; the aux pin is not treated special in any way. This is the only mode supported with SYNCPINMODE_CLASSIC.
                                    All devices' AUX pin modes will be set to AUX_NONE_AUX_IN_ZERO or AUX_IN_SAMPLED depending on firmware. */
      AUXPINMODE_RUNHIGH = 0x20, /* The data stream will only be delivered when the AUX pin is at a high (1) state.
                                    All devices' AUX pin modes will be set to AUX_IN_SAMPLED. Requires updated firmware. */
      AUXPINMODE_RUNLOW,         /* The data stream will only be delivered when the AUX pin is at a low (0) state.
                                    All devices' AUX pin modes will be set to AUX_IN_SAMPLED. Requires updated firmware. */
      AUXPINMODE_START_PULSE_ON, /* The data stream will only start once there is a low-high-low transition on the AUX pin.
                                    Once triggered, the data stream will not stop and will continue to run; additional pulses will be ignored.
                                    All devices' AUX pin modes will be set to AUX_IN_SAMPLED. Requires updated firmware. */
      AUXPINMODE_START_PULSE_ONOFF, /* The data stream will only start once there is a low-high-low transition on the AUX pin,
                                       and then continue to run until the next low-high-low transition. Each pulse pair will cause data to start and then stop.
                                       All devices' AUX pin modes will be set to AUX_IN_SAMPLED. Requires updated firmware. */
   };

   int           size;                   /* size of the control structure; must be set to sizeof(bertec_DataStreamControl). Used for future expansion. */
   SyncPinMode   syncPinMode;            /* controls how the hardware SYNC pin is used. */
   AuxPinMode    auxPinMode;             /* controls how the hardware AUX pin is used. */
   int           internalClockSource;    /* device index of the internal clock source; only used when syncPinMode = SYNCPINMODE_INTCLOCK */
   float         internalClockFrequency; /* the frequency to be generated by the internal clock source; only used when syncPinMode = SYNCPINMODE_INTCLOCK */
                                         /* If this is set to zero, then SYNCPINMODE_INTCLOCK will be ignored. */
   unsigned int  deviceFilterBitmask;    /* used to control which devices are delivering data via the bertec_DataStreamCallback. Each bit corresponds to a
                                            device index; bit 0 == device index 1, bit 1 == index 1, etc. Setting this mask to a zero (all bits off)
                                            is treated the same as all bits on (0xFFFFFFFF).
                                            It is entirely possible to set the internalClockSource value to a device that has been masked out by
                                            deviceFilterBitmask – doing effectively turns the masked device into an external clock source.
                                            Setting deviceFilterBitmask to allow only 1 device through is not an error.
                                            NOTE: your application will need to handle mapping the resulting data frame to any real device index. */
};

// Use this vaule to set the bertec_DataStreamControl::internalClockFrequency to the default frequency that the hardware uses.
#define DEFAULT_INTERNALCLOCKFREQUENCY (1000.0)

/** Checks the input values for the control struct and if they can be used with the current hardware, returns OK */
BIFH_EXPORT int bertec_CanStartDataStream( bertec_Handle bHand, const bertec_DataStreamControl* pControlStruct );

/** Sets up the desired data streaming mode for the connected devices, and limits which devices are read from.
    The Library will do all the needed checks on incoming data and SYNC/AUX pin signals, only returning once the expected conditions are met.
    Some of the functionality here overlaps with other API methods, such as bertec_SetExternalClockMode, but provides a different level of control.
    NOTE: Outside of SYNCPINMODE_NONE, SYNCPINMODE_CLASSIC, and AUXPINMODE_NONE, sync and aux pin functionally requires updated firmware to function.
    If outdated firmware is used, the Library will reject the requested mode type and return an error.
*/
BIFH_EXPORT int bertec_StartDataStream( bertec_Handle bHand, const bertec_DataStreamControl* pControlStruct );



#ifndef bertec_StartDataStreamNotifcation
typedef void (CALLBACK *bertec_StartDataStreamNotifcation)(bertec_Handle bHand, const bertec_DataStreamControl * control, int status, void * userData);
#endif

/** This is the same as bertec_StartDataStream, but will set up a background worker thread that performs the setup, calling the
	 status_notifcation callback periodically. bertec_StartDataStreamAsync will return immediately with a success or error code.
	 If the data stream is currently being set up by a prior call to bertec_StartDataStreamAsync, the new call will terminate the
	 previous one.
*/
BIFH_EXPORT int bertec_StartDataStreamAsync( bertec_Handle bHand, const bertec_DataStreamControl* pControlStruct, bertec_StartDataStreamNotifcation status_notifcation, void * userData );

/** Stops the current data stream and returns the hardware back to their default states. This will have the effect of resetting
    both syncPinMode and auxPinMode mode to NONE, and clearing the device filter bitmask. */
BIFH_EXPORT int bertec_StopDataStream( bertec_Handle bHand );

/** Gets a copy of the current control structure being used. */
BIFH_EXPORT int bertec_GetCurrentDataStreamControl( bertec_Handle bHand, bertec_DataStreamControl* pControlStructOut, size_t structOutSize );

/** zero the input against what the plate has loaded on it right now */
BIFH_EXPORT int bertec_ZeroNow(bertec_Handle bHand);

/** enable/disable the autozeroing of the plate, which occurs if the plate is loaded at less than 40 Newtons for
    about 3.5 seconds. */
BIFH_EXPORT int bertec_SetEnableAutozero(bertec_Handle bHand,int enableFlag);
BIFH_EXPORT int bertec_GetEnableAutozero( bertec_Handle bHand );

/** returns the current autozering status. */
BIFH_EXPORT bertec_AutozeroStates bertec_GetAutozeroState(bertec_Handle bHand);

/** returns the zero level noise value for a device. ZeroNow/EnableAutozero must have been called.
    This is a computed value that can be used for advanced filtering. 
    This is always a positive value; negative values indicate no zeroing or some other error. */
BIFH_EXPORT float bertec_GetZeroLevelNoiseValue(bertec_Handle bHand,int deviceIndex,int channelIndex);

/** Average the samples. SamplesToAverage should be >= 2. Setting to 1 or less turns it off */
BIFH_EXPORT int bertec_SetAveraging(bertec_Handle bHand,int samplesToAverage);
BIFH_EXPORT int bertec_GetAveraging( bertec_Handle bHand );

/** Perform low-pass filtering on the samples. SamplesToFilter should be >=1. Setting to 0 or less turns it off. */
BIFH_EXPORT int bertec_SetLowpassFiltering(bertec_Handle bHand,int samplesToFilter);
BIFH_EXPORT int bertec_GetLowpassFiltering( bertec_Handle bHand);

/** callback registration functions
    * Callbacks are called from a separate thread.
    * The callback method is called with the same userData value that was passed to the register function;
      if this userData value is not needed it should be NULL.
    * Only one callback at a time for each callback interface may be registered at once; you cannot have two 
      bertec_DataStreamCallback methods going at the same time, for example. If your application requires multiple
      callback support, your callback implementation will need to handle this.
    * Registering a new callback will replace the previous one.
    */


/** Registers callbacks for the combined data frame from all devices, after the data processing has been completed (resampling, averaging, filtering, etc).
    Each time the function registered to the callback is called, the function will get a complete 'frame' of data from all devices.
    Depending on the options being used, the apparent data rate (calling frequency) of this callback will vary and may not be called at
    the same rate of the hardware device (1000hz). For example, is 2x averaging is being used, this will be called at an apparent rate of
    500hz. Note that due to PC hardware limitations, the data rate should only be considered an average over time; a 1000hz rate of data
    will effectively be called 15-16 times per ms in a 'burst' fashion.
    The bertec_DataStreamCallback callback can be used in conjunction with bertec_ImmediateDeviceDataCallback. */
#ifndef bertec_DataStreamCallback
typedef void (CALLBACK *bertec_DataStreamCallback)(bertec_Handle bHand, const bertec_DataFrame * dataFrame, void * userData);
#endif

BIFH_EXPORT int bertec_RegisterDataStreamCallback(bertec_Handle bHand,bertec_DataStreamCallback, void * userData);
BIFH_EXPORT int bertec_UnregisterDataStreamCallback(bertec_Handle bHand,bertec_DataStreamCallback, void * userData);


/** Registers callbacks for a single device's data prior to any processing, such as resampling, averaging, filtering, or computed channels;
    however, zeroing offsets (bertec_ZeroNow, bertec_SetEnableAutozero) are handled prior to this.
    Each time the function registered to the callback is called, the function will get the device index, the unique ID of the device,
    and a single block of data. This callback will be called at the data rate of the device (1000hz), and the ordering of the callback between
    devices is not guaranteed (ex: you may get device # 1, 1, 2, 3, 3, 2, 1, 1 etc.). However, data ordering within the device itself
    is guaranteed (ex: you will get block #1, 2, 3, 4, and never 1, 4, 3, 2 for example). If there is a connection issue, then data blocks
    may be skipped - this can be detected by a discontinuous additionalData.timestamp values. The timestamp value comes from the device itself
    and increments at a monotonic rate (ex: 1,2,3,4 - a pattern of 1,2,4 indicates block #3 was dropped between the device and the PC).
    This callback is designed to be used as a feature where your application needs to have some sort of "monitoring" of the device data stream
    outside of normal data processing.
    Special Note: this callback is invoked within the context of the low-level USB interface thread - your implementation MUST return
    as quickly as possible to avoid data loss.
    The bertec_ImmediateDeviceDataCallback callback can be used in conjunction with bertec_DataStreamCallback. */
#ifndef bertec_ImmediateDeviceDataCallback
typedef void (CALLBACK *bertec_ImmediateDeviceDataCallback)(bertec_Handle bHand, int deviceIndex, const char* uid, const bertec_DeviceData * deviceData, void * userData);
#endif

BIFH_EXPORT int bertec_RegisterImmediateDeviceDataCallback( bertec_Handle bHand, bertec_ImmediateDeviceDataCallback, void * userData );
BIFH_EXPORT int bertec_UnregisterImmediateDeviceDataCallback( bertec_Handle bHand, bertec_ImmediateDeviceDataCallback, void * userData );


#ifndef bertec_StatusCallback
typedef void (CALLBACK *bertec_StatusCallback)(bertec_Handle bHand, int status, void * userData);
#endif

BIFH_EXPORT int bertec_RegisterStatusCallback(bertec_Handle bHand,bertec_StatusCallback, void * userData);
BIFH_EXPORT int bertec_UnregisterStatusCallback(bertec_Handle bHand,bertec_StatusCallback, void * userData);

/** This will set a callback that is used to sort the device order. By default they are sorted by usb hardware id/connection */
#ifndef bertec_DeviceSortCallback
typedef void (CALLBACK *bertec_DeviceSortCallback)(bertec_DeviceInfo* pInfos,int deviceCount,int* orderArray, void * userData);
#endif
BIFH_EXPORT int bertec_RegisterDeviceSortCallback(bertec_Handle bHand,bertec_DeviceSortCallback, void * userData);
BIFH_EXPORT int bertec_UnregisterDeviceSortCallback(bertec_Handle bHand,bertec_DeviceSortCallback, void * userData);

/** This will set a callback that is used whenever a new line of text is written to the device log file. The callback is called in the context of a worker
    thread and as such your own code should handle things appropriately. Use this to monitor and display diagnostic text data. The leading digits are the 
    millisecond timestamp when the message was generated (which will differ from when it is actually logged). */
#ifndef bertec_DeviceLogCallback
typedef void (CALLBACK *bertec_DeviceLogCallback)(const char* pszText, void * userData);
#endif
BIFH_EXPORT void bertec_RegisterDeviceLogCallback(bertec_DeviceLogCallback, void * userData);
BIFH_EXPORT void bertec_UnregisterDeviceLogCallback(bertec_DeviceLogCallback, void * userData);

/** If not using callbacks, calling this will read one sample from the buffered data and return ether 1 (data read but more left) or 0 (did not read, no more left)
    The dataFrame pointer MUST point to a valid bertec_DataFrame block, which will be filled in with the appropriate values. Passing NULL
    will result in an error. If there are no more data blocks waiting to be read in the buffer this will return zero.
    The dataFrameSize MUST be equal to the size of the bertec_DeviceData structure times the # of devices + the size of an int. */
BIFH_EXPORT int bertec_ReadBufferedDataStream(bertec_Handle bHand, bertec_DataFrame * dataFrame, size_t dataFrameSize );

/** To facilitate create and using the buffer data reader, these two convenience functions have been provided.  */

/** This is a convenience for use with bertec_ReadBufferedData; it simply allocates a buffer large enough to handle
    the current number of connected devices. You must call free before exiting the sdk or re-allocating another buffer. */
BIFH_EXPORT bertec_DataFrame * bertec_AllocateReadBufferedData( bertec_Handle bHand, size_t* dataFrameSizeOut );
BIFH_EXPORT bertec_DataFrame * bertec_AllocateReadBufferedDataForCount( bertec_Handle bHand, int deviceCount, size_t* dataFrameSizeOut );
BIFH_EXPORT int bertec_FreeAllocatedReadBufferedData( bertec_Handle bHand, bertec_DataFrame* dataFrame );

struct bertec_BufferedData
{
   typedef void* BufferedDataHandle;  // do not change
   BufferedDataHandle handle;

   int frameCount;      // # of data frame samples that have been taken; use this to loop through the data via bertec_BufferedDataGetDataFrame
};

// Copies the current buffered data array from the library and fills in the dataPtr object. The library will continue to buffer data after this call.
// This call MUST be matched with a call to bertec_FreeTakeBufferedData with the SAME dataPtr value.
BIFH_EXPORT int bertec_TakeBufferedData( bertec_Handle bHand, bertec_BufferedData* dPtr );   // creates a bertec_BufferedData object and fills it in.
BIFH_EXPORT int bertec_FreeTakeBufferedData( bertec_Handle bHand, bertec_BufferedData* dPtr );  // releases the bertec_BufferedData object

BIFH_EXPORT bertec_DataFrame* bertec_BufferedDataGetDataFrame( bertec_BufferedData* dPtr, int index ); // returns the pointer to the data; do not modify it


/** This will return how many blocks of data are in the internal buffer and can be read via bertec_ReadBufferedData.
    This is a shortcut to calling bertec_ReadBufferedData(bHand,NULL)
    Only use this if your code is using bertec_ReadBufferedData (polling) instead of data callbacks; during callbacks this may return 0 or 1,
	 since callbacks can be serviced very quickly.
    */
BIFH_EXPORT int bertec_GetBufferedDataAvailable(bertec_Handle bHand);

/** This will return the last available bertec_AdditionalData.frameCounter in the buffered data that is waiting to be sent to the callback or the polling call.
    Returns 0 if there is no buffered data waiting to be handled either by the callback or the polling call.
	 NOTE: this is not the first item in the buffer (the next one to be read by polling or sent by callback).
	 */
BIFH_EXPORT uint64_t bertec_GetLastBufferedFrameCounterValue( bertec_Handle bHand );


/** Discards all current data in the buffer, both processed and in flight. Note that doing so will introduce 'gaps' in the data stream,
    and should not be done except under the most strict circumstances. */
BIFH_EXPORT int bertec_ClearBufferedData(bertec_Handle bHand);

/** By default the SDK will buffer up to 500 samples before discarding old data. If you believe your system cannot keep up or are using slow polling, then
    increase this value. Be aware that larger values (ex: 5000) will dramatically increase memory usage which can impact your application.
    Note that changing the size will force a discard of all currently buffered data, so do this before calling Start. */
BIFH_EXPORT int bertec_ChangeMaxBufferedDataSize(bertec_Handle bHand,int newMaxSamples);

/** returns the currently set max buffer size, in samples. The default is 100 */
BIFH_EXPORT int bertec_GetMaxBufferedDataSize(bertec_Handle bHand);

/** Enables or disables the ability to compute certain channels from the force device's FZ, MX, and MY values. If the device does not have the
    appropriate channels, then setting this will have no effect. Note that turning on the COG and Sway Angle channels may incur a small CPU usage
    penalty and require setting the subject height via bertec_SetSubjectHeight. The COP calculation is a simple moments over force function and
    has little to no additional CPU overhead. The COP also does not need the subject height set in order to be used.
    This function must be called after bertec_Init but before bertec_Start; calling this while devices are actively delivering data will result in an error. */
BIFH_EXPORT int bertec_SetComputedChannelsFlags( bertec_Handle bHand, bertec_ComputedChannelFlags newMode );

BIFH_EXPORT bertec_ComputedChannelFlags bertec_GetComputedChannelsFlags( bertec_Handle bHand );

/** For the SwayAngle computation, the height of the person standing on the plate must be known. If this value is not set the program will
    default to 1.5 meters (1500mm). This value CAN be changed while data is being collected. */
BIFH_EXPORT int bertec_SetSubjectHeight( bertec_Handle bHand, float heightMM );
BIFH_EXPORT int bertec_GetSubjectHeight( bertec_Handle bHand, float* heightMMOut );

/** For 6-channel plates with the full Fx, Fy, Fz, Mx, My, and Mz channels, you can tell the SDK to adjust for a floor covering. 
    By default, no adjustment is made but setting this to any value above 0 will affect the outgoing Mx and My values (along with the
	 corresponding COP/COG values, if computed channels are turned on). Note that Mz is not affected by this.
	 By default, this value is 0 which does not adjust the Mx and My values
	 The cover thickness must be a positive number; passing anything less than 0 will return an error.
	 Passing 0 will turn off the adjustment (the default).
	 Setting this will ONLY affect the output values for devices with the full 6-channel compliment; other devices will NOT be affected.
	 This value CAN be changed while data is being collected. */
BIFH_EXPORT int bertec_SetCoveringThickness( bertec_Handle bHand, float heightMM );
BIFH_EXPORT int bertec_GetCoveringThickness( bertec_Handle bHand, float* heightMMOut );

/** Returns true if any device has the needed channels (Fx, Fy, Fz, Mx, My, and Mz) for the covering thickness adjustment. */
BIFH_EXPORT bool bertec_CanHaveCoveringThickness( bertec_Handle bHand );

/** Enables or disables the ability to combine the output of two plates as one long virtual plate. This can be enabled or disabled at any point,
    and the output from the callback or data block will change accordingly. If this mode is turned on then the bertec_DataFrame::deviceCount value
    will be set to 1 even if there are more than one device connected, but Bertec_GetDeviceCount will always return the true number of devices
    connected to the system.
    In order for this to work properly both devices must be of the same type, same size, and have the same data channels. You should not try to
    combine a balance plate with a force plate, or a sport plate with a functional model for example. */
BIFH_EXPORT int bertec_SetAggregateDeviceMode( bertec_Handle bHand, bertec_AggregateDeviceMode newMode );
BIFH_EXPORT bertec_AggregateDeviceMode bertec_GetAggregateDeviceMode( bertec_Handle bHand );

/** Enables the ability for the SDK to clock the data stream against an external sync or clock source tied into the physical SYNC connection.
    Setting this to any non-zero value overrides the internal 1000hz hardware clock, allowing the data to be either under or over sampled as needed.
    Special note: using an external clock disables Averaging, low-pass Filtering, and multiple plate sync abilities; the SyncPinMode will be set to SYNC_NONE.
    Your hardware needs will dictate if this mode is suitable for your configuration, and your hardware must be capable of delivering the proper
    SYNC signal. Failure to do so will cause either random samples or no samples at all.
    */
BIFH_EXPORT int bertec_SetExternalClockMode( bertec_Handle bHand, int deviceIndex, bertec_ClockSourceFlags newMode );

/** Sets the SYNC pin operating mode. This is only valid for devices that support the extended SYNC and AUX feature set. Overrides the current master/slave relationship. */
BIFH_EXPORT int bertec_SetSyncPinMode(bertec_Handle bHand,int deviceIndex,bertec_SyncModeFlags newMode);

/** Sets the AUX pin operating mode. This is only valid for devices that support the extended SYNC and AUX feature set. */
BIFH_EXPORT int bertec_SetAuxPinMode(bertec_Handle bHand,int deviceIndex,bertec_AuxModeFlags newMode);

/** Sets the given pin to the selected mode. This is only valid for devices that support the extended SYNC and AUX feature set. Only one pin at a time can be set;
    if you desire to set multiple pins to the same or different modes, you must call this function multiple times with different selected pin enum values.
    Calling this with selectedPin set to IO_PIN_NONE will result in an error. */
BIFH_EXPORT int bertec_SetPinMode( bertec_Handle bHand, int deviceIndex, bertec_IOPins pin, bertec_PinModes newMode );

/** Sets both the SYNC and AUX output pins to the passed values; these values only take effect if the given pin has been set to _INSTANT.
    If the pin has not been set to _INSTANT, then the passed value is ignored. Note that you must pass both values even if you intend to only set one pin.
    Only valid for devices that support the extended SYNC and AUX feature set. */
BIFH_EXPORT int bertec_SetSyncAuxPinValues(bertec_Handle bHand,int deviceIndex, int syncBoolValue, int auxBoolValue);

/** Sets the flow control for the given device to the set pin. The SDK will only deliver data if the given pin meets the flow control setting
    (ex: the pin is high or a pulse has been received). Using this requires the correct hardware (external 6500/6800 amp) and correct firmware.
    Only valid for devices that support the extended SYNC and AUX feature set; if the connected hardware does not support this functionality,
    a BERTEC_UNSUPPORTED_COMMAND error will be returned.
    Note that while the 'pin' parameters uses the bertec_IOPins enum, only the SYNC and AUX values are allowed here; using CH7, CH8, or NONE
    will result in an BERTEC_INVALID_PARAMETER error.
    Note that calling bertec_StartDataStream/bertec_StartDataStreamAsync/bertec_StopDataStream will reset the flow control mode
    to what is requested in the data stream control block.
    */
BIFH_EXPORT int bertec_SetPinFlowControl( bertec_Handle bHand, int deviceIndex, bertec_IOPins pin, bertec_FlowControl flowControl );
BIFH_EXPORT int bertec_GetPinFlowControl( bertec_Handle bHand, int deviceIndex, bertec_IOPins pin, bertec_FlowControl* flowControl );

/** Sets or clears a callback function for when the given pin state changes from high to low or log to high. This is handled
    by internal logic that reads the incoming sync and aux pings and looks for changes in the bit state of the pin, calling the callback function
    whenever this bit pattern changes from 0 to 1 (nowhigh=true) or 1 to 0 (nowhigh=false). Since the hardware pins (sync and aux)
    are sampled at an 8000hz rate, your callback could be called at a maximum rate of 4000hz (assuming a max bit flip of 50% ex 01010101).
    Special Implementation Note: Since the callback is called within the context of the USB device's working thread,
    you MUST take care to allow your function to be thread-safe and re-entrant.
    bertec_RegisterPinStateChangeCallback will return an error if the the pin enum is not SYNC or AUX.
    Note that the PinStateChangeCallback functionality is unaffected by PinFlowControl or by internal or external clock signaling;
    the logic reads the 'raw' pin value from the hardware. See also bertec_ImmediateDeviceDataCallback.
    To facilitate your application in tracking these pin states, a pointer to the data frame for the device that is currently being processed is also
    passed. Possible uses for this include examining the force values or the timestamp/frame counters.
    Note: This callback is invoked during the specific device's data processing - you MUST handle this as quickly as possible to avoid data loss!
    */
#ifndef bertec_PinStateChangeCallback
typedef void (CALLBACK *bertec_PinStateChangeCallback)(bertec_Handle bHand, bertec_IOPins pin, int deviceIndex, bool nowhigh, const bertec_DeviceData* data, void * userData);
#endif
BIFH_EXPORT int bertec_RegisterPinStateChangeCallback( bertec_Handle bHand, bertec_IOPins pin, bertec_PinStateChangeCallback, void * userData );
BIFH_EXPORT int bertec_UnregisterPinStateChangeCallback( bertec_Handle bHand, bertec_IOPins pin, bertec_PinStateChangeCallback, void * userData );

/** Sets and enables the data rate resampling. This will disable the SYNC and AUX pin modes, returning the device to SAMPLED mode;
    the SYNC and AUX data values will always be zero. This feature will work with any device, even if there is no hardware support for SYNC or AUX.
    Setting the newFrequency to 0 will turn off the data rate resampling, and return to the hardware data rate of 1000hz.
    Note that not all frequencies are available; the resulting value is equal to floor(8000/round(8000/input));
    also note that data rate resampling can introduce delays in the data.
    This is an advanced function, and is typically not needed for most projects.
    */
BIFH_EXPORT int bertec_SetDataRateResampling( bertec_Handle bHand, int deviceIndex, int newFrequency );
BIFH_EXPORT int bertec_GetDataRateResampling( bertec_Handle bHand, int deviceIndex );

/** Sets the frequency generator parameters for a given bertec_IOPins enum. The frequency generator is only active when the pin is in a FREQGEN mode, 
    and both the hardware and firmware supports it.
    The frequency generation will be a 50% duty cycle square wave output.
    Returns an error if the device or pin does not support frequency generation or the input frequency is out of range.
    Currently, only the IO_PIN_SYNC supports frequency generation; all other pins will return an error (subject to hardware change). */
BIFH_EXPORT int bertec_SetFrequencyGeneration( bertec_Handle bHand, int deviceIndex, bertec_IOPins pin, float frequency );

/** Returns the min and max value for the frequency generator; if the device or pin does not support frequency generation, will return an error */
BIFH_EXPORT int bertec_GetFrequencyGenerationLimits( bertec_Handle bHand, int deviceIndex, bertec_IOPins pin, float* frequencyMin, float* frequencyMax );

/** Sets the device's internal bertec_AdditionalData::timestamp value to the given value. */
BIFH_EXPORT int bertec_ResetDeviceTimestamp(bertec_Handle bHand,int deviceIndex, uint64_t newTimestampValue);

/** Sets all of the devices' internal bertec_AdditionalData::timestamp values to the same given value. */
BIFH_EXPORT int bertec_ResetAllDeviceTimestamps(bertec_Handle bHand, uint64_t newTimestampValue);

/** Sets the device's internal bertec_AdditionalData::timestamp value to the given value when the timestamp reaches the condition value. */
BIFH_EXPORT int bertec_ResetDeviceTimestampAtMark(bertec_Handle bHand,int deviceIndex, uint64_t newTimestampValue, uint64_t futureConditionValue);

/** Sets all of the devices' internal bertec_AdditionalData::timestamp values to the given value when the each timestamp reaches the condition value. */
BIFH_EXPORT int bertec_ResetAllDeviceTimestampsAtMark(bertec_Handle bHand, uint64_t newTimestampValue, uint64_t futureConditionValue );

/** By default, the library will only present data via the callback or data polling when *all* devices have data, allowing for software sync,
    device aggregation, and data averaging. This is typically the desired mode, but some applications may benefit from turning this functionality off.
    If the enabled parameter is set to 0 (false), then the library will present data whenever *any* device has data, even if the others do not.
    Software sync, device aggregation, and data averaging will *not* be performed in this mode. The data frame received by the callback or data polling
    will be incomplete; your implementation must be ready to check for and handle cases where device #1 presents data but #2 will not, and then some
    frames later that will change to #2 has data but #1 does not; the data will appear to be unaligned with zero values for the no-data-present device structures.
    The simplest method to handle this situation is to check the channelData.count value for the device; if this is zero, there is no data for that device
    in the current frame.
    Turning off unified data implies your application will do it's own post-processing of the data, using the timestamp number to perform some kind of specialized alignment.
    Call this function prior to calling Start in order to ensure that the data being received is in the format expected.
    Using non-unified data mode with a single plate has no net effect.
    Computed channels, device timestamps and sync pin settings (bertec_SetComputedChannelsFlags, bertec_ResetDeviceTimestamp, bertec_SetExternalClockMode, etc)
    will still function if unified data is turned off; however, data averaging and aggregation (bertec_SetAveraging, bertec_SetAggregateDeviceMode) will not.
*/
BIFH_EXPORT int bertec_SetUnifiedDataMode( bertec_Handle bHand, int enabled );
BIFH_EXPORT int bertec_GetUnifiedDataMode( bertec_Handle bHand );

/** this sets or gets the mode where you are expecting to have both portable (non-sync capable plates) combined with sync-capable amplifiers.
    In this mode, the sync and aux values from the amplifiers are copied to the non-sync plate(s), which gives the portable plates the appearance
	 of supporting sync and data resampling. This should only be done for VERY specific setups and needs, and the portable plates will not actually
	 be in full sync with the rest of the hardware, since they are not reading the sync input line from the same source at the same moment in time.
	 You will need to set this before calling StartDataStream, since this affects the handling of the sync mode flags.
*/
BIFH_EXPORT int bertec_SetBlendedDeviceMode( bertec_Handle bHand, int enabled );
BIFH_EXPORT int bertec_GetBlendedDeviceMode( bertec_Handle bHand );

/** set the data processing work priority. Advanced functionality, usually not needed. */
BIFH_EXPORT void bertec_SetWorkPriority(bertec_Handle bHand,int priority);

/** signals the sdk that it should perform a device rescan and reinit all the devices. This is the same as physically unplugging and
    then replugging all devices from the usb connection at the same time. The status BERTEC_LOOKING_FOR_DEVICES will be emitted. Does not block. */
BIFH_EXPORT int bertec_RedetectConnectedDevices( bertec_Handle bHand );

/** returns a dynamically computed value of the sample rate from the device, in hertz. This value is updated approximately every two seconds,
    and is affected by the external clock mode, if any, and is further impacted by overall system performance/load. */
BIFH_EXPORT int bertec_DeviceDataRate( bertec_Handle bHand, int deviceIndex, float* rateOut, uint64_t* updatedWhenOut );

/** Returns the computed sync pin pulse rate for the given device. Returns error if not supported for device. */
BIFH_EXPORT int bertec_ReadCurrentSyncPulseRate( bertec_Handle bHand, int deviceIndex, float* rateOut, uint64_t* updatedWhenOut );

/** returns a dynamically computed value of the effective data rate of the data being fed into the buffer and data callback; expressed in hertz.
    This value is updated approximately every five seconds and is affected by the external clock mode, if any, and is further impacted by overall system performance/load. */
BIFH_EXPORT int bertec_EffectiveDataRate( bertec_Handle bHand, float* rateOut, uint64_t* updatedWhenOut );

/** This will reset the internal sync counters to zero; this is used with dual synced plates */
BIFH_EXPORT int bertec_ResetSyncCounters(bertec_Handle bHand);

/** Set or changes the output folder for the device logs and how long the logs should be kept. You should call this before anything else.
    Defaults to the %temp%/bertec-device-logs folder and 7 days. Pass NULL for the outputFolder to use the default directory, and 0 to turn off age cleaning.*/
BIFH_EXPORT void bertec_SetDeviceLogDirectory(const char *outputFolder,int maxAge);

/** Puts a copy of the current device log filename into the buffer array, and returns the length. If either buffer or maxBufferSize is NULL or 0,
    returns the needed minimum buffer size. */
BIFH_EXPORT int bertec_GetCurrentDeviceLogFilename(char* buffer,int maxBufferSize);

/** Turns on or off the device log output (by default this is on).
	 You will want to call this before anything else so that the log file is not created by accident.
	 Note that turning off the log file will still allow bertec_DeviceLogCallback to work so you can do whatever with it.*/
BIFH_EXPORT void bertec_SetDeviceLogEnable( bool enableFileOut );

/** Fills the passed buffer with a text string of the error value - useful for debugging/logging. */
BIFH_EXPORT void bertec_GetErrorString( bertec_StatusErrors ee, char* buffer, size_t maxBufferSize );

#ifdef  __cplusplus
   }
#endif

#pragma warning(pop)

#endif   /*BERTECIF_H*/
