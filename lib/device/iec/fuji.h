#ifndef FUJI_H
#define FUJI_H

#include <cstdint>
#include <cstring>

#include "bus.h"
#include "network.h"
#include "cassette.h"
#include "fnWiFi.h"

#include "../fuji/fujiHost.h"
#include "../fuji/fujiDisk.h"
#include "../fuji/fujiCmd.h"

#include "hash.h"

#define MAX_HOSTS 8
#define MAX_DISK_DEVICES 8
#define MAX_NETWORK_DEVICES 4

class iecFuji : public IECFileDevice
{
 public:
    iecFuji();

    void setup(systemBus *bus);

    int get_disk_id(int drive_slot);
    std::string get_host_prefix(int host_slot);

    fujiHost *get_hosts(int i) { return &_fnHosts[i]; }
    fujiDisk *get_disks(int i) { return &_fnDisks[i]; }

    void _populate_slots_from_config();
    void _populate_config_from_slots();

    // 0xD7 - why is this public?
    void mount_all();

    bool boot_config = true;

 private:
    // called when the bus master reads from channel 15 and the status
    // buffer is currently empty. this should populate buffer with an appropriate 
    // status message bufferSize is the maximum allowed length of the message
    virtual void getStatus(char *buffer, byte bufferSize);

    // called when the bus master sends data (i.e. a command) to channel 15
    // command is a 0-terminated string representing the command to execute
    // commandLen contains the full length of the received command (useful if
    // the command itself may contain zeros)
    virtual void execute(const char *command, byte cmdLen);

    // called on falling edge of RESET line
    virtual void reset();

    void set_fuji_iec_status(int8_t error, const std::string msg);

    fujiHost _fnHosts[MAX_HOSTS];
    fujiDisk _fnDisks[MAX_DISK_DEVICES];
};

extern iecFuji theFuji;

#endif // FUJI_H
