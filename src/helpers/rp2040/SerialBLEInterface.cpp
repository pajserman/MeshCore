#if defined(BLE_PIN_CODE)

#include "SerialBLEInterface.h"

// The RP2040 BLE link is UNENCRYPTED and BLE_PIN_CODE is NOT enforced (see header).
#warning "RP2040 BLE companion: link is unencrypted and BLE_PIN_CODE is not enforced"

extern "C" {
  #include "ble/att_server.h"
  #include "ble/att_db_util.h"
  #include "btstack_util.h"
}

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#define BLE_WRITE_MIN_INTERVAL 60

// BTstack callbacks are C-style and global; route them to the single instance.
static SerialBLEInterface* _instance = nullptr;

static uint16_t _rx_value_handle = 0;
static uint16_t _tx_value_handle = 0;
static hci_con_handle_t _con_handle = HCI_CON_HANDLE_INVALID;
static volatile bool _can_send_pending = false;

// Concurrency model (arduino-pico, default PICO_CYW43_ARCH_THREADSAFE_BACKGROUND,
// no FreeRTOS): BTstack callbacks (onConnect/onDisconnect/onGattWrite/onAttPacket)
// run from the cyw43 async_context serviced by a low-priority IRQ on core0. They are
// therefore atomic with respect to loop(), and loop() cannot preempt them. The
// main-loop side masks that IRQ with noInterrupts()/interrupts() around shared-queue
// access; the callback side needs no guard because loop() can never run mid-callback.
void SerialBLEInterface::begin(const char* prefix, char* name, uint32_t pin_code) {
  (void) pin_code;  // not enforced on this platform (see header security note)
  _instance = this;

  static char device_name[32];
  snprintf(device_name, sizeof(device_name), "%s%s", prefix, name);

  // build the Nordic UART Service before bringing up the stack
  static UUID service_uuid(SERVICE_UUID);
  static UUID rx_uuid(CHARACTERISTIC_UUID_RX);
  static UUID tx_uuid(CHARACTERISTIC_UUID_TX);

  BTstack.addGATTService(&service_uuid);
  _rx_value_handle = BTstack.addGATTCharacteristicDynamic(
    &rx_uuid, ATT_PROPERTY_WRITE | ATT_PROPERTY_WRITE_WITHOUT_RESPONSE, 0);
  _tx_value_handle = BTstack.addGATTCharacteristicDynamic(
    &tx_uuid, ATT_PROPERTY_NOTIFY, 0);

  BTstack.setBLEDeviceConnectedCallback(onConnect);
  BTstack.setBLEDeviceDisconnectedCallback(onDisconnect);
  BTstack.setGATTCharacteristicWrite(onGattWrite);

  BTstack.setup(device_name);

  // receive ATT_EVENT_CAN_SEND_NOW so notifications are paced by the stack
  att_server_register_packet_handler(&SerialBLEInterface::onAttPacket);

  // advertise the 128-bit service UUID (little-endian) so service scanners match,
  // and put the full local name in the scan response for name-based scanners
  static uint8_t adv_data[31];
  int pos = 0;
  adv_data[pos++] = 0x02;
  adv_data[pos++] = 0x01;
  adv_data[pos++] = 0x06;
  adv_data[pos++] = 0x11;
  adv_data[pos++] = 0x07;
  reverse_128(service_uuid.getUuid(), &adv_data[pos]);
  pos += 16;
  BTstack.setAdvData(pos, adv_data);

  static uint8_t scan_data[31];
  int slen = strlen(device_name);
  if (slen > 29) slen = 29;
  scan_data[0] = slen + 1;
  scan_data[1] = 0x09;  // complete local name
  memcpy(&scan_data[2], device_name, slen);
  BTstack.setScanData(slen + 2, scan_data);

  BLE_DEBUG_PRINTLN("WARNING: link is UNENCRYPTED, BLE_PIN_CODE not enforced");
  BTstack.startAdvertising();
}

void SerialBLEInterface::clearBuffers() {
  noInterrupts();
  recv_queue_len = 0;
  send_queue_len = 0;
  interrupts();
}

void SerialBLEInterface::enable() {
  if (_isEnabled) return;
  _isEnabled = true;
  clearBuffers();
  BTstack.startAdvertising();
}

void SerialBLEInterface::disable() {
  _isEnabled = false;
  BTstack.stopAdvertising();
}

bool SerialBLEInterface::isConnected() const {
  return _isConnected;
}

