/*
 * Copyright (c) 2025 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Routes ETL runtime errors to Zephyr. ETL is built with exceptions disabled
 * and CONFIG_ETL_LOG_ERRORS=y, so on a failed check ETL invokes the registered
 * error handler instead of throwing. We fail fast: report the error, then panic.
 * Registered automatically via SYS_INIT at PRE_KERNEL_1 (priority 0) so the
 * fail-fast callback is armed before essentially all other init - an ETL check
 * failing during early boot must panic, not silently continue. set_callback only
 * writes a static pointer and touches no kernel services, so it is safe this early.
 *
 * The callback uses printk rather than LOG_ERR: it can run at any boot stage
 * (including before the logging subsystem is up), and printk is synchronous and
 * early-boot-safe, so the diagnostic is emitted before k_panic() halts.
 */

#include <etl/error_handler.h>
#include <etl/exception.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

namespace {

void etl_error(const etl::exception &e) {
  printk("ETL error: %s @ %s:%d\n", e.what(), e.file_name(), e.line_number());
  k_panic();
}

int etl_register_error_handler(void) {
  etl::error_handler::set_callback<etl_error>();
  return 0;
}

} // namespace

SYS_INIT(etl_register_error_handler, PRE_KERNEL_1, 0);
