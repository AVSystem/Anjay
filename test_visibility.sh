#!/bin/bash
set -e

preprocess_file() {
	# This uses ex/vim regexes and search-and-replace syntax.
	# https://www.youtube.com/watch?v=0p_1QSUsbsM
	#
	# Some handy references:
	# - http://jeetworks.org/vim-regular-expression-special-characters-to-escape-or-not-to-escape/
	# - http://vim.wikia.com/wiki/Search_and_replace
	# - http://vimdoc.sourceforge.net/cgi-bin/help?tag=/magic
	# - http://vimdoc.sourceforge.net/cgi-bin/help?tag=pattern
	#
	# Also, things to note:
	# - '@' is used here as the argument delimiter, instead of the
	#   usually used '/' (as in s/one/two/), due to slashes in patterns
	# - in ex's language, double-quote " denotes line comment
	ex -m "$1" <<-'EOF'
		%s@\\\s*\n@ @g      " joins lines on trailing backslash
		%s@^\s*@@g          " removes leading spaces
		%s@/\*\_.\{-}\*/@@g " removes block comments (incl. multiline)
		%s@//.*$@@g         " removes line comments
		%s@^#.*$@@g         " removes preprocessor directives
		%s@\s*$@@g          " removes trailing spaces
		%s@^extern .*);$@@g " removes extern function declarations
		g/^$/d              " removes empty lines
		%p                  " prints the processed document
	EOF
}

read_lines() {
	FIRST=''
	read FIRST || return 0
	LAST="$FIRST"
	while read REPLY; do
		LAST="$REPLY"
	done
}

_check_empty() {
	test -z "$FIRST" -a -z "$LAST"
}

_check_private_header_top() {
	test "$FIRST" = 'VISIBILITY_PRIVATE_HEADER_BEGIN'
}

_check_private_header_bottom() {
	test "$LAST" = 'VISIBILITY_PRIVATE_HEADER_END'
}

check_source() {
	test "$FIRST" = 'VISIBILITY_SOURCE_BEGIN'
}

check_private_header() {
	_check_private_header_top && _check_private_header_bottom
}

check_public_header() {
        ! { [[ $FIRST =~ '^VISIBILITY' ]] || [[ $LAST =~ '^VISIBILITY' ]]; }
}

classify_file() {
	if [[ $1 =~ .h$ ]]; then
		if [[ $1 =~ /include_public/ ]]; then
			echo 'public_header'
		else
			echo 'private_header'
		fi
	else
		echo 'source'
	fi
}

preprocess_file "$1" | { read_lines; _check_empty || check_"$(classify_file "$1")"; }
