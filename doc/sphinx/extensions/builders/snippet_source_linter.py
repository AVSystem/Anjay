import os
import re
import collections

from snippet_source import SnippetSourceNode
from file_dirtiness_checker import FileDirtinessChecker
from builders.dummy import DummyBuilder


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, '../../../..'))

DOC_ROOT = os.path.join(PROJECT_ROOT, "doc/sphinx/source")
EXAMPLES_PATH = "examples" # relative to PROJECT_ROOT
EXAMPLES_ROOT = os.path.join(PROJECT_ROOT, EXAMPLES_PATH)
MD5HASHES_FILE_PATH = os.path.join(EXAMPLES_ROOT, 'test_examples.md5')


class CodeChunk:
    """
    A chunk of code that should be found in source file (+/- indents).
    """
    def __init__(self,
                 doc_source_path,
                 doc_source_start_lineno,
                 code_source_path,
                 chunk_code):
        """
        @param doc_source_path: str
                - path to a documentation source @p chunk_code was
                  extracted from.
        @param doc_source_start_lineno: int
                - line number at which @p chunk_code can be found in
                  @p doc_source_path, used to provide extra debug info.
        @param code_source_path: str
                - path to the source code file the chunk should be in.
        @param chunk_code: str
                - code snippet, as found in docs.
        """
        self.doc_source_path = doc_source_path
        self.doc_source_start_lineno = doc_source_start_lineno
        self.code_source_path = code_source_path
        self.code = chunk_code


    def to_regex(self):
        """
        @returns: str
                - regex matching the chunk represented by SELF +/- indents.
        """
        return r'\s+'.join(re.escape(line.strip())
                           for line in self.code.split('\n') if line.strip())


    def __str__(self):
        return ('----- BEGIN CHUNK -----\n'
                'doc:  %s line %d\n'
                'code: %s\n'
                '----- CODE -----\n'
                '%s\n'
                '----- END CHUNK -----'
                % (self.doc_source_path, self.doc_source_start_lineno,
                   self.code_source_path, self.code))


class CodeSnippet:
    def __init__(self,
                 doc_source_path,
                 doc_source_start_lineno,
                 code_source_path,
                 code):
        """
        Code snippet may consist of multiple chunks separated by
        SNIPPET_SEPARATOR (i.e. "// ... something here" comment). In an actual
        source file there may be some code in place of the separator - that
        should not trigger a mismatch error.

        @param doc_source_path: str
                - path to a documentation source @p code_lines were
                  extracted from.
        @param doc_source_start_lineno: int
                - line number of the first line in @p code_lines in
                  @p doc_source_path, used to provide extra debug info.
        @param code_source_path: str
                - path to a source file @p code_lines associated with
                  @p code_lines (through ".. snippet-source:" comment).
                  Relative to PROJECT_ROOT.
        @param code: str
                - code snippet extracted from @p doc_source_path file.
        """
        SNIPPET_SEPARATOR = r'^\s*//\s*\.\.\..*$'

        self.code_source_path = code_source_path
        self.chunks = []

        chunk_lines = []
        start_idx = 0

        for idx, line in enumerate(code.split('\n')):
            if re.match(SNIPPET_SEPARATOR, line):
                self.chunks.append(CodeChunk(doc_source_path,
                                             doc_source_start_lineno + start_idx,
                                             code_source_path,
                                             '\n'.join(chunk_lines)))
                chunk_lines = []
                start_idx = idx + 1
            else:
                chunk_lines.append(line)

        self.chunks.append(CodeChunk(doc_source_path,
                                     doc_source_start_lineno + start_idx,
                                     code_source_path,
                                     '\n'.join(chunk_lines)))


    def get_invalid_chunks(self):
        """
        @returns: List[CodeChunk]
                - a list of chunks that were not found in associated source
                  code files.
        """
        with open(os.path.join(PROJECT_ROOT, self.code_source_path)) as f:
            source = f.read()

        invalid_chunks = []

        for chunk in self.chunks:
            if not re.search(chunk.to_regex(), source):
                invalid_chunks.append(chunk)

        return invalid_chunks


DocSourceErrors = collections.namedtuple('DocSourceErrors',
                                         ['invalid_chunks', # List[CodeChunk]
                                          'dirty_referenced_paths', # Set[source_path: str]
                                          'missing_referenced_paths']) # Dict[source_path: str, List[line: int]]


class SnippetSourceLintBuilder(DummyBuilder):
    name = 'snippet_source_lint'


    def __init__(self, *args, **kwargs):
        super(SnippetSourceLintBuilder, self).__init__(*args, **kwargs)

        # doc_filename: str -> DocSourceErrors
        self.possibly_invalid_docs = dict()


    def write_doc(self, docname, doctree):
        dirtiness_checker = FileDirtinessChecker(MD5HASHES_FILE_PATH)
        invalid_chunks = []
        dirty_referenced_paths = set()
        missing_referenced_paths = collections.defaultdict(lambda: [])

        for node in doctree.traverse(SnippetSourceNode):
            try:
                snip = CodeSnippet(docname,
                                   node.line,
                                   node.source_filepath,
                                   node.astext())
                invalid_chunks += snip.get_invalid_chunks()

                if dirtiness_checker.is_file_dirty(node.source_filepath):
                    dirty_referenced_paths.add(node.source_filepath)
            except (OSError, IOError):
                missing_referenced_paths[node.source_filepath].append(node.line)

        if invalid_chunks or dirty_referenced_paths or missing_referenced_paths:
            self.possibly_invalid_docs[docname] = DocSourceErrors(invalid_chunks, dirty_referenced_paths, missing_referenced_paths)


    def finish(self):
        if self.possibly_invalid_docs:
            print('')
            print('Some potential errors detected in documentation:')
            print('')

        for doc_filepath, errors in sorted(self.possibly_invalid_docs.items()):
            print('- %s:' % (doc_filepath,))
            if errors.missing_referenced_paths:
                print('  - %s references to missing files:' % (len(errors.missing_referenced_paths),))
                for path, lines in sorted(errors.missing_referenced_paths.items()):
                    print('    - %s at line%s %s' % (path, 's' if len(lines) > 1 else '', ', '.join(str(l) for l in lines)))

            if errors.dirty_referenced_paths:
                print('  - %d references to recently modified files:' % (len(errors.dirty_referenced_paths),))
                for path in sorted(errors.dirty_referenced_paths):
                    print('    - %s' % (path,))

            if errors.invalid_chunks:
                print('  - %s chunks missing from sources' % (len(errors.invalid_chunks),))
                print('')
                for chunk in errors.invalid_chunks:
                    print(chunk)

            print('')

        if self.possibly_invalid_docs:
            print('Resolve errors above, then use following command to update md5 hash cache:')
            print('')
            print('    cd "%s"; sphinx-build -Q -b snippet_source_list_references "%s" /tmp | xargs md5sum > "%s"' % (PROJECT_ROOT, DOC_ROOT, MD5HASHES_FILE_PATH))
            print('')

            raise Exception('Lint errors occurred')
        else:
            print('snippet-source lint OK')


def setup(app):
    app.add_builder(SnippetSourceLintBuilder)
