# -*- coding: utf-8 -*-
#
# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

from builders.dummy import DummyBuilder
from snippet_source import SnippetSourceNode


class SnippetSourceListReferencesBuilder(DummyBuilder):
    name = 'snippet_source_list_references'
    # write_doc() accumulates paths in this builder for finish() to print.
    allow_parallel = False

    def __init__(self, *args, **kwargs):
        super(SnippetSourceListReferencesBuilder, self).__init__(*args, **kwargs)

        self.referenced_docs = set()

    def write_doc(self, docname, doctree):
        list_commercial = False

        for node in doctree.traverse(SnippetSourceNode):
            if list_commercial or not node['commercial']:
                self.referenced_docs.add(node.source_filepath)

    def finish(self):
        print('\n'.join(self.referenced_docs))


def setup(app):
    app.add_builder(SnippetSourceListReferencesBuilder)
    # Document reading has no shared state. This builder writes serially, and
    # registering it does not affect parallel writing by other builders.
    return {
        'parallel_read_safe': True,
        'parallel_write_safe': True,
    }
