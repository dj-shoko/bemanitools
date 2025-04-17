#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <windows.h>

#include "ViGEm/Client.h"

#include "bemanitools/ddrio.h"
#include "util/log.h"
#include "util/math.h"
#include "util/thread.h"
#include "vigemstub/helper.h"

#include "vigem-ddrio/config-vigem-ddrio.h"

#define NUM_PADS 2

bool check_key(uint32_t input, size_t idx_in)
{
    if ((input >> idx_in) & 1) {
        return true;
    }

    return false;
}

uint32_t check_assign_key(uint32_t input, size_t idx_in, size_t bit_out)
{
    if (check_key(input, idx_in)) {
        return bit_out;
    }

    return 0;
}

uint32_t check_assign_light(uint32_t input, size_t idx_in, size_t gpio_out)
{
    if (check_key(input, idx_in)) {
        return 1 << gpio_out;
    }

    return 0;
}

void set_all_lights(bool on)
{
    ddr_io_set_lights_extio(on ? UINT32_MAX : 0);
    ddr_io_set_lights_p3io(on ? UINT32_MAX : 0);
    ddr_io_set_lights_hdxs_panel(on ? UINT32_MAX : 0);

    for (int i = 0; i < 4; i++) {
        uint8_t val = on ? UINT8_MAX : 0;
        ddr_io_set_lights_hdxs_rgb(i, val, val, val);
    }
}

void set_reactive_lights(uint32_t input_state)
{
    uint32_t new_extio_state = 0;
    uint32_t new_p3io_state = 0;
    uint32_t new_hdxs_state = 0;

    new_extio_state |= check_assign_light(input_state, DDR_P1_UP, LIGHT_P1_UP);
    new_extio_state |=
        check_assign_light(input_state, DDR_P1_DOWN, LIGHT_P1_DOWN);
    new_extio_state |=
        check_assign_light(input_state, DDR_P1_LEFT, LIGHT_P1_LEFT);
    new_extio_state |=
        check_assign_light(input_state, DDR_P1_RIGHT, LIGHT_P1_RIGHT);

    new_extio_state |= check_assign_light(input_state, DDR_P2_UP, LIGHT_P2_UP);
    new_extio_state |=
        check_assign_light(input_state, DDR_P2_DOWN, LIGHT_P2_DOWN);
    new_extio_state |=
        check_assign_light(input_state, DDR_P2_LEFT, LIGHT_P2_LEFT);
    new_extio_state |=
        check_assign_light(input_state, DDR_P2_RIGHT, LIGHT_P2_RIGHT);

    new_p3io_state |=
        check_assign_light(input_state, DDR_P1_START, LIGHT_P1_MENU);
    new_p3io_state |=
        check_assign_light(input_state, DDR_P2_START, LIGHT_P2_MENU);

    new_hdxs_state |=
        check_assign_light(input_state, DDR_P1_START, LIGHT_HD_P1_START);
    new_hdxs_state |=
        check_assign_light(input_state, DDR_P1_MENU_UP, LIGHT_HD_P1_UP_DOWN);
    new_hdxs_state |=
        check_assign_light(input_state, DDR_P1_MENU_DOWN, LIGHT_HD_P1_UP_DOWN);
    new_hdxs_state |= check_assign_light(
        input_state, DDR_P1_MENU_LEFT, LIGHT_HD_P1_LEFT_RIGHT);
    new_hdxs_state |= check_assign_light(
        input_state, DDR_P1_MENU_RIGHT, LIGHT_HD_P1_LEFT_RIGHT);

    new_hdxs_state |=
        check_assign_light(input_state, DDR_P2_START, LIGHT_HD_P2_START);
    new_hdxs_state |=
        check_assign_light(input_state, DDR_P2_MENU_UP, LIGHT_HD_P2_UP_DOWN);
    new_hdxs_state |=
        check_assign_light(input_state, DDR_P2_MENU_DOWN, LIGHT_HD_P2_UP_DOWN);
    new_hdxs_state |= check_assign_light(
        input_state, DDR_P2_MENU_LEFT, LIGHT_HD_P2_LEFT_RIGHT);
    new_hdxs_state |= check_assign_light(
        input_state, DDR_P2_MENU_RIGHT, LIGHT_HD_P2_LEFT_RIGHT);

    ddr_io_set_lights_extio(new_extio_state);
    ddr_io_set_lights_p3io(new_p3io_state);
    ddr_io_set_lights_hdxs_panel(new_hdxs_state);
}

