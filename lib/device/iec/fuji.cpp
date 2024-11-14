#ifdef BUILD_IEC

#include "fuji.h"

#include <driver/ledc.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <utility>

#include "string_utils.h"
#include "../../../include/debug.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fsFlash.h"
#include "fnWiFi.h"
#include "network.h"
#include "led.h"
#include "siocpm.h"
#include "clock.h"
#include "utils.h"
#include "status_error_codes.h"

iecFuji theFuji; // global fuji device object

// Constructor
iecFuji::iecFuji() : IECFileDevice()
{
    // Helpful for debugging
    for (int i = 0; i < MAX_HOSTS; i++)
        _fnHosts[i].slotid = i;
}

int iecFuji::get_disk_id(int drive_slot)
{
    return _fnDisks[drive_slot].disk_dev.id();
}

std::string iecFuji::get_host_prefix(int host_slot)
{
    return _fnHosts[host_slot].get_prefix();
}


// Initializes base settings and adds our devices to the SIO bus
void iecFuji::setup(systemBus *bus)
{
    Debug_printf("iecFuji::setup()\n");

    _populate_slots_from_config();

    FileSystem *ptrfs = fnSDFAT.running() ? (FileSystem *)&fnSDFAT : (FileSystem *)&fsFlash;
//    iecPrinter::printer_type ptype = Config.get_printer_type(0);
    iecPrinter::printer_type ptype = iecPrinter::printer_type::PRINTER_COMMODORE_MPS803; // temporary
    Debug_printf("Creating a default printer using %s storage and type %d\r\n", ptrfs->typestring(), ptype);
    iecPrinter *ptr = new iecPrinter(4, ptrfs, ptype);
    fnPrinters.set_entry(0, ptr, ptype, Config.get_printer_port(0));

    // 04-07 Printers / Plotters
    bus->attachDevice(ptr);

    // 08-15 Drives
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
      {
        _fnDisks[i].disk_dev.setDeviceNumber(BUS_DEVICEID_DISK+i);
        bus->attachDevice(&_fnDisks[i].disk_dev);
      }

    /*
    Serial.print("Network "); bus->addDevice(new iecNetwork(), 16);     // 16-19 Network Devices
    Serial.print("CPM "); bus->addDevice(new iecCpm(), 20);             // 20-29 Other
    Serial.print("Clock "); bus->addDevice(new iecClock(), 29);
    */
    setDeviceNumber(30); bus->attachDevice(this);                 // 30    FujiNet
}


void iecFuji::set_fuji_iec_status(int8_t error, const std::string msg) 
{
  //set_iec_status(error, last_command, msg, fnWiFi.connected(), 15);
}


void iecFuji::getStatus(char *buffer, byte bufferSize)
{
  strcpy(buffer, "OK\r");
}


void iecFuji::execute(const char *cmd, byte cmdLen)
{
  setStatus("99,SYNTAX ERROR,00,00\r", 22);
}


void iecFuji::reset()
{
  setStatus("73,FUJINET-CBM,00,00\r", 22);
}



// Temporary(?) function while we move from old config storage to new
void iecFuji::_populate_slots_from_config()
{
    // Debug_printf("_populate_slots_from_config()\n");
    for (int i = 0; i < MAX_HOSTS; i++)
    {
        if (Config.get_host_type(i) == fnConfig::host_types::HOSTTYPE_INVALID)
            _fnHosts[i].set_hostname("");
        else
            _fnHosts[i].set_hostname(Config.get_host_name(i).c_str());
    }

    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        _fnDisks[i].reset();

        if (Config.get_mount_host_slot(i) != HOST_SLOT_INVALID)
        {
            if (Config.get_mount_host_slot(i) >= 0 && Config.get_mount_host_slot(i) <= MAX_HOSTS)
            {
                strlcpy(_fnDisks[i].filename, Config.get_mount_path(i).c_str(), sizeof(fujiDisk::filename));
                _fnDisks[i].host_slot = Config.get_mount_host_slot(i);
                if (Config.get_mount_mode(i) == fnConfig::mount_modes::MOUNTMODE_WRITE)
                    _fnDisks[i].access_mode = DISK_ACCESS_MODE_WRITE;
                else
                    _fnDisks[i].access_mode = DISK_ACCESS_MODE_READ;
            }
        }
    }
}

// Temporary(?) function while we move from old config storage to new
void iecFuji::_populate_config_from_slots()
{
    for (int i = 0; i < MAX_HOSTS; i++)
    {
        fujiHostType htype = _fnHosts[i].get_type();
        const char *hname = _fnHosts[i].get_hostname();

        if (hname[0] == '\0')
        {
            Config.clear_host(i);
        }
        else
        {
            Config.store_host(
                i, 
                hname,
                htype == HOSTTYPE_TNFS ? fnConfig::host_types::HOSTTYPE_TNFS : fnConfig::host_types::HOSTTYPE_SD
            );
        }
    }

    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        if (_fnDisks[i].host_slot >= MAX_HOSTS || _fnDisks[i].filename[0] == '\0')
            Config.clear_mount(i);
        else
            Config.store_mount(i, _fnDisks[i].host_slot, _fnDisks[i].filename,
                               _fnDisks[i].access_mode == DISK_ACCESS_MODE_WRITE ? fnConfig::mount_modes::MOUNTMODE_WRITE : fnConfig::mount_modes::MOUNTMODE_READ);
    }
}


// TODO: refactor this into BASIC/RAW, for now, leaving it with the response value and using "is_raw_command"
void iecFuji::mount_all()
{
  /*
    // Check at the end if no disks are in a slot and disable config
    bool nodisks = true;

    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        fujiDisk &disk = _fnDisks[i];
        fujiHost &host = _fnHosts[disk.host_slot];
        char flag[3] = {'r', 0, 0};

        if (disk.access_mode == DISK_ACCESS_MODE_WRITE)
            flag[1] = '+';

        if (disk.host_slot != INVALID_HOST_SLOT && strlen(disk.filename) > 0)
        {
            nodisks = false; // We have a disk in a slot

            if (!host.mount())
            {
                std::string slotno = std::to_string(i);
                response = "error: unable to mount slot " + slotno + "\r";
                set_fuji_iec_status(DEVICE_ERROR, response);
                return;
            }

            Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n",
                         disk.filename, disk.host_slot, flag, i + 1);

            disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), flag);

            if (disk.fileh == nullptr)
            {
                std::string slotno = std::to_string(i);
                response = "error: invalid file handle for slot " + slotno + "\r";
                set_fuji_iec_status(DEVICE_ERROR, response);
                return;
            }

            // We've gotten this far, so make sure our bootable CONFIG disk is disabled
            boot_config = false;

            // We need the file size for loading XEX files and for CASSETTE, so get that too
            disk.disk_size = host.file_size(disk.fileh);

            // Set the host slot for high score mode
            // TODO: Refactor along with mount disk image.
            disk.disk_dev.host = &host;

            // And now mount it
            disk.disk_type = disk.disk_dev.mount(disk.fileh, disk.filename, disk.disk_size);
        }
    }

    if (nodisks)
    {
        // No disks in a slot, disable config
        boot_config = false;
    }

    response = "ok";
    if (is_raw_command) {
        set_fuji_iec_status(0, "");
    } else {
        set_fuji_iec_status(0, response);
    }
  */
}



#endif /* BUILD_IEC */
