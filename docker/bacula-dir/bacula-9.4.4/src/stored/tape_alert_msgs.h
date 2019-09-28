/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2017 Kern Sibbald

   The original author of Bacula is Kern Sibbald, with contributions
   from many others, a complete list can be found in the file AUTHORS.

   You may use this file and others of this release according to the
   license defined in the LICENSE file, which includes the Affero General
   Public License, v3.0 ("AGPLv3") and some additional permissions and
   terms pursuant to its AGPLv3 Section 7.

   This notice must be preserved when any source code is
   conveyed and/or propagated.

   Bacula(R) is a registered trademark of Kern Sibbald.
*/
/*
 *
 *  Routines for getting and displaying tape alerts
 *
 *   Written by Kern Sibbald, October MMXVI
 *
 */

struct ta_error_handling {
   const char severity;
   const char flags;
   const char *short_msg;
};

#define TA_NONE           (0)
#define TA_DISABLE_DRIVE  (1<<0)
#define TA_DISABLE_VOLUME (1<<1)
#define TA_CLEAN_DRIVE    (1<<2)
#define TA_PERIODIC_CLEAN (1<<3)
#define TA_RETENTION      (1<<4)

/*
 * ta_error_handling determines error handling
 *   Severity determines if we have info, warning or fail (critical) error.
 *   Flags allows setting flags
 *
 */
static struct ta_error_handling ta_errors[] = {
         {' ', TA_NONE,           ""},
/* 1 */  {'W', TA_NONE,           "Read Warning"},
         {'W', TA_NONE,           "Write Warning"},
         {'C', TA_NONE,           "Hard Error"},
         {'C', TA_NONE,           "Media"},
         {'C', TA_NONE,           "Read Failure"},
         {'C', TA_NONE,           "Write Failure"},
         {'W', TA_DISABLE_VOLUME, "Media Life"},
         {'W', TA_DISABLE_VOLUME, "Not Data Grade"},
         {'C', TA_NONE,           "Write Protect"},
/* 10 */ {'I', TA_NONE,           "No Removal"},
         {'I', TA_NONE,           "Cleaning Media"},
         {'I', TA_NONE,           "Unsupported Format"},
         {'C', TA_DISABLE_VOLUME, "Recoverable Snapped Tape"},
         {'C', TA_DISABLE_DRIVE| \
               TA_DISABLE_VOLUME, "Unrecoverable Snapped Tape"},
         {'W', TA_NONE,           "Cartridge Memory Chip Failure"},
         {'C', TA_NONE,           "Forced Eject"},
         {'W', TA_NONE,           "Read Only Format"},
         {'W', TA_NONE,           "Tape Directory Corrupted on load"},
         {'I', TA_NONE,           "Nearing Media Life"},
/* 20 */ {'C', TA_CLEAN_DRIVE| \
               TA_DISABLE_DRIVE| \
               TA_DISABLE_VOLUME, "Clean Now"},
         {'W', TA_PERIODIC_CLEAN, "Clean Periodic"},
         {'C', TA_DISABLE_VOLUME, "Expired Cleaning Media"},
         {'C', TA_RETENTION,      "Invalid Cleaning Media"},
         {'W', TA_NONE,           "Retention Requested"},
         {'W', TA_NONE,           "Dual-Port Interface Error"},
         {'W', TA_NONE,           "Cooling Fan Failure"},
         {'W', TA_NONE,           "Power Supply Failure"},
         {'W', TA_NONE,           "Power Consumption"},
         {'W', TA_DISABLE_DRIVE,  "Drive Maintenance"},
/* 30 */ {'C', TA_DISABLE_DRIVE,  "Hardware A"},
         {'C', TA_DISABLE_DRIVE,  "Hardware B"},
         {'W', TA_NONE,           "Interface"},
         {'C', TA_NONE,           "Eject Media"},
         {'W', TA_NONE,           "Download Fail"},
         {'W', TA_NONE,           "Drive Humidity"},
         {'W', TA_NONE,           "Drive Temperature"},
         {'W', TA_NONE,           "Drive Voltage"},
         {'C', TA_DISABLE_DRIVE,  "Predictive Failure"},
         {'W', TA_DISABLE_DRIVE,  "Diagnostics Required"},
/* 40 */ {'C', TA_NONE,           "Loader Hardware A"},
         {'C', TA_NONE,           "Loader Stray Tape"},
         {'W', TA_NONE,           "Loader Hardware B"},
         {'C', TA_NONE,           "Loader Door"},
         {'C', TA_NONE,           "Loader Hardware C"},
         {'C', TA_NONE,           "Loader Magazine"},
/* 46 */ {'W', TA_NONE,           "Loader Predictive Failure"},
         {' ', TA_NONE,           ""},
         {' ', TA_NONE,           ""},
         {' ', TA_NONE,           ""},
/* 50 */ {'W', TA_NONE,           "Lost Statistics"},
         {'W', TA_NONE,           "Tape directory invalid at unload"},
         {'C', TA_DISABLE_VOLUME, "Tape system area write failure"},
         {'C', TA_DISABLE_VOLUME, "Tape system area read failure"},
/* 54 */ {'C', TA_DISABLE_VOLUME, "No start of data"}
};

