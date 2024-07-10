#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

typedef struct buffer_s buffer_t;
typedef struct buffer_list_s buffer_list_t;
typedef struct device_s device_t;
struct pollfd;

typedef struct device_mjpeg_sw_s {
  int quality;
} device_mjpeg_sw_t;

typedef struct buffer_list_mjpeg_sw_s {
  int fds[2];
} buffer_list_mjpeg_sw_t;

typedef struct buffer_mjpeg_sw_s {
  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;
  unsigned char *outbuffer;
  unsigned long outbuffer_size;
} buffer_mjpeg_sw_t;

int mjpeg_sw_device_open(device_t *dev);
void mjpeg_sw_device_close(device_t *dev);
int mjpeg_sw_device_set_option(device_t *dev, const char *key, const char *value);

int mjpeg_sw_buffer_open(buffer_t *buf);
void mjpeg_sw_buffer_close(buffer_t *buf);
int mjpeg_sw_buffer_enqueue(buffer_t *buf, const char *who);
int mjpeg_sw_buffer_list_dequeue(buffer_list_t *buf_list, buffer_t **bufp);
int mjpeg_sw_buffer_list_pollfd(buffer_list_t *buf_list, struct pollfd *pollfd, bool can_dequeue);

int mjpeg_sw_buffer_list_open(buffer_list_t *buf_list);
void mjpeg_sw_buffer_list_close(buffer_list_t *buf_list);
int mjpeg_sw_buffer_list_set_stream(buffer_list_t *buf_list, bool do_on);
