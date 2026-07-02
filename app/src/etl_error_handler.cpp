/*
 * Copyright (c) 2025 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Routes ETL runtime errors to Zephyr. ETL is built with exceptions disabled
 * and CONFIG_ETL_LOG_ERRORS=y, so on a failed check ETL invokes the registered
 * error handler instead of throwing. We fail fast: log the error, then panic.
 * Registered automatically via SYS_INIT; no caller action required.
 */

#include <etl/error_handler.h>
#include <etl/exception.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(etl, CONFIG_LOG_DEFAULT_LEVEL);

namespace {

void etl_error(const etl::exception &e) {
  LOG_ERR("ETL error: %s @ %s:%d", e.what(), e.file_name(), e.line_number());
  k_panic();
}

int etl_register_error_handler(void) {
  etl::error_handler::set_callback<etl_error>();
  return 0;
}

} // namespace

SYS_INIT(etl_register_error_handler, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
