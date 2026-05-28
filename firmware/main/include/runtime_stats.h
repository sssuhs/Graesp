#pragma once

#include "app_types.h"

void runtime_stats_init(runtime_stats_t *stats);
void runtime_stats_set_self_test(runtime_stats_t *stats, bool ok, uint32_t fault_mask);
void runtime_stats_update(runtime_stats_t *stats,
                          const thermal_features_t *features,
                          const overload_result_t *result);