/*
 * Long message, sometimes even too verbose.
 */
static const char *long_msg[] = {
         "",
/* 1 */  "The tape drive is having problems reading data. No data has been lost, but there has been a reduction in the performance of the tape. The drive is having severe trouble reading",
         "The tape drive is having problems writing data. No data has been lost, but there has been a reduction in the capacity of the tape. The drive is having severe trouble writing",
         "The operation has stopped because an error has occurred while reading or writing data which the drive cannot correct. The drive had a hard read or write error",
         "Your data is at risk: Media cannot be written/read, or media performance is severely degraded.\n    1. Copy any data you require from the tape.\n    2. Do not use this tape again.\n    3. Restart the operation with a different tape.",
         "The tape is damaged or the drive is faulty. Call the tape drive supplier helpline.  The drive can no longer read data from the tape",
         "The tape is from a faulty batch or the tape drive is faulty:    The drive can no longer write data to the tape.\n    1. Use a good tape to test the drive.\n    2. If the problem persists, call the tape drive supplier helpline.",
         "The tape cartridge has reached the end of its calculated useful life:  The media has exceeded its specified life.\n    1. Copy any data you need to another tape.\n     2. Discard the old tape.",
         "The tape cartridge is not data-grade. Any data you back up to the tape is at risk. The drive has not been able to read the MRS stripes. Replace the cartridge with a data-grade tape.",
         "You are trying to write to a write-protected cartridge. Write command is attempted to a write protected tape. Remove the write-protection or use another tape.",
/* 10 */ "You cannot eject the cartridge because the tape drive is in use. Manual or s/w unload attempted when prevent media removal is enabled. Wait until the operation is complete before ejecting the cartridge.",
         "The tape in the drive is a cleaning cartridge.  Cleaning tape loaded in drive.",
         "You have tried to load a cartridge of a type which is not supported by this drive. Attempted loaded of unsupported tape format, e.g. DDS2 in DDS1 drive.",
         "The operation has failed because the tape in the drive has snapped: Tape snapped/cut in the drive where media can be ejected.\n    1. Discard the old tape.\n    2. Restart the operation with a different tape.",
         "The operation has failed because the tape in the drive has snapped: Tape snapped/cut in the drive where media cannot be ejected.\n    1. Do not attempt to extract the tape cartridge.\n    2. Call the tape drive supplier helpline.",
         "The memory in the tape cartridge has failed, which reduces performance. Memory chip failed in cartridge. Do not use the cartridge for further backup operations.",
         "The operation has failed because the tape cartridge was manually ejected while the tape drive was actively writing or reading.  Manual or forced eject while drive actively writing or reading",
         "You have loaded a cartridge of a type that is read-only in this drive. Media loaded that is read-only format. The cartridge will appear as write-protected.",
         "The directory on the tape cartridge has been corrupted. Tape drive powered down with tape loaded, or permanent error prevented the tape directory being updated. File search performance will be degraded. The tape directory can be rebuilt by reading all the data on the cartridge.",
         "The tape cartridge is nearing the end of its calculated life. Media may have exceeded its specified number of passes. It is recommended that you:\n    1. Use another tape cartridge for your next backup.\n    2. Store this tape cartridge in a safe place in case you need to restore data from it.",
/* 20 */ "The tape drive needs cleaning: The drive thinks it has a head clog, or needs cleaning.\n    1. If the operation has stopped, eject the tape and clean the drive.\n    2. If the operation has not stopped, wait for it to finish and then clean the drive.\n    Check the tape drive users manual for device specific cleaning instructions.",
         "The tape drive is due for routine cleaning: The drive is ready for a periodic clean.\n    1. Wait for the current operation to finish.\n    2. Then use a cleaning cartridge.\n    Check the tape drive users manual for device specific cleaning instructions.",
         "The last cleaning cartridge used in the tape drive has worn out. The cleaning tape has expired.\n    1. Discard the worn out cleaning cartridge.\n    2. Wait for the current operation to finish.\n    3. Then use a new cleaning cartridge.",
         "The last cleaning cartridge used in the tape drive was an invalid type: Invalid cleaning tape type used.\n    1. Do not use this cleaning cartridge in this drive.\n    2. Wait for the current operation to finish.\n    3. Then use a valid cleaning cartridge.",
         "The tape drive has requested a retention operation. The drive is having severe trouble reading or writing, which will be resolved by a retention cycle.",
         "A redundant interface port on the tape drive has failed. Failure of one interface port in a dual-port configuration, e.g. Fibrechannel.",
         "A tape drive cooling fan has failed. Fan failure inside tape drive mechanism or tape drive enclosure.",
         "A redundant power supply has failed inside the tape drive enclosure. Check the enclosure users manual for instructions on replacing the failed power supply. Redundant PSU failure inside the tape drive enclosure or rack subsystem.",
         "The tape drive power consumption is outside the specified range. Power consumption of the tape drive is outside specified range.",
         "Preventive maintenance of the tape drive is required. The drive requires preventative maintenance (not cleaning). Check the tape drive users manual for device specific preventive maintenance tasks or call the tape drive supplier helpline.",
/* 30 */ "The tape drive has a hardware fault: The drive has a hardware fault that requires reset to recover.\n    1. Eject the tape or magazine.\n    2. Reset the drive.\n    3. Restart the operation.",
         "The tape drive has a hardware fault:  The drive has a hardware fault which is not read/write related or requires a power cycle to recover.\n    1. Turn the tape drive off and then on again.\n    2. Restart the operation.\n    3. If the problem persists, call the tape drive supplier helpline.\n    Check the tape drive users manual for device specific instructions on turning the device power on and off.",
         "The tape drive has a problem with the host interface: The drive has identified an interfacing fault.\n    1. Check the cables and cable connections.\n    2. Restart the operation.",
         "The operation has failed. Error recovery action:\n    1. Eject the tape or magazine.\n    2. Insert the tape or magazine again.\n    3. Restart the operation.",
         "The firmware download has failed because you have tried to use the incorrect firmware for this tape drive. Firmware download failed. Obtain the correct firmware and try again.",
         "Environmental conditions inside the tape drive are outside the specified humidity range. Drive humidity limits exceeded.",
         "Environmental conditions inside the tape drive are outside the specified temperature range. Drive temperature limits exceeded.",
         "The voltage supply to the tape drive is outside the specified range. Drive voltage limits exceeded.",
         "A hardware failure of the tape drive is predicted. Call the tape drive supplier helpline. Predictive failure of drive hardware.",
         "The tape drive may have a fault. Check for availability of diagnostic information and run extended diagnostics if applicable.   The drive may have had a failure which may be identified by stored diagnostic information or by running extended diagnostics (eg Send Diagnostic). Check the tape drive users manual for instructions on running extended diagnostic tests and retrieving diagnostic data.",
/* 40 */ "The changer mechanism is having difficulty communicating with the tape drive:   Loader mech. is having trouble communicating with the tape drive.\n    1. Turn the autoloader off then on.\n    2. Restart the operation.\n    3. If problem persists, call the tape drive supplier helpline.",
         "A tape has been left in the autoloader by a previous hardware fault: Stray tape left in loader after previous error recovery.\n    1. Insert an empty magazine to clear the fault.\n    2. If the fault does not clear, turn the autoloader off and then on again.\n    3. If the problem persists, call the tape drive supplier helpline.",
         "There is a problem with the autoloader mechanism. Loader mech. has a hardware fault.",
         "The operation has failed because the autoloader door is open: Tape changer door open:\n    1. Clear any obstructions from the autoloader door.\n    2. Eject the magazine and then insert it again.\n    3. If the fault does not clear, turn the autoloader off and then on again.\n    4. If the problem persists, call the tape drive supplier helpline.",
         "The autoloader has a hardware fault: The loader mechanism has a hardware fault that is not mechanically related.\n    1. Turn the autoloader off and then on again.\n    2. Restart the operation.\n    3. If the problem persists, call the tape drive supplier helpline.\n    Check the autoloader users manual for device specific instructions on turning the device power on and off.",
         "The autoloader cannot operate without the magazine. Loader magazine not present.\n    1. Insert the magazine into the autoloader.\n    2. Restart the operation.",
/* 46 */  "A hardware failure of the changer mechanism is predicted. Predictive failure of loader mechanism hardware. Call the tape drive supplier helpline.",
          "",
          "",
          "",
/* 50 */ "Media statistics have been lost at some time in the past, Drive or library powered down with tape loaded.",
         "The tape directory on the tape cartridge just unloaded has been corrupted. Error prevented the tape directory being updated on unload. File search performance will be degraded. The tape directory can be rebuilt by reading all the data.",
         "The tape just unloaded could not write its system area successfully: Write errors while writing the system log on unload.\n    1. Copy data to another tape cartridge.\n    2. Discard the old cartridge.",
         "The tape system area could not be read successfully at load time: Read errors while reading the system area on load.\n    1. Copy data to another tape cartridge.\n    2. Discard the old cartridge.",
/* 54 */ "The start of data could not be found on the tape: Tape damaged, bulk erased, or incorrect format.\n    1. Check you are using the correct format tape.\n    2. Discard the tape or return the tape to your supplier."
};
