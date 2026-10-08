#ifndef OLED_WIRE_H
#define OLED_WIRE_H
#include <stddef.h>
#include <stdint.h>
typedef int (*oled_writer_t)(void *context, const uint8_t *bytes, size_t size);
/* Return 0 on success, writer's error on transfer failure, -1 for bad input.
 * One address command and one full 128-byte data write per selected page. */
int oled_wire_write_pages(oled_writer_t writer, void *context, const uint8_t *frame, uint8_t mask,
                          uint8_t offset);
#endif