bool SerialBLEInterface::isWriteBusy() const {
  return millis() < _last_write + BLE_WRITE_MIN_INTERVAL;
}

size_t SerialBLEInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("writeFrame(), frame too big, len=%d", (int) len);
    return 0;
  }

  if (_isConnected && len > 0) {
    noInterrupts();
    if (send_queue_len >= FRAME_QUEUE_SIZE) {
      interrupts();
      BLE_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
      return 0;
    }
    send_queue[send_queue_len].len = len;
    memcpy(send_queue[send_queue_len].buf, src, len);
    send_queue_len++;
    interrupts();
    return len;
  }
  return 0;
}

void SerialBLEInterface::trySend() {
  if (!_isConnected || _con_handle == HCI_CON_HANDLE_INVALID) return;
  if (millis() < _last_write + BLE_WRITE_MIN_INTERVAL) return;

  noInterrupts();
  bool have_data = send_queue_len > 0;
  bool pending = _can_send_pending;
  if (have_data && !pending) {
    _can_send_pending = true;
    att_server_request_can_send_now_event(_con_handle);
  }
  interrupts();
}

size_t SerialBLEInterface::checkRecvFrame(uint8_t dest[]) {
  trySend();

  size_t out = 0;
  noInterrupts();
  if (recv_queue_len > 0) {
    out = recv_queue[0].len;
    memcpy(dest, recv_queue[0].buf, out);
    recv_queue_len--;
    for (int i = 0; i < recv_queue_len; i++) {
      recv_queue[i] = recv_queue[i + 1];
    }
  }
  interrupts();
  return out;
}

// ---------- BTstack callbacks (run in the btstack background context)

void SerialBLEInterface::onConnect(BLEStatus status, BLEDevice* device) {
  if (status != BLE_STATUS_OK || device == nullptr) return;
  _con_handle = device->getHandle();
  _can_send_pending = false;
  if (_instance) _instance->_isConnected = true;
  BLE_DEBUG_PRINTLN("onConnect(), handle=0x%04x", _con_handle);
}

void SerialBLEInterface::onDisconnect(BLEDevice* device) {
  (void) device;
  _con_handle = HCI_CON_HANDLE_INVALID;
  _can_send_pending = false;
  if (_instance) {
    _instance->_isConnected = false;
    _instance->recv_queue_len = 0;
    _instance->send_queue_len = 0;
  }
  BLE_DEBUG_PRINTLN("onDisconnect()");
}

int SerialBLEInterface::onGattWrite(uint16_t att_handle, uint8_t* buffer, uint16_t buffer_size) {
  if (_instance) _instance->handleWrite(att_handle, buffer, buffer_size);
  return 0;
}

void SerialBLEInterface::handleWrite(uint16_t att_handle, const uint8_t* buffer, uint16_t buffer_size) {
  if (att_handle != _rx_value_handle) return;
  if (buffer_size == 0 || buffer_size > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("handleWrite(), bad len=%d", (int) buffer_size);
    return;
  }
  if (recv_queue_len >= FRAME_QUEUE_SIZE) {
    BLE_DEBUG_PRINTLN("handleWrite(), recv_queue full!");
    return;
  }
  recv_queue[recv_queue_len].len = buffer_size;
  memcpy(recv_queue[recv_queue_len].buf, buffer, buffer_size);
  recv_queue_len++;
}

void SerialBLEInterface::onAttPacket(uint8_t packet_type, uint16_t channel, uint8_t* packet, uint16_t size) {
  (void) channel;
  (void) size;
  if (packet_type != HCI_EVENT_PACKET) return;
  if (packet[0] != ATT_EVENT_CAN_SEND_NOW) return;
  if (_instance == nullptr || _con_handle == HCI_CON_HANDLE_INVALID) return;

  _can_send_pending = false;
  if (_instance->send_queue_len > 0) {
    att_server_notify(_con_handle, _tx_value_handle,
                      _instance->send_queue[0].buf, _instance->send_queue[0].len);
    _instance->_last_write = millis();
    _instance->send_queue_len--;
    for (int i = 0; i < _instance->send_queue_len; i++) {
      _instance->send_queue[i] = _instance->send_queue[i + 1];
    }
    if (_instance->send_queue_len > 0) {
      _can_send_pending = true;
      att_server_request_can_send_now_event(_con_handle);
    }
  }
}

#endif  // BLE_PIN_CODE
