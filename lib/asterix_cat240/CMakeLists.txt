# SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only

zephyr_library()
zephyr_library_sources(
  cat240.c
  cat240_codec.c
)
zephyr_library_sources_ifdef(CONFIG_ASTERIX_CAT240_CARTESIAN cat240_cartesian.c)
