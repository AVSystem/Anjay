import docutils.nodes
import docutils.parsers.rst
import sphinx.directives.code

import sphinx.writers.html
import sphinx.writers.latex
import sphinx.writers.text


class SnippetSourceNode(docutils.nodes.literal_block):
    def __init__(self, source_filepath, lineno, rawsource, text):
        super(SnippetSourceNode, self).__init__(rawsource, text)

        self.source_filepath = source_filepath
        if self.line is None:
            # line number is *sometimes* not passed
            self.line = lineno


class SnippetSourceDirective(docutils.parsers.rst.Directive):
    '''
    .. snippet-source:: filepath_relative_to_project_root

       [code]
    '''
    required_arguments = 1
    has_content = True

    def run(self):
        source_file = self.arguments[0]

        code = u'\n'.join(self.content)
        return [SnippetSourceNode(source_file, self.lineno, code, code)]


def setup(app):
    app.add_node(SnippetSourceNode,
                 html=(sphinx.writers.html.HTMLTranslator.visit_literal_block,
                       sphinx.writers.html.HTMLTranslator.depart_literal_block),
                 latex=(sphinx.writers.latex.LaTeXTranslator.visit_literal_block,
                        sphinx.writers.latex.LaTeXTranslator.depart_literal_block),
                 text=(sphinx.writers.text.TextTranslator.visit_literal_block,
                        sphinx.writers.text.TextTranslator.depart_literal_block))

    app.add_directive('snippet-source', SnippetSourceDirective)
