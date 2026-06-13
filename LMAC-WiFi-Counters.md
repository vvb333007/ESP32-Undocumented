# ESP32-S3 Wi-Fi Error / Statistics Counters (LMAC)

ESP32-S3 contains a large number of internal Wi-Fi counters. Some of them can be quite useful when debugging connectivity issues.

In this post we'll look at both software counters (available on all ESP32 chips with Wi-Fi support) and hardware counters (using ESP32-S3 as an example).

I already wrote about HMAC counters somewhere on this forum. If you search for it, you should be able to find the post. This time we'll move one level down and look at LMAC.

## Software LMAC Wi-Fi Counters

The counters are stored in the global variable `g_lmac_cnt` (C linkage).

The underlying structure looks like this:

```c
struct lmac_cnt_t {
    struct wifi_mem_stat_t pool_stats[11]; // Memory stats. Element 0 is unused
    struct wifi_hwtxq_stat_t lmac_tx[5];   // TX stats. Five hardware TX queues

    uint32_t ampdu;                        // Total received AMPDU
    uint32_t mpdu;                         // Total received frames
    uint32_t mgmt;                         // Total received management frames
    uint32_t ctrl;                         // Total received control frames
    uint16_t frag;                         // Total received frames with FRAG bit set
};

// Wi-Fi driver memory pool statistics.
//
// The driver uses ten memory pools to allocate packet buffer structures.
// Think of them as a much simpler version of FreeBSD mbufs.
//
struct wifi_mem_stat_t {
    uint32_t total_alloc;  // Successful allocations
    uint32_t total_free;   // Successful frees

    uint16_t fail_fc;
    uint16_t fail_oom;     // Out-of-memory events
};

// Hardware TX queues.
//
// From observations: in pure STA or pure SoftAP mode only TXQ#0 is used.
// In mixed STA+SoftAP mode queues #0 and #2 are active. It is good idea to check ALL queues.
//
struct wifi_hwtxq_stat_t {

    uint32_t tx_all; // Total transmitted packets (software counter)

    uint8_t lrc;
    uint8_t src;
    uint8_t age;
    uint8_t to;
};
```

To access these counters simply declare:

```c
extern struct lmac_cnt_t g_lmac_cnt;
```

and read the fields directly.

The counters above are available on all ESP32 chips that include Wi-Fi support.

## Hardware LMAC Counters (ESP32-S3 only)

> Note: Counter meanings were determined experimentally and are not confirmed by Espressif. Some descriptions are educated guesses.

## General RX Statistics

| Address      | Description             | Notes                                                                                                                                               |
| ------------ | ----------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- |
| `0x600332f8` | Useful received packets | Packets that were actually intended for us. Without active traffic this counter increases very slowly, mostly due to management and control frames. |
| `0x600332bc` | Failed receive attempts | Noise, packets for other stations, and similar events. Normally this counter increases fairly quickly.                                              |
| `0x60033d58` | Wi-Fi hardware events   | Includes received packets, interrupts, and other internal events. Normally increases quickly.                                                       |

## RTS / CTS / ACK Statistics

These counters reflect operation of the medium access control logic.

Under normal conditions they grow very slowly. Rapid growth may indicate RF congestion. CTS is especially interesting: whenever we hear someone else's CTS frame we stay quiet. If the CTS counter grows too quickly, we may spend most of our time waiting.

| Address      | Description    |
| ------------ | -------------- |
| `0x600332ec` | ACK interrupts |
| `0x600332f0` | RTS interrupts |
| `0x600332f4` | CTS interrupts |

## RF Front-End and Demodulator Errors

| Address      | Description   | Notes                                                                                                                                    |
| ------------ | ------------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| `0x600332c0` | CCK errors    | Errors while receiving CCK-modulated frames.                                                                                             |
| `0x600332c4` | OFDM errors   | Usually very close to the CCK error count. The receiver may be attempting multiple decoding paths before deciding the signal is garbage. |
| `0x600332c8` | AGC RX errors | Automatic Gain Control errors. Signal level was either too weak or too strong.                                                           |

## Post-Demodulation RX Errors

At this stage the signal has already been successfully converted into digital data.

| Address      | Description     | Notes                                                            |
| ------------ | --------------- | ---------------------------------------------------------------- |
| `0x600332d0` | RX Abort        | Reception was aborted.                                           |
| `0x600332d4` | RX FCS          | Frame Check Sequence error. Usually indicates a corrupted frame. |
| `0x600332e0` | Other RX errors | Miscellaneous receive errors. Normally grows slowly.             |

## Cryptography

| Address      | Description |                                |
| ------------ | ----------- | ------------------------------ |
| `0x600332e4` | TKIP errors | Errors during TKIP processing. |

## Internal Errors and Diagnostics

These counters are normally zero or close to zero.

If they increase steadily, something is probably wrong inside the Wi-Fi subsystem.

| Address      | Description                   | Notes                                                                                                                                       |
| ------------ | ----------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| `0x600332cc` | RXSF errors                   | Exact meaning unknown. Possibly "RF Short Frame".                                                                                           |
| `0x600332d8` | FIFO Full3                    | Internal FIFO overflow. Normally zero.                                                                                                      |
| `0x600332dc` | FIFO Full2                    | Internal FIFO overflow. Normally zero.                                                                                                      |
| `0x60033308` | FIFO Full1                    | Internal FIFO overflow. Normally zero.                                                                                                      |
| `0x60033304` | HOP errors                    | Meaning unknown. Normally zero.                                                                                                             |
| `0x60033d3c` | HW Collision Avoidance stalls | The transmitter was blocked by hardware collision avoidance logic. For example, the channel is continuously occupied by foreign CTS frames. |
| `0x60033d40` | Panic                         | Internal Wi-Fi hardware error. Exact meaning unknown, but it definitely does not indicate anything good.                                    |

```
