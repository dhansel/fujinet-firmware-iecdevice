#ifdef BUILD_IEC

#include "iec.h"
#include "../../device/iec/fuji.h"

#include <cstring>
#include <memory>

#include "../../include/debug.h"
#include "../../include/pinmap.h"
#include "../../hardware/led.h"

systemBus IEC;

systemBus::systemBus() : IECBusHandler(PIN_IEC_ATN, PIN_IEC_CLK_OUT, PIN_IEC_DATA_OUT, PIN_IEC_RESET)
{
}


void systemBus::setup()
{
  Debug_printf("IEC systemBus::setup()\r\n");
  begin();
}


void systemBus::service()
{
  task();
  
  bool error = false, active = false;
  for(int i = 0; i < MAX_DISK_DEVICES; i++)
    {
      iecDrive *d = &(theFuji.get_disks(i)->disk_dev);
      error  |= d->hasError();
      active |= d->getNumOpenChannels()>0;
    }

  if( error )
    {
      static bool     flashState = false;
      static uint32_t prevFlash  = 0;
      if( (fnSystem.millis()-prevFlash) > 250 )
        {
          flashState = !flashState;
          fnLedManager.set(eLed::LED_BUS, flashState);
          prevFlash = fnSystem.millis();
        }
    }
  else
    fnLedManager.set(eLed::LED_BUS, active);
}


void systemBus::shutdown()
{
}


#endif /* BUILD_IEC */
