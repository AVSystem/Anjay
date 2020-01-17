# -*- coding: utf-8 -*-
#
# Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from builders.dummy import DummyBuilder
from snippet_source import SnippetSourceNode


class SnippetSourceListReferencesBuilder(DummyBuilder):
    name = 'snippet_source_list_references'

    def __init__(self, *args, **kwargs):
        super(SnippetSourceListReferencesBuilder, self).__init__(*args, **kwargs)

        self.referenced_docs = set()

    def write_doc(self, docname, doctree):
        for node in doctree.traverse(SnippetSourceNode):
            self.referenced_docs.add(node.source_filepath)

    def finish(self):
        print('\n'.join(self.referenced_docs))


def setup(app):
    app.add_builder(SnippetSourceListReferencesBuilder)
