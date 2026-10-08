#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "oled_wire.h"
#include "ui_frame.h"

typedef struct {
    uint8_t ram[8][132];
    unsigned page, column, calls, fail_call;
    bool partial_failure;
} mock_t;

/* Emulate the relevant controller page/column addressing, not the renderer. */
static int write_mock(void *context, const uint8_t *bytes, size_t size)
{
    mock_t *mock = context;
    ++mock->calls;
    bool fail = mock->calls == mock->fail_call;
    if (fail && !mock->partial_failure) {
        return 777;
    }
    if (bytes[0] == 0x00) {
        assert(size == 4);
        assert(bytes[1] >= 0xb0 && bytes[1] <= 0xb7);
        assert((bytes[2] & 0xf0) == 0 && (bytes[3] & 0xf0) == 0x10);
        mock->page = bytes[1] - 0xb0;
        mock->column = (bytes[3] & 0x0f) * 16 + bytes[2];
    } else {
        assert(bytes[0] == 0x40 && size == 129);
        size_t amount = fail ? 32 : size - 1;
        for (size_t i = 1; i <= amount; ++i) {
            assert(mock->page < 8 && mock->column < 132);
            mock->ram[mock->page][mock->column++] = bytes[i];
        }
    }
    return fail ? 777 : 0;
}

int main(void)
{
    uint8_t frame[UI_FRAME_BYTES];
    for (unsigned i = 0; i < sizeof(frame); ++i) {
        frame[i] = (uint8_t)(i * 31 + i / 128);
    }
    for (unsigned offset = 0; offset <= 2; offset += 2) {
        mock_t mock = {0};
        assert(oled_wire_write_pages(write_mock, &mock, frame, 0xff, offset) == 0);
        assert(mock.calls == 16);
        for (unsigned page = 0; page < 8; ++page) {
            assert(memcmp(mock.ram[page] + offset, frame + page * UI_WIDTH, UI_WIDTH) == 0);
        }
        mock.calls = 0;
        assert(oled_wire_write_pages(write_mock, &mock, frame, 0, offset) == 0);
        assert(mock.calls == 0);
        assert(oled_wire_write_pages(write_mock, &mock, frame, 0x80, offset) == 0);
        assert(mock.calls == 2);
    }
    mock_t mock = {.fail_call = 2, .partial_failure = true};
    assert(oled_wire_write_pages(write_mock, &mock, frame, 0xff, 0) == 777);
    assert(mock.calls == 2);
    mock.fail_call = 0;
    assert(oled_wire_write_pages(write_mock, &mock, frame, 0xff, 0) == 0);
    for (unsigned page = 0; page < 8; ++page) {
        assert(memcmp(mock.ram[page], frame + page * UI_WIDTH, UI_WIDTH) == 0);
    }
    assert(oled_wire_write_pages(NULL, &mock, frame, 1, 0) == -1);
    assert(oled_wire_write_pages(write_mock, &mock, NULL, 1, 0) == -1);
    assert(oled_wire_write_pages(write_mock, &mock, frame, 1, 5) == -1);
    puts("PASS: 16 full-frame writes, 2 single-page writes, offsets and recovery after partial "
         "write");
    return 0;
}
