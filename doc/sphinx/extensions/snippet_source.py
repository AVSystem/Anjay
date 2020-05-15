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

import docutils
import sphinx.writers.html
import sphinx.writers.latex
import sphinx.writers.text
from docutils.parsers.rst import directives
from sphinx.directives.code import container_wrapper
from sphinx.transforms.post_transforms.code import HighlightLanguageVisitor
from sphinx.util import parselinenos
from sphinx.util.docutils import SphinxDirective


class SnippetSourceNode(docutils.nodes.literal_block):
    def __init__(self, source_filepath, lineno, rawsource, text):
        super(SnippetSourceNode, self).__init__(rawsource, text)

        self.source_filepath = source_filepath
        if self.line is None:
            # line number is *sometimes* not passed
            self.line = lineno


class SnippetSourceDirective(SphinxDirective):
    '''
    .. snippet-source:: filepath_relative_to_project_root

       [code]
    '''
    required_arguments = 1
    optional_arguments = 1
    has_content = True

    option_spec = {
        'emphasize-lines': directives.unchanged_required,
        'caption': directives.unchanged_required,
    }

    def run(self):
        document = self.state.document
        source_file = self.arguments[0]
        code = u'\n'.join(self.content)

        # heavily inspired by https://github.com/sphinx-doc/sphinx/blob/3.x/sphinx/directives/code.py#L134
        emphasized_lines = self.options.get('emphasize-lines')
        if emphasized_lines:
            try:
                nlines = len(self.content)
                hl_lines = parselinenos(emphasized_lines, nlines)
                if any(i >= nlines for i in hl_lines):
                    raise RuntimeError('line number spec out of range 1-%d: %r' % (
                        (nlines, self.options['emphasize-lines'])))
                hl_lines = [i + 1 for i in hl_lines]
            except ValueError as err:
                return [document.reporter.warning(err, line=self.lineno)]
        else:
            hl_lines = None

        node = SnippetSourceNode(source_file, self.lineno, code, code)
        if hl_lines is not None:
            node['highlight_args'] = {
                'hl_lines': hl_lines
            }

        caption = self.options.get('caption')
        if caption:
            try:
                node = container_wrapper(self, node, caption)
            except ValueError as exc:
                return [document.reporter.warning(exc, line=self.lineno)]

        # See: https://github.com/sphinx-doc/sphinx/blob/3.x/sphinx/directives/code.py#L184
        self.add_name(node)

        return [node]


def setup(app):
    app.add_node(SnippetSourceNode,
                 html=(sphinx.writers.html.HTMLTranslator.visit_literal_block,
                       sphinx.writers.html.HTMLTranslator.depart_literal_block),
                 latex=(sphinx.writers.latex.LaTeXTranslator.visit_literal_block,
                        sphinx.writers.latex.LaTeXTranslator.depart_literal_block),
                 text=(sphinx.writers.text.TextTranslator.visit_literal_block,
                       sphinx.writers.text.TextTranslator.depart_literal_block))

    app.add_directive('snippet-source', SnippetSourceDirective)

    HighlightLanguageVisitor.visit_SnippetSourceNode = HighlightLanguageVisitor.visit_literal_block
