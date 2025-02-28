# -*- coding: utf-8 -*-
#
# Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

from docutils import nodes
from sphinx.application import Sphinx

def small_literal_role(name, rawtext, text, lineno, inliner, options={}, content=[]):
    return [nodes.literal(rawtext, text, classes=['small-literal'])], []

def setup(app: Sphinx):
    app.add_role('small-literal', small_literal_role)
