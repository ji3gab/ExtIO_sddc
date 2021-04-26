#include "FX3handler.h"
#include "usb_device.h"
#include "usb_device_internals.h"
#include "logging.h"
#include <string.h>

#define USB_READ_CONCURRENT  4
#define BULK_XFER_TIMEOUT 5000

extern "C" {
 
/* internal functions */
static void LIBUSB_CALL streaming_read_async_callback(struct libusb_transfer *transfer)
{
  uint8_t *buffer = (uint8_t *) transfer->user_data;

  switch (transfer->status) {
    case LIBUSB_TRANSFER_COMPLETED:
      /* success!!! */
      //fprintf(stderr, "act_len: %d\n", transfer->actual_length);
      memcpy(buffer, transfer->buffer,
             (transfer->actual_length > 131072) ? 131072 : transfer->actual_length);
      return;
    case LIBUSB_TRANSFER_CANCELLED:
      /* librtlsdr does also ignore LIBUSB_TRANSFER_CANCELLED */
      return;
    case LIBUSB_TRANSFER_ERROR:
    case LIBUSB_TRANSFER_TIMED_OUT:
    case LIBUSB_TRANSFER_STALL:
    case LIBUSB_TRANSFER_NO_DEVICE:
    case LIBUSB_TRANSFER_OVERFLOW:
      log_usb_error(transfer->status, __func__, __FILE__, __LINE__);
      break;
  }
  return;
}

}



fx3class* CreateUsbHandler()
{
	return new fx3handler();
}

fx3handler::fx3handler()
{
}

fx3handler::~fx3handler()
{
}

bool fx3handler::Open(uint8_t* fw_data, uint32_t fw_size)
{
    dev = usb_device_open(0, (const char*)fw_data, fw_size);

    return dev != nullptr;
}

bool fx3handler::Control(FX3Command command, uint8_t data)
{
    return usb_device_control(this->dev, command, 0, 0, (uint8_t *) &data, sizeof(data), 0) == 0;
}

bool fx3handler::Control(FX3Command command, uint32_t data)
{
    return usb_device_control(this->dev, command, 0, 0, (uint8_t *) &data, sizeof(data), 0) == 0;
}

bool fx3handler::Control(FX3Command command, uint64_t data)
{
    return usb_device_control(this->dev, command, 0, 0, (uint8_t *) &data, sizeof(data), 0) == 0;
}

bool fx3handler::SetArgument(uint16_t index, uint16_t value)
{
    uint8_t data = 0;
    return usb_device_control(this->dev, SETARGFX3, value, index, (uint8_t *) &data, sizeof(data), 0) == 0;
}

bool fx3handler::GetHardwareInfo(uint32_t* data)
{
    return usb_device_control(this->dev, TESTFX3, 0, 0, (uint8_t *) data, sizeof(*data), 1) == 0;
}

bool fx3handler::BeginDataXfer(uint8_t *buffer, long transferSize, void **context)
{
    struct libusb_transfer **transfer = (struct libusb_transfer **)context;
    
    if (this->dev->bulk_in_endpoint_address == 0) {
      return false;
    }

    if (*transfer == nullptr) {
      /* allocate a frame for a zerocopy USB bulk transfer */
      //fprintf(stderr, "allocating libusb_mem\n");
      uint8_t *frame = libusb_dev_mem_alloc(this->dev->dev_handle, transferSize);
      if (frame == nullptr) {
        log_error("libusb_dev_mem_alloc() failed", __func__, __FILE__, __LINE__);
        return false;
      }

      *transfer = libusb_alloc_transfer(0);
      libusb_fill_bulk_transfer(*transfer, this->dev->dev_handle,
                                this->dev->bulk_in_endpoint_address,
                                frame, transferSize, streaming_read_async_callback,
                                buffer, BULK_XFER_TIMEOUT);
    } else {
      (*transfer)->user_data = buffer;
    }
    // atomic_init(&this->active_transfers, 0); ??
    /* submit a transfer */
    // atomic_init(&this->active_transfers, 0);
    int ret = libusb_submit_transfer(*transfer);
    if (ret < 0) {
      log_usb_error(ret, __func__, __FILE__, __LINE__);
      //this->status = STREAMING_STATUS_FAILED;
      return false;
    }
    // atomic_fetch_add(&this->active_transfers, 1);

   return true;
}

bool fx3handler::FinishDataXfer(void **context)
{
    return usb_device_handle_events(this->dev) == 0;
}

void fx3handler::CleanupDataXfer(void **context)
{
    struct libusb_transfer **transfer = (struct libusb_transfer **)context;
    
    int ret = libusb_cancel_transfer(*transfer);
    if (ret < 0) {
      if (ret == LIBUSB_ERROR_NOT_FOUND) {
	// continue;
      }
      log_usb_error(ret, __func__, __FILE__, __LINE__);
      // this->status = STREAMING_STATUS_FAILED;
    }

    /* flush all the events */
    struct timeval noblock = { 0, 0 };
    ret = libusb_handle_events_timeout_completed(this->dev->context, &noblock, 0);
    if (ret < 0) {
      log_usb_error(ret, __func__, __FILE__, __LINE__);
      // this->status = STREAMING_STATUS_FAILED;
    }
    return;
}
