#pragma once

#if defined(BLE_PIN_CODE)

#include "../BaseSerialInterface.h"
#include <BTstackLib.h>

// NUS-style companion BLE interface for RP2040 / Pico W using the arduino-pico
// bundled BTstack. Presents the same Nordic UART Service + '>'/'<'+len framing
// as the ESP32/nRF52 implementations so existing companion apps work unchanged.
//
// SECURITY NOTE: BTstackLib dynamic characteristics are registered with
// ATT_SECURITY_NONE, so this link is NOT encrypted/paired (unlike the ESP32 and
// nRF52 builds which require MITM pairing). The pin_code argument is accepted for
// API compatibility but not currently enforced.

class SerialBLEInterface : public BaseSerialInterface {
  bool _isEnabled;
  volatile bool _isConnected;

  struct Frame {
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];
  };

  #ifndef FRAME_QUEUE_SIZE
  #define FRAME_QUEUE_SIZE  4
  #endif
  volatile int recv_queue_len;
  Frame recv_queue[FRAME_QUEUE_SIZE];
  volatile int send_queue_len;
  Frame send_queue[FRAME_QUEUE_SIZE];

  unsigned long _last_write;

  void clearBuffers();

  // callbacks routed from BTstack (run in the btstack background context)
  static void onConnect(BLEStatus status, BLEDevice* device);
  static void onDisconnect(BLEDevice* device);
  static int  onGattWrite(uint16_t att_handle, uint8_t* buffer, uint16_t buffer_size);
  static void onAttPacket(uint8_t packet_type, uint16_t channel, uint8_t* packet, uint16_t size);

  void handleWrite(uint16_t att_handle, const uint8_t* buffer, uint16_t buffer_size);
  void trySend();

public:
  SerialBLEInterface() {
    _isEnabled = false;
    _isConnected = false;
    recv_queue_len = 0;
    send_queue_len = 0;
    _last_write = 0;
  }

  /**
   * init the BLE interface.
   * @param prefix   a prefix for the device name
   * @param name  a name for the device (combined with prefix)
   * @param pin_code   the BLE security pin (accepted for compatibility, not enforced)
   */
  void begin(const char* prefix, char* name, uint32_t pin_code);

  // BaseSerialInterface methods
  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }

  bool isConnected() const override;
  bool isWriteBusy() const override;

  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;
};

#if BLE_DEBUG_LOGGING && ARDUINO
  #include <Arduino.h>
  #define BLE_DEBUG_PRINT(F, ...) Serial.printf("BLE: " F, ##__VA_ARGS__)
  #define BLE_DEBUG_PRINTLN(F, ...) Serial.printf("BLE: " F "\n", ##__VA_ARGS__)
#else
  #define BLE_DEBUG_PRINT(...) {}
  #define BLE_DEBUG_PRINTLN(...) {}
#endif

#endif  // BLE_PIN_CODE