int main(int argc, char **argv)
{
    log_to_writer(log_writer_stdout, NULL);

    struct vigem_ddrio_config config;
    if (!get_vigem_ddrio_config(&config)) {
        exit(EXIT_FAILURE);
    }

    ddr_io_set_loggers(
        log_impl_misc, log_impl_info, log_impl_warning, log_impl_fatal);

    if (!ddr_io_init(crt_thread_create, crt_thread_join, crt_thread_destroy)) {
        log_warning("Initializing ddrio failed");
        return -1;
    }

    // go ahead and turn lights off as some are on during boot.
    set_all_lights(false);

    PVIGEM_CLIENT client = vigem_helper_setup();

    if (!client) {
        log_warning("client failed to connect failed");
        return -1;
    }

    PVIGEM_TARGET pad[NUM_PADS];
    bool failed = false;

    for (uint8_t i = 0; i < NUM_PADS; i++) {
        pad[i] = vigem_helper_add_pad(client);

        if (!pad[i]) {
            log_warning("vigem_alloc pad %d failed", i);
            failed = true;
        }
    }

    if (failed) {
        ddr_io_fini();
        return -1;
    }

    bool loop = true;
    uint32_t pad_state = 0;
    DS4_REPORT state[NUM_PADS] = {0};

    log_info("vigem init succeeded, beginning poll loop");

    while (loop) {
        pad_state = ddr_io_read_pad();

        for (uint8_t i = 0; i < NUM_PADS; i++) {
            memset(&state[i], 0, sizeof(state[i]));
        }

        state[0].wButtons |=
            check_assign_key(pad_state, DDR_TEST, DS4_BUTTON_THUMB_LEFT);
        state[0].wButtons |=
            check_assign_key(pad_state, DDR_SERVICE, DS4_BUTTON_THUMB_RIGHT);
        state[0].wButtons |=
            check_assign_key(pad_state, DDR_COIN, DS4_SPECIAL_BUTTON_TOUCHPAD);

        // assign arrows to face buttons due to jumps / hat interpretation
        state[0].wButtons |=
            check_assign_key(pad_state, DDR_P1_UP, DS4_BUTTON_TRIANGLE);
        state[0].wButtons |=
            check_assign_key(pad_state, DDR_P1_DOWN, DS4_BUTTON_CROSS);
        state[0].wButtons |=
            check_assign_key(pad_state, DDR_P1_LEFT, DS4_BUTTON_SQUARE);
        state[0].wButtons |=
            check_assign_key(pad_state, DDR_P1_RIGHT, DS4_BUTTON_CIRCLE);

        state[0].wButtons |=
            check_assign_key(pad_state, DDR_P1_START, DS4_BUTTON_OPTIONS);
        state[0].wButtons |=
            check_assign_key(pad_state, DDR_P1_MENU_UP, DS4_BUTTON_DPAD_NORTH);
        state[0].wButtons |= check_assign_key(
            pad_state, DDR_P1_MENU_DOWN, DS4_BUTTON_DPAD_SOUTH);
        state[0].wButtons |= check_assign_key(
            pad_state, DDR_P1_MENU_LEFT, DS4_BUTTON_DPAD_WEST);
        state[0].wButtons |= check_assign_key(
            pad_state, DDR_P1_MENU_RIGHT, DS4_BUTTON_DPAD_EAST);

        state[1].wButtons |=
            check_assign_key(pad_state, DDR_P2_UP, DS4_BUTTON_TRIANGLE);
        state[1].wButtons |=
            check_assign_key(pad_state, DDR_P2_DOWN, DS4_BUTTON_CROSS);
        state[1].wButtons |=
            check_assign_key(pad_state, DDR_P2_LEFT, DS4_BUTTON_SQUARE);
        state[1].wButtons |=
            check_assign_key(pad_state, DDR_P2_RIGHT, DS4_BUTTON_CIRCLE);

        state[1].wButtons |=
            check_assign_key(pad_state, DDR_P2_START, DS4_BUTTON_OPTIONS);
        state[1].wButtons |=
            check_assign_key(pad_state, DDR_P2_MENU_UP, DS4_BUTTON_DPAD_NORTH);
        state[1].wButtons |= check_assign_key(
            pad_state, DDR_P2_MENU_DOWN, DS4_BUTTON_DPAD_SOUTH);
        state[1].wButtons |= check_assign_key(
            pad_state, DDR_P2_MENU_LEFT, DS4_BUTTON_DPAD_WEST);
        state[1].wButtons |= check_assign_key(
            pad_state, DDR_P2_MENU_RIGHT, DS4_BUTTON_DPAD_EAST);

        for (uint8_t i = 0; i < NUM_PADS; i++) {
            vigem_target_ds4_update(client, pad[i], state[i]);
        }

        if (config.enable_reactive_light) {
            set_reactive_lights(pad_state);
        }

        if (check_key(pad_state, DDR_TEST) &&
            check_key(pad_state, DDR_SERVICE)) {
            loop = false;
        }

        // avoid CPU banging
        Sleep(1);
    }

    for (uint8_t i = 0; i < NUM_PADS; i++) {
        vigem_target_remove(client, pad[i]);
        vigem_target_free(pad[i]);
    }

    vigem_free(client);
    ddr_io_fini();

    return 0;
}
