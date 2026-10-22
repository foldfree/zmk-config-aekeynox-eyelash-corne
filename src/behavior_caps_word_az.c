/*
 * Caps word behavior with configurable shift-list and no-default-keys,
 * for use with ZMK v0.3-based forks that lack the caps-word rework
 * (mirrors the semantics of the newer zmk,behavior-caps-word).
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_caps_word_az

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>

#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/modifiers_state_changed.h>
#include <zmk/keys.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct caps_word_az_item {
    uint16_t page;
    uint32_t id;
};

struct caps_word_az_continue_item {
    uint16_t page;
    uint32_t id;
    uint8_t implicit_modifiers;
};

struct behavior_caps_word_az_config {
    zmk_mod_flags_t mods;
    bool no_default_keys;
    uint8_t shifts_count;
    const struct caps_word_az_item *shifts;
    uint8_t continuations_count;
    const struct caps_word_az_continue_item *continuations;
};

struct behavior_caps_word_az_data {
    bool active;
};

static void activate_caps_word_az(const struct device *dev) {
    struct behavior_caps_word_az_data *data = dev->data;

    data->active = true;
}

static void deactivate_caps_word_az(const struct device *dev) {
    struct behavior_caps_word_az_data *data = dev->data;

    data->active = false;
}

static int on_caps_word_az_binding_pressed(struct zmk_behavior_binding *binding,
                                            struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_caps_word_az_data *data = dev->data;

    if (data->active) {
        deactivate_caps_word_az(dev);
    } else {
        activate_caps_word_az(dev);
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_caps_word_az_binding_released(struct zmk_behavior_binding *binding,
                                            struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_caps_word_az_driver_api = {
    .binding_pressed = on_caps_word_az_binding_pressed,
    .binding_released = on_caps_word_az_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

static int caps_word_az_keycode_state_changed_listener(const zmk_event_t *eh);

ZMK_LISTENER(behavior_caps_word_az, caps_word_az_keycode_state_changed_listener);
ZMK_SUBSCRIPTION(behavior_caps_word_az, zmk_keycode_state_changed);

#define GET_DEV(inst) DEVICE_DT_INST_GET(inst),
static const struct device *devs[] = {DT_INST_FOREACH_STATUS_OKAY(GET_DEV)};

static bool caps_word_az_in_shiftlist(const struct behavior_caps_word_az_config *config,
                                      uint16_t usage_page, uint32_t usage_id) {
    for (int i = 0; i < config->shifts_count; i++) {
        if (config->shifts[i].page == usage_page && config->shifts[i].id == usage_id) {
            LOG_DBG("Found shifted usage: 0x%02X - 0x%02X", usage_page, usage_id);
            return true;
        }
    }

    return false;
}

static bool caps_word_az_in_continuelist(const struct behavior_caps_word_az_config *config,
                                         uint16_t usage_page, uint32_t usage_id,
                                         uint8_t implicit_modifiers) {
    for (int i = 0; i < config->continuations_count; i++) {
        const struct caps_word_az_continue_item *continuation = &config->continuations[i];

        if (continuation->page == usage_page && continuation->id == usage_id &&
            (continuation->implicit_modifiers &
             (implicit_modifiers | zmk_hid_get_explicit_mods())) ==
                continuation->implicit_modifiers) {
            LOG_DBG("Continuing capsword, found included usage: 0x%02X - 0x%02X", usage_page,
                    usage_id);
            return true;
        }
    }

    return false;
}

static bool caps_word_az_is_alpha(uint32_t usage_id) {
    return (usage_id >= HID_USAGE_KEY_KEYBOARD_A && usage_id <= HID_USAGE_KEY_KEYBOARD_Z);
}

static bool caps_word_az_is_numeric(uint32_t usage_id) {
    return (usage_id >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION &&
            usage_id <= HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS);
}

static int caps_word_az_keycode_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    for (int i = 0; i < ARRAY_SIZE(devs); i++) {
        const struct device *dev = devs[i];

        struct behavior_caps_word_az_data *data = dev->data;
        if (!data->active) {
            continue;
        }

        const struct behavior_caps_word_az_config *config = dev->config;

        bool in_shift_list = caps_word_az_in_shiftlist(config, ev->usage_page, ev->keycode);
        if (in_shift_list) {
            LOG_DBG("Enhancing usage 0x%02X with modifiers: 0x%02X", ev->keycode, config->mods);
            ev->implicit_modifiers |= config->mods;
        }

        bool allowed = in_shift_list ||
                       caps_word_az_in_continuelist(config, ev->usage_page, ev->keycode,
                                                    ev->implicit_modifiers) ||
                       is_mod(ev->usage_page, ev->keycode) ||
                       (!config->no_default_keys && (caps_word_az_is_alpha(ev->keycode) ||
                                                     caps_word_az_is_numeric(ev->keycode)));

        if (!allowed) {
            LOG_DBG("Deactivating caps_word_az for 0x%02X - 0x%02X", ev->usage_page, ev->keycode);
            deactivate_caps_word_az(dev);
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

#define SHIFT_ITEM(i, n) {.page = ZMK_HID_USAGE_PAGE(i), .id = ZMK_HID_USAGE_ID(i)}

#define CONTINUE_ITEM(i, n)                                                                        \
    {.page = ZMK_HID_USAGE_PAGE(i), .id = ZMK_HID_USAGE_ID(i),                                     \
     .implicit_modifiers = SELECT_MODS(i)}

#define KP_INST(n)                                                                                 \
    static struct behavior_caps_word_az_data behavior_caps_word_az_data_##n = {.active = false};  \
    static const struct caps_word_az_item behavior_caps_word_az_shifts_##n[] = {                 \
        LISTIFY(DT_INST_PROP_LEN(n, shift_list), SHIFT_ITEM, (, ), n)};                           \
    static const struct caps_word_az_continue_item behavior_caps_word_az_conts_##n[] = {         \
        LISTIFY(DT_INST_PROP_LEN_OR(n, continue_list, 0), CONTINUE_ITEM, (, ), n)};              \
    static const struct behavior_caps_word_az_config behavior_caps_word_az_config_##n = {         \
        .mods = DT_INST_PROP_OR(n, mods, MOD_LSFT),                                                \
        .no_default_keys = DT_INST_PROP_OR(n, no_default_keys, 0),                                \
        .shifts = behavior_caps_word_az_shifts_##n,                                                \
        .shifts_count = DT_INST_PROP_LEN(n, shift_list),                                           \
        .continuations = behavior_caps_word_az_conts_##n,                                         \
        .continuations_count = DT_INST_PROP_LEN_OR(n, continue_list, 0),                          \
    };                                                                                            \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, &behavior_caps_word_az_data_##n,                       \
                            &behavior_caps_word_az_config_##n, POST_KERNEL,                        \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_caps_word_az_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KP_INST)

#endif
