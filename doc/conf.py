# SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only
#
# Sphinx configuration, see
# https://www.sphinx-doc.org/en/master/usage/configuration.html

project = "ASTERIX Category 240"
copyright = "2026, Muhammad Waleed Badar"
author = "Muhammad Waleed Badar"
release = "0.1.0"

extensions = ["sphinx.ext.intersphinx"]

exclude_patterns = ["_build_sphinx", "_build_doxygen", "Thumbs.db", ".DS_Store"]

html_theme = "alabaster"

intersphinx_mapping = {"zephyr": ("https://docs.zephyrproject.org/latest/", None)}
