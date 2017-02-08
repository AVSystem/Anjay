try:
    from sphinx.builders.dummy import DummyBuilder
except ImportError:
    # dummy builder available in sphinx 1.4+
    from sphinx.builders import Builder

    class DummyBuilder(Builder):
        name = 'dummy'
        allow_parallel = True

        def init(self): pass
        def get_outdated_docs(self): return self.env.found_docs
        def get_target_uri(self, docname, typ=None): return ''
        def prepare_writing(self, docnames): pass
        def write_doc(self, docname, doctree): pass
        def finish(self): pass
