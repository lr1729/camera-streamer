#include "mjpeg_sw.h"
#include "device/device.h"
#include "util/opts/log.h"
#include "util/opts/fourcc.h"

#include <jpeglib.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

device_hw_t mjpeg_sw_device_hw = {
  .device_open = mjpeg_sw_device_open,
  .device_close = mjpeg_sw_device_close,
  .device_set_option = mjpeg_sw_device_set_option,

  .buffer_open = mjpeg_sw_buffer_open,
  .buffer_close = mjpeg_sw_buffer_close,
  .buffer_enqueue = mjpeg_sw_buffer_enqueue,

  .buffer_list_dequeue = mjpeg_sw_buffer_list_dequeue,
  .buffer_list_pollfd = mjpeg_sw_buffer_list_pollfd,
  .buffer_list_open = mjpeg_sw_buffer_list_open,
  .buffer_list_close = mjpeg_sw_buffer_list_close,
  .buffer_list_set_stream = mjpeg_sw_buffer_list_set_stream
};

device_t *device_mjpeg_sw_open(const char *name, const char *path) {
  return device_open(name, path, &mjpeg_sw_device_hw);
}

int mjpeg_sw_device_open(device_t *dev) {
  dev->mjpeg_sw = calloc(1, sizeof(device_mjpeg_sw_t));
  dev->mjpeg_sw->quality = 80; // Default quality
  return 0;
}

void mjpeg_sw_device_close(device_t *dev) {
  free(dev->mjpeg_sw);
}

int mjpeg_sw_device_set_option(device_t *dev, const char *key, const char *value) {
  if (!strcmp(key, "compression_quality")) {
    dev->mjpeg_sw->quality = atoi(value);
    LOG_INFO(dev, "Set compression quality to %d", dev->mjpeg_sw->quality);
    return 1;
  }
  return 0;
}

int mjpeg_sw_buffer_open(buffer_t *buf) {
  buf->mjpeg_sw = calloc(1, sizeof(buffer_mjpeg_sw_t));
  buf->mjpeg_sw->outbuffer_size = 1024 * 1024; // Initial output buffer size
  buf->mjpeg_sw->outbuffer = malloc(buf->mjpeg_sw->outbuffer_size);
  if (!buf->mjpeg_sw->outbuffer) {
    LOG_ERROR(buf, "Failed to allocate output buffer");
  }
  return 0;

error:
  return -1;
}

void mjpeg_sw_buffer_close(buffer_t *buf) {
  if (buf->mjpeg_sw) {
    jpeg_destroy_compress(&buf->mjpeg_sw->cinfo);
    free(buf->mjpeg_sw->outbuffer);
  }
  free(buf->mjpeg_sw);
}

int mjpeg_sw_buffer_enqueue(buffer_t *buf, const char *who) {
  buffer_list_t *buf_list = buf->buf_list;
  device_t *dev = buf_list->dev;
  buffer_t *src_buf = buf->dma_source;

  if (!src_buf) {
    LOG_ERROR(buf, "No source buffer for encoding");
  }

  // libjpeg-turbo setup
  buf->mjpeg_sw->cinfo.err = jpeg_std_error(&buf->mjpeg_sw->jerr);
  jpeg_create_compress(&buf->mjpeg_sw->cinfo);
  jpeg_mem_dest(&buf->mjpeg_sw->cinfo, &buf->mjpeg_sw->outbuffer, &buf->mjpeg_sw->outbuffer_size);

  buf->mjpeg_sw->cinfo.image_width = src_buf->buf_list->fmt.width;
  buf->mjpeg_sw->cinfo.image_height = src_buf->buf_list->fmt.height;
  buf->mjpeg_sw->cinfo.input_components = 3; // Assuming RGB input
  buf->mjpeg_sw->cinfo.in_color_space = JCS_RGB;

  jpeg_set_defaults(&buf->mjpeg_sw->cinfo);
  jpeg_set_quality(&buf->mjpeg_sw->cinfo, dev->mjpeg_sw->quality, TRUE);

  jpeg_start_compress(&buf->mjpeg_sw->cinfo, TRUE);

  // Encode each row
  JSAMPROW row_pointer[1];
  while (buf->mjpeg_sw->cinfo.next_scanline < buf->mjpeg_sw->cinfo.image_height) {
    row_pointer[0] = (JSAMPROW)((char*)src_buf->start + buf->mjpeg_sw->cinfo.next_scanline * src_buf->buf_list->fmt.bytesperline);
    jpeg_write_scanlines(&buf->mjpeg_sw->cinfo, row_pointer, 1);
  }

  jpeg_finish_compress(&buf->mjpeg_sw->cinfo);

  buf->used = buf->mjpeg_sw->cinfo.dest->free_in_buffer;
  unsigned index = buf->index;
  if (write(buf_list->mjpeg_sw->fds[1], &index, sizeof(index)) != sizeof(index)) {
    return -1;
  }
  return 0;

error:
  return -1;
}

int mjpeg_sw_buffer_list_dequeue(buffer_list_t *buf_list, buffer_t **bufp) {
  unsigned index = 0;
  int n = read(buf_list->mjpeg_sw->fds[0], &index, sizeof(index));
  if (n != sizeof(index)) {
    LOG_INFO(buf_list, "Received invalid result from `read`: %d", n);
    return -1;
  }

  if (index >= (unsigned)buf_list->nbufs) {
    LOG_INFO(buf_list, "Received invalid index from `read`: %d >= %d", index, buf_list->nbufs);
    return -1;
  }

  *bufp = buf_list->bufs[index];
  return 0;
}

int mjpeg_sw_buffer_list_pollfd(buffer_list_t *buf_list, struct pollfd *pollfd, bool can_dequeue) {
  int count_enqueued = buffer_list_count_enqueued(buf_list);
  pollfd->fd = buf_list->mjpeg_sw->fds[0]; // write end
  pollfd->events = POLLHUP;
  if (can_dequeue && count_enqueued > 0) {
    pollfd->events |= POLLIN;
  }
  pollfd->revents = 0;
  return 0;
}

int mjpeg_sw_buffer_list_open(buffer_list_t *buf_list) {
  if (!buf_list->do_capture) {
    LOG_ERROR(buf_list, "Only capture mode supported");
  }

  buf_list->mjpeg_sw = calloc(1, sizeof(buffer_list_mjpeg_sw_t));
  if (pipe2(buf_list->mjpeg_sw->fds, O_DIRECT|O_CLOEXEC) < 0) {
    LOG_INFO(buf_list, "Cannot open `pipe2`.");
    return -1;
  }

  return buf_list->fmt.nbufs;

error:
  return -1;
}

void mjpeg_sw_buffer_list_close(buffer_list_t *buf_list) {
  if (buf_list->mjpeg_sw) {
    close(buf_list->mjpeg_sw->fds[0]);
    close(buf_list->mjpeg_sw->fds[1]);
  }
  free(buf_list->mjpeg_sw);
}

int mjpeg_sw_buffer_list_set_stream(buffer_list_t *buf_list, bool do_on) {
  return 0;
}
