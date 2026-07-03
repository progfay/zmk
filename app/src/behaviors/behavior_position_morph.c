/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_position_morph

#include <zephyr/device.h>
#include <zephyr/sys/util.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct behavior_position_morph_config {
    struct zmk_behavior_binding normal_binding;
    struct zmk_behavior_binding morph_binding;
    const uint32_t *positions;
    size_t positions_len;
};

struct behavior_position_morph_data {
    struct zmk_behavior_binding *pressed_binding;
    uint32_t held_positions;
};

static int on_position_morph_binding_pressed(struct zmk_behavior_binding *binding,
                                             struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_position_morph_config *cfg = dev->config;
    struct behavior_position_morph_data *data = dev->data;

    if (data->pressed_binding != NULL) {
        LOG_ERR("Can't press the same position-morph twice");
        return -ENOTSUP;
    }

    if (data->held_positions != 0) {
        data->pressed_binding = (struct zmk_behavior_binding *)&cfg->morph_binding;
    } else {
        data->pressed_binding = (struct zmk_behavior_binding *)&cfg->normal_binding;
    }
    return zmk_behavior_invoke_binding(data->pressed_binding, event, true);
}

static int on_position_morph_binding_released(struct zmk_behavior_binding *binding,
                                              struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_position_morph_data *data = dev->data;

    if (data->pressed_binding == NULL) {
        LOG_ERR("Position-morph already released");
        return -ENOTSUP;
    }

    struct zmk_behavior_binding *pressed_binding = data->pressed_binding;
    data->pressed_binding = NULL;
    return zmk_behavior_invoke_binding(pressed_binding, event, false);
}

static const struct behavior_driver_api behavior_position_morph_driver_api = {
    .binding_pressed = on_position_morph_binding_pressed,
    .binding_released = on_position_morph_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define POSITION_MORPH_DEV(n) DEVICE_DT_INST_GET(n),

static const struct device *const position_morph_devs[] = {
    DT_INST_FOREACH_STATUS_OKAY(POSITION_MORPH_DEV)};

static int position_morph_position_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    for (size_t d = 0; d < ARRAY_SIZE(position_morph_devs); d++) {
        const struct behavior_position_morph_config *cfg = position_morph_devs[d]->config;
        struct behavior_position_morph_data *data = position_morph_devs[d]->data;
        for (size_t i = 0; i < cfg->positions_len; i++) {
            if (cfg->positions[i] == ev->position) {
                WRITE_BIT(data->held_positions, i, ev->state);
            }
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(behavior_position_morph, position_morph_position_listener);
ZMK_SUBSCRIPTION(behavior_position_morph, zmk_position_state_changed);

#define _TRANSFORM_ENTRY(idx, node)                                                                \
    {                                                                                              \
        .behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE_BY_IDX(node, bindings, idx)),               \
        .param1 = COND_CODE_0(DT_INST_PHA_HAS_CELL_AT_IDX(node, bindings, idx, param1), (0),       \
                              (DT_INST_PHA_BY_IDX(node, bindings, idx, param1))),                  \
        .param2 = COND_CODE_0(DT_INST_PHA_HAS_CELL_AT_IDX(node, bindings, idx, param2), (0),       \
                              (DT_INST_PHA_BY_IDX(node, bindings, idx, param2))),                  \
    }

#define KP_INST(n)                                                                                 \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, positions) <= 32,                                             \
                 "position-morph supports at most 32 positions");                                  \
    static const uint32_t behavior_position_morph_positions_##n[] = DT_INST_PROP(n, positions);    \
    static struct behavior_position_morph_config behavior_position_morph_config_##n = {            \
        .normal_binding = _TRANSFORM_ENTRY(0, n),                                                  \
        .morph_binding = _TRANSFORM_ENTRY(1, n),                                                   \
        .positions = behavior_position_morph_positions_##n,                                        \
        .positions_len = DT_INST_PROP_LEN(n, positions),                                           \
    };                                                                                             \
    static struct behavior_position_morph_data behavior_position_morph_data_##n = {};              \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, &behavior_position_morph_data_##n,                      \
                            &behavior_position_morph_config_##n, POST_KERNEL,                      \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                   \
                            &behavior_position_morph_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KP_INST)

#endif
