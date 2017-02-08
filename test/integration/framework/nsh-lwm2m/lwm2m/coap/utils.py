import string

def hexlify(s):
    return ''.join('\\x%02x' % c for c in s)

def hexlify_nonprintable(s):
    return ''.join(chr(c) if chr(c) in string.printable else ('\\x%02x' % c) for c in s)
