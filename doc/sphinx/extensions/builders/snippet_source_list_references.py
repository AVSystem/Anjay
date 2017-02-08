from snippet_source import SnippetSourceNode
from builders.dummy import DummyBuilder


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
