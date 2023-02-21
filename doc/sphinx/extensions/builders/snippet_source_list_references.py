# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

from builders.dummy import DummyBuilder
from snippet_source import SnippetSourceNode


class SnippetSourceListReferencesBuilder(DummyBuilder):
    name = 'snippet_source_list_references'

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
