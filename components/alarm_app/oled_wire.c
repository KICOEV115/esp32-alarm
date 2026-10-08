#include "oled_wire.h"
#include "ui_frame.h"
#include <string.h>

int oled_wire_write_pages(oled_writer_t writer, void *context, const uint8_t *frame, uint8_t mask,
                          uint8_t offset)
{
    if (writer == NULL || frame == NULL || offset > 4) {
        return -1;
    }
    uint8_t data[UI_WIDTH + 1];
    data[0] = 0x40;
    for (unsigned page = 0; page < 8; ++page) {
        if ((mask & (1u << page)) == 0) {
            continue;
        }
        const uint8_t position[] = {
            0x00,
            (uint8_t)(0xb0 + page),
            (uint8_t)(offset & 0x0f),
            (uint8_t)(0x10 | (offset >> 4)),
        };
        int error = writer(context, position, sizeof(position));
        if (error != 0) {
            return error;
        }
        memcpy(data + 1, frame + page * UI_WIDTH, UI_WIDTH);
        error = writer(context, data, sizeof(data));
        if (error != 0) {
            return error;
        }
    }
    return 0;
}
