Hidden WIFI notification mechanism
----------------------------------

ESP32-S3 (and other ESP32 chips with WIFI onboard) have one hidden mechanism which Espressif uses for debugging their
RF part. Apart from debugging it can be used to create a LED status lights on your device: think of LEDs of a home wifi router.

WiFi stack on esp32 allows for setting a hook, which is called by WIFI driver upon certain events:

```
static int wifi_hook(int x, int y); // --> your hook function


extern void wifi_set_gpio_debug_cb(void *handler); // ESP-IDF hidden API to install your hook

```

Once installed, hooks get called quite often during normal WIFI operation: doing something slow there will slow whole WIFI stack down


Hook function receives two parameters - small integers, which identify type of event. Considering the function names and range of arguments
i'd suggest that `x` was once used as a GPIO number while second arg, `y` was an actual data sent to that GPIO.

We instead can connect LEDs to arbitrary gpios and turn them on/off creating a visible device status, which does not depend on printfs :)


Ok, now about `x` and `y` parameters which are passed to our hook function:



Low MAC: Frame Transmission Events
----------------------------------
X , Y -> DESCRIPTION

8   2 -> Called every time when software tries to transmit a frame using esp_wifi_internal_tx() or 
         esp_wifi_internal_tx_by_ref(), just before frame is sent to an output queue

         This event could be intepreted as [Software WIFI Output Event]

9   2 -> Called from lmacTxFrame, just before hal_mac_txq_enable

         This event could be intepreted as [Hardware WIFI Output Started]

10  4 -> Called by ppTask() right after lmacProcessCollisions_task() call

         This event could be intepreted as [ WIFI Collision Event ]

10  2
10  3 -> Called from lmacProcessTxComplete, before processing starts. Second argument can be 2 or 3,
         the meaning is not known. Probably denotes output interface: STA or AP, but it is just a guess

         This event could be intepreted as [Hardware Output Stopped]


Low MAC: Frame Reception
------------------------
X , Y -> DESCRIPTION

11,>1 -> Called from IndicateFrame, Memory buffer successfully allocated. This indicates that a valid
         WIFI frame was received and sent up to LMAC machinery.
         Also indicates that this frame is for us

         This event could be intepreted as [RX or Valid Frame Received]

12,?? -> Called from IndicateFrame before DiscardFrame, to indicate memory allocation problems
         This event could be intepreted as [Frame Dropped: No Mem, Critical]. This even only happens on Out-of-Memory situation, this is not an indication of a "Packed Dropped" in general


IEEE80211: High level events
----------------------------

13,2 -> Called from ieee80211_hostap_send_beacon_process() just before ic_tx_pkt(), to send out our preassembled beacon. Happens quite often.
        Only happens when WIFI is in AP or AP+STA mixed mode

        This event could be intepreted as  [SoftAP Beacon Output, AP is Alive]

        These 3 events only sent during WIFI scanning process:

15,0 -> Scan started
14,2 -> Scaning in process, periofic calls
15,1 -> Scan done



Considering the number and density of calls to this hook made from PowerManager module of the WIFI driver
I start to think that whole hook idea was invented just to debug PM module :)


Ok, sleep, wakeup, repeat: PowerManager


PowerManager: Waking up/Sleep procedures:
-----------------------------------------

17,0 -> after pm_wake_up()

        This event could be intepreted as  [WakeUP process initiated]

17,1 -> before pm_wake_done()
        This event could be intepreted as [WakeUP process done]

Now Espressif increases complexity a little:

A sequence consisting of 3 consecutive calls.
It is guaranteed that this sequence will not be split by another concurrent sequence: these calls are all made
from the WIFI task context, which serializes access to WIFI API. (One big global lock for everything.
Reminds me BSD-derived stack on RTEMS circa 2005)

Seuence#1, three callse to our hook with these arguments:

3,1
4,1
5,0 -> at the end of pm_start(), at the pm_disconnected_start(), at the pm_disconnected_wake()

       This event could be intepreted as [Some WakeUP process, unclear]


3,0
4,1
5,1 -> pm_disconnected_sleep, at the pm_disconnected_stop()

       This event could be intepreted as [Some Sleep process, unclear]




PowerManager: Internal FSM state changes monitoring:
----------------------------------------------------

A sequence consisting of two consecutive calls; executed when PM module changes state from 
`old_state` to `new_state`. State is a number in [0..5] range, the "minus" below means substraction, which
results in X argument being in range [0..5] as well;

It is guaranteed that this sequence will not be split by another concurrent sequence

5 - old_state, 1
5 - new_state, 0

This event could be intepreted as [PM is Working]


PowerManager: Beacon-related activity:
-------------------------------------

0 , 2 -> Target Beacon Transmission Time.
       This event could be intepreted as [PowerManager: Hardware Wakeup for sending/receiving beacons]

1 , 2 -> This event could be intepreted as [PowerManager: Beacon Processing Start]
2 , 2 -> This event could be intepreted as [PowerManager: No beacon at expected time]


PowerManager: Not Fully Understood
----------------------------------

6, >1 -> This event could be intepreted as [Have no idea yet]
16, 2 -> Happens together with a documented WIFI_EVENT 0x16 (See ESP-IDF docs, WIFI section)


PowerManager: Coexist-related (RF arbiter/scheduler for BT/BLE/WIFI)
--------------------------------------------------------------------
These can be used to track down Coexist errors

7 , 0 -> An RF arbiter which gives timeslots to BT and WIFI is alive. Only relevant when both BT and WIFI are active.

      This event could be intepreted as [Coexistence Scheduler Mechanism tick]

7 , 1 -> Something related to time slicing procedure, which again can be used as an indication of working coexist_schm



This is the minimal Arduino sketch which shows how to attach the hook:

```

#include <Arduino.h>

extern "C" {
  void wifi_set_gpio_debug_cb(void *handler);
};

static int wifi_hook(int x, int y) {
  esp_rom_printf("\r\nWIFI-GPIO-DEBUG: X=%d, Y=%d\r\n",x,y);
  return 0;
}


void setup() {
  
  Serial.begin(115200);

  wifi_set_gpio_debug_cb((void *)wifi_hook); 

  // Initialize wifi here or your hook will never be called :)
}


void loop() {

  delay(1000);
}
```
