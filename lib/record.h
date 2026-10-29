#ifndef RECORD_H
#define RECORD_H

#include <stddef.h>
#include <stdint.h>
#include "sample.h"

#define CSV_HEADER "timestamp,bme_temp_c,humidity_pct,pressure_hpa,lm35_temp_c,flags\r\n"

/* One CSV row, e.g. "2026-10-29T14:03:05Z,24.37,61.20,1008.42,24.90,0\r\n". Returns length or -1. */
int csv_format(const sample_t *s, char *buf, size_t size);

/* Log file name for the sample's UTC day, e.g. "20261029.CSV". */
void csv_filename(uint32_t epoch, char out[13]);

/* UART frames to the ESP32, NMEA style: $BODY*HH\r\n where HH is the XOR of BODY.
 *   STM32 -> ESP32: $LOG,seq,epoch,bme_c*100,rh*100,pa,lm35_c*100,flags*HH
 *   ESP32 -> STM32: $TIME,epoch*HH    sets the RTC
 *                   $ACK,seq*HH       confirms a LOG frame */
int frame_log(const sample_t *s, char *buf, size_t size);

typedef enum { FRAME_NONE, FRAME_TIME, FRAME_ACK, FRAME_BAD } frame_kind_t;
frame_kind_t frame_parse(const char *line, uint32_t *value);

uint8_t frame_checksum(const char *body, size_t len);

#endif
