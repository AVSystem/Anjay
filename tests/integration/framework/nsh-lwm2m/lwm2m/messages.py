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

import copy
import functools
import sys
from typing import List, T

from . import coap
from .coap.packet import ANY
from .coap.utils import hexlify_nonprintable, hexlify
from .path import CoapPath, Lwm2mPath, Lwm2mNonemptyPath, Lwm2mObjectPath, Lwm2mResourcePath
from .tlv import TLV


class EscapedBytes:
    """
    A pseudo-type that allows parsing a hex-escaped string into a standard
    bytes object when used as an argument type hint for the powercmd command
    handler.

    Example:
        '\x61\x62\x63' - parsed as b'abc'
    """

    @staticmethod
    def powercmd_parse(text):
        import ast
        result = ast.literal_eval('b"%s"' % (text,))
        return result


def concat_if_not_any(*lists: List[T]):
    if all(list is ANY for list in lists):
        return ANY

    return sum((([] if list is ANY else list) for list in lists), [])


class Lwm2mMsg(coap.Packet):
    """
    Base class of all LWM2M messages.
    """

    @classmethod
    def from_packet(cls, pkt: coap.Packet):
        if not cls._pkt_matches(pkt):
            raise TypeError('packet does not match %s' % (cls.__name__,))

        msg = copy.copy(pkt)
        # It's an awful hack that may explode if any Lwm2mMsg subclass
        # introduces any fields not present in the coap.Packet class.
        # Since all Lwm2mMsg subclasses are meant to be thin wrappers
        # facilitating message creation/recognition, it should never be
        # a problem.
        msg.__class__ = cls
        return msg

    @staticmethod
    def _pkt_matches(_pkt: coap.Packet):
        return True

    def summary(self):
        def block_summary(pkt):
            blk1 = pkt.get_options(coap.Option.BLOCK1)
            blk2 = pkt.get_options(coap.Option.BLOCK2)
            return ', '.join(['']
                             + ([repr(blk1[0])] if blk1 else [])
                             + ([repr(blk2[0])] if blk2 else []))

        return ('%s, %s, id=%s, token=%s%s'
                % (self.code,
                   self.type,
                   self.msg_id,
                   self.token,
                   block_summary(self)))

    def content_summary(self):
        if self.content is ANY:
            return 'ANY'
        else:
            return shorten(hexlify_nonprintable(self.content))

    @staticmethod
    def _decode_text_content(content):
        return 'ascii-ish:\n' + hexlify_nonprintable(content) + '\n'

    @staticmethod
    def _decode_binary_content(content):
        return 'binary:\n' + hexlify(content) + '\n'

    @staticmethod
    def _decode_tlv_content(content):
        try:
            return str(TLV.parse(content))
        except Exception as exc:
            return ('(malformed TLV: %s)\n' % (exc,)
                    + Lwm2mMsg._decode_binary_content(content))


    def _decode_content(self):
        if self.content is ANY:
            return ''

        decoders = {
            coap.ContentFormat.TEXT_PLAIN: Lwm2mMsg._decode_text_content,
            coap.ContentFormat.APPLICATION_LINK: Lwm2mMsg._decode_text_content,
            coap.ContentFormat.APPLICATION_LWM2M_TLV: Lwm2mMsg._decode_tlv_content,
            coap.ContentFormat.APPLICATION_OCTET_STREAM: Lwm2mMsg._decode_binary_content,
        }

        desired_decoders = set()

        for opt in self.get_options(coap.Option.CONTENT_FORMAT):
            decoder = decoders.get(opt.content_to_int(), Lwm2mMsg._decode_binary_content)
            desired_decoders.add(decoder)

        if not desired_decoders:
            desired_decoders.add(decoders[coap.ContentFormat.TEXT_PLAIN])

        decoded_content = ''
        for decoder in desired_decoders:
            decoded_content += decoder(self.content) + '\n'

        return decoded_content

    def details(self):
        return str(self)

    def __str__(self):
        return '%s\n\n%s\n%s' % (self.summary(),
                                 super().__str__(),
                                 self._decode_content())


class Lwm2mResponse(Lwm2mMsg):
    """
    Base class for all LWM2M responses.
    """

    @staticmethod
    def _pkt_matches(_pkt: coap.Packet):
        return False

    @classmethod
    def matching(cls, request):
        if issubclass(cls, Lwm2mEmpty):
            return functools.partial(cls, msg_id=request.msg_id)
        else:
            return functools.partial(cls,
                                     msg_id=request.msg_id,
                                     token=request.token)


def is_lwm2m_nonempty_path(path):
    try:
        return Lwm2mNonemptyPath(path) is not None
    except ValueError:
        return False


def is_lwm2m_path(path):
    try:
        return Lwm2mPath(path) is not None
    except ValueError:
        return False


def is_link_format(pkt):
    fmt = pkt.get_options(coap.Option.CONTENT_FORMAT)
    return (fmt == [coap.Option.CONTENT_FORMAT.APPLICATION_LINK]
            or (fmt == [] and pkt.content == b''))


def shorten(text):
    if len(text) > 30:
        return text[:27] + '...'
    return text


class Lwm2mRequestBootstrap(Lwm2mMsg):
    @staticmethod
    def _pkt_matches(pkt: coap.Packet):
        """Checks if the PKT is a LWM2M Request Bootstrap message."""
        return (pkt.type in (None, coap.Type.CONFIRMABLE)
                and pkt.code == coap.Code.REQ_POST
                and '/bs?ep=' in pkt.get_full_uri())

    def __init__(self,
                 endpoint_name: str,
                 preferred_content_format: int = None,
                 msg_id: int = ANY,
                 token: EscapedBytes = ANY,
                 uri_path: str = '',
                 uri_query: List[str] = None,
                 options: List[coap.Option] = ANY,
                 content: EscapedBytes = ANY):
        if not uri_query:
            uri_query = []
        uri_query = uri_query + ['ep=' + endpoint_name]
        if preferred_content_format is not None:
            uri_query = uri_query + ['pct=%d' % (preferred_content_format,)]
        super().__init__(type=coap.Type.CONFIRMABLE,
                         code=coap.Code.REQ_POST,
                         msg_id=msg_id,
                         token=token,
                         options=concat_if_not_any(
                             CoapPath(uri_path + '/bs').to_uri_options(),
                             [coap.Option.URI_QUERY(query) for query in uri_query],
                             options),
                         content=content)

    def summary(self):
        return ('Request Bootstrap %s: %s' % (self.get_full_uri(),
                                              self.content_summary()))


class Lwm2mBootstrapFinish(Lwm2mMsg):
    @staticmethod
    def _pkt_matches(pkt: coap.Packet):
        """Checks if the PKT is a LWM2M Bootstrap Finish message."""
        return (pkt.type in (None, coap.Type.CONFIRMABLE)
                and pkt.code == coap.Code.REQ_POST
                and pkt.get_full_uri().endswith('/bs'))

    def __init__(self,
                 msg_id: int = ANY,
                 token: EscapedBytes = ANY,
                 options: List[coap.Option] = ANY,
                 content: EscapedBytes = ANY):
        super().__init__(type=coap.Type.CONFIRMABLE,
                         code=coap.Code.REQ_POST,
                         msg_id=msg_id,
                         token=token,
                         options=concat_if_not_any(
                             CoapPath('/bs').to_uri_options(),
                             options),
                         content=content)

    def summary(self):
        return ('Bootstrap Finish %s: %s' % (self.get_full_uri(),
                                             self.content_summary()))




def _split_string_path(path: str,
                       query: List[str] = None):
    """
    Splits a CoAP PATH given as string into a path component and a list of
    query strings ("foo=bar").
    Returns (CoapPath, List[str]) tuple with a parsed CoapPath and a list of
    query strings from the PATH concatenated with QUERY contents (if any).
    """
    path_query = []

    if isinstance(path, str):
        if query is None:
            if '?' in path:
                path, query_string = path.split('?', maxsplit=1)
                path_query = query_string.split('&')

        path = CoapPath(path)

    return path, path_query + (query or [])


class Lwm2mRegister(Lwm2mMsg):
    @staticmethod
    def _pkt_matches(pkt: coap.Packet):
        """Checks if the PKT is a LWM2M Register message."""
        return (pkt.type in (None, coap.Type.CONFIRMABLE)
                and pkt.code == coap.Code.REQ_POST
                and is_link_format(pkt)
                and pkt.get_uri_path().endswith('/rd'))

    def __init__(self,
                 path: str or CoapPath,
                 query: List[str] = None,
                 msg_id: int = ANY,
                 token: EscapedBytes = ANY,
                 options: List[coap.Option] = ANY,
                 content: EscapedBytes = ANY):
        path, query = _split_string_path(path, query)

        super().__init__(type=coap.Type.CONFIRMABLE,
                         code=coap.Code.REQ_POST,
                         msg_id=msg_id,
                         token=token,
                         options=concat_if_not_any(
                             path.to_uri_options(),
                             [coap.Option.URI_QUERY(q) for q in query],
                             [coap.Option.CONTENT_FORMAT.APPLICATION_LINK],
                             options),
                         content=content)

    def summary(self):
        return ('Register %s: %s' % (self.get_full_uri(),
                                     self.content_summary()))


class Lwm2mUpdate(Lwm2mMsg):
    @staticmethod
    def _pkt_matches(pkt: coap.Packet):
        """Checks if the PKT is a LWM2M Update message."""
        # Update is very similar to Execute
        # assumption: Update will never be called on a path that resembles
        # /OID or /OID/IID or /OID/IID/RID
        return (pkt.type in (None, coap.Type.CONFIRMABLE)
                and pkt.code == coap.Code.REQ_POST
                and '/rd/' in pkt.get_uri_path()
                and (is_link_format(pkt)
                     or not is_lwm2m_nonempty_path(pkt.get_full_uri())))

    def __init__(self,
                 path: str or CoapPath,
                 query: List[str] = None,
                 msg_id: int = ANY,
                 token: EscapedBytes = ANY,
                 options: List[coap.Option] = ANY,
                 content: EscapedBytes = ANY):
        path, query = _split_string_path(path, query)

        super().__init__(type=coap.Type.CONFIRMABLE,
                         code=coap.Code.REQ_POST,
                         msg_id=msg_id,
                         token=token,
                         options=concat_if_not_any(
                             path.to_uri_options(),
                             [coap.Option.URI_QUERY(q) for q in query],
                             ([coap.Option.CONTENT_FORMAT.APPLICATION_LINK] if content else []),
                             options),
                         content=content)

    def summary(self):
        return ('Update %s: %s' % (self.get_full_uri(),
                                   self.content_summary()))


class Lwm2mDeregister(Lwm2mMsg):
    @staticmethod
    def _pkt_matches(pkt: coap.Packet):
        return (pkt.type in (None, coap.Type.CONFIRMABLE)
                and pkt.code == coap.Code.REQ_DELETE
                and not is_lwm2m_nonempty_path(pkt.get_uri_path()))

    def __init__(self,
                 path: str or CoapPath,
                 msg_id: int = ANY,
                 token: EscapedBytes = ANY,
                 options: List[coap.Option] = ANY):
        if isinstance(path, str):
            path = CoapPath(path)

        super().__init__(type=coap.Type.CONFIRMABLE,
                         code=coap.Code.REQ_DELETE,
                         msg_id=msg_id,
                         token=token,
                         options=concat_if_not_any(
                             path.to_uri_options(),
                             options))

    def summary(self):
        return 'De-register ' + self.get_full_uri()




class CoapGet(Lwm2mMsg):
    @staticmethod
    def _pkt_matches(pkt: coap.Packet):
        return (pkt.type in (None, coap.Type.CONFIRMABLE)
                and pkt.code == coap.Code.REQ_GET)

    def __init__(self,
                 path: str or CoapPath,
                 accept: coap.AcceptOption = None,
                 msg_id: int = ANY,
                 token: EscapedBytes = ANY,
                 options: List[coap.Option] = ANY):
        if isinstance(path, str):
            path = CoapPath(path)

        if isinstance(accept, int):
            accept = coap.Option.ACCEPT(accept)

        super().__init__(type=coap.Type.CONFIRMABLE,
                         code=coap.Code.REQ_GET,
                         msg_id=msg_id,
                         token=token,
                         options=concat_if_not_any(
                             path.to_uri_options(),
                             ([accept] if accept is not None else []),
                             options))

    def summary(self):
        text = 'GET ' + self.get_full_uri()

        accept = self.get_options(coap.Option.ACCEPT)
        if accept:
            accept_vals = [x.content_to_int() for x in accept]
            text += ': accept ' + ', '.join(map(coap.ContentFormat.to_str, accept_vals))

        return text


class Lwm2mRead(CoapGet):
    @staticmethod
    def _pkt_matches(pkt: coap.Packet):
        return (CoapGet._pkt_matches(pkt)
                and is_lwm2m_nonempty_path(pkt.get_uri_path())
                and not is_link_format(pkt))

    def __init__(self,
                 path: str or Lwm2mNonemptyPath,
                 accept: coap.AcceptOption = None,
                 msg_id: int = ANY,
                 token: EscapedBytes = ANY,
                 options: List[coap.Option] = ANY):
        if isinstance(path, str):
            path = Lwm2mNonemptyPath(path)

        super().__init__(path=path, accept=accept, msg_id=msg_id,
                         token=token, options=options)

    def summary(self):
        text = 'Read ' + self.get_full_uri()

        accept = self.get_options(coap.Option.ACCEPT)
        if accept:
            accept_vals = [x.content_to_int() for x in accept]
            text += ': accept ' + ', '.join(map(coap.ContentFormat.to_str, accept_vals))

        return text


class Lwm2mObserve(Lwm2mRead):
    @staticmethod
    def _pkt_matches(pkt: coap.Packet):
        return (Lwm2mRead._pkt_matches(pkt)
                and len(pkt.get_options(coap.Option.OBSERVE)) > 0)

    def __init__(self,
                 path: str or Lwm2mNonemptyPath,
                 observe: int = 0,
                 accept: coap.AcceptOption = None,
                 msg_id: int = ANY,
                 token: EscapedBytes = ANY,
                 options: List[coap.Option] = ANY):
        if isinstance(path, str):
            path = Lwm2mNonemptyPath(path)

        if isinstance(accept, int):
            accept = coap.Option.ACCEPT(accept)

        super().__init__(path=path,
                         accept=accept,
                         msg_id=msg_id,
                         token=token,
                         options=concat_if_not_any(
                             [coap.Option.OBSERVE(observe)],
                             options))

    def summary(self):
        opt = self.get_options(coap.Option.OBSERVE)
        if len(opt) > 1:
            text = 'Observe %s (multiple Observe options)' % (self.get_full_uri(),)
        else:
            opt = opt[0]
            if opt.content_to_int() == 0:
                text = 'Observe ' + self.get_full_uri()
            elif opt.content_to_int() == 1:
                text = 'Cancel Observation ' + self.get_full_uri()
            else:
                text = 'Observe %s (invalid Observe value: %d)' % (
                    self.get_full_uri(), opt.content_to_int())

        accept = self.get_options(coap.Option.ACCEPT)
        if accept:
            accept_vals = [x.content_to_int() for x in accept]
            text += ': accept ' + ', '.join(map(coap.ContentFormat.to_str, accept_vals))

        return text


class Lwm2mDiscover(CoapGet):
    @staticmethod
    def _pkt_matches(pkt: coap.Packet):
        return (CoapGet._pkt_matches(pkt)
                and is_lwm2m_nonempty_path(pkt.get_uri_path())
                and is_link_format(pkt))

    def __init__(self,
                 path: str or Lwm2mPath,
                 msg_id: int = ANY,
                 token: EscapedBytes = ANY,
                 options: List[coap.Option] = ANY):
        super().__init__(path=path,
                         msg_id=msg_id,
                         token=token,
                         accept=coap.Option.ACCEPT.APPLICATION_LINK,
                         options=options)

    def summary(self):
        return 'Discover ' + self.get_full_uri()


class Lwm2mWrite(Lwm2mMsg):
    @staticmethod
    def _pkt_matches(pkt: coap.Packet):
        return (pkt.type in (None, coap.Type.CONFIRMABLE)
                and pkt.code in (coap.Code.REQ_PUT, coap.Code.REQ_POST)
                and is_lwm2m_nonempty_path(pkt.get_uri_path())
                and not is_link_format(pkt))

    def __init__(self,
                 path: str or Lwm2mNonemptyPath,
                 content: EscapedBytes,
                 format: coap.ContentFormatOption = coap.ContentFormat.TEXT_PLAIN,
                 update: bool = False,
                 msg_id: int = ANY,
                 token: EscapedBytes = ANY,
                 options: List[coap.Option] = ANY):

        if isinstance(path, str):
            path = Lwm2mNonemptyPath(path)
        if isinstance(content, str):
            content = bytes(content, 'ascii')

        if isinstance(format, int):
            format = coap.Option.CONTENT_FORMAT(format)

        super().__init__(type=coap.Type.CONFIRMABLE,
                         code=(coap.Code.REQ_POST if update else coap.Code.REQ_PUT),
                         msg_id=msg_id,
                         token=token,
                         options=concat_if_not_any(
                             path.to_uri_options(),
                             [format],
                             options),
                         content=content)

    def summary(self):
        fmt_vals = [x.content_to_int() for x in self.get_options(coap.Option.CONTENT_FORMAT)]
        fmt = ', '.join(map(coap.ContentFormat.to_str, fmt_vals))

        return ('Write%s %s: %s, %d bytes'
                % (' (update)' if self.code == coap.Code.REQ_POST else '',
                   self.get_full_uri(), fmt, len(self.content)))




class Lwm2mWriteAttributes(Lwm2mMsg):
    @staticmethod
    def _pkt_matches(pkt: coap.Packet):
        return (pkt.type in (None, coap.Type.CONFIRMABLE)
                and pkt.code == coap.Code.REQ_PUT
                and is_lwm2m_nonempty_path(pkt.get_uri_path())
                and pkt.get_content_format() is None)

    def __init__(self,
                 path: str or Lwm2mNonemptyPath,
                 lt: float = None,
                 gt: float = None,
                 st: float = None,
                 pmin: int = None,
                 pmax: int = None,
                 epmin: int = None,
                 epmax: int = None,
                 query: List[str] = None,
                 msg_id: int = ANY,
                 token: EscapedBytes = ANY,
                 options: List[coap.Option] = ANY):
        path, query = _split_string_path(path, query)

        if lt is not None:
            query.append('lt=%f' % (lt,))
        if gt is not None:
            query.append('gt=%f' % (gt,))
        if st is not None:
            query.append('st=%f' % (st,))
        if pmin is not None:
            query.append('pmin=%d' % (pmin,))
        if pmax is not None:
            query.append('pmax=%d' % (pmax,))
        if epmin is not None:
            query.append('epmin=%d' % (epmin,))
        if epmax is not None:
            query.append('epmax=%d' % (epmax,))

        super().__init__(type=coap.Type.CONFIRMABLE,
                         code=coap.Code.REQ_PUT,
                         msg_id=msg_id,
                         token=token,
                         options=concat_if_not_any(
                             path.to_uri_options(),
                             [coap.Option.URI_QUERY(x) for x in query],
                             options),
                         content=b'')

    def summary(self):
        attrs = ', '.join(x.content_to_str() for x in self.get_options(coap.Option.URI_QUERY))
        return 'Write Attributes %s: %s' % (self.get_full_uri(), attrs)


class Lwm2mExecute(Lwm2mMsg):
    @staticmethod
    def _pkt_matches(pkt: coap.Packet):
        return (pkt.type in (None, coap.Type.CONFIRMABLE)
                and pkt.code == coap.Code.REQ_POST
                and is_lwm2m_nonempty_path(pkt.get_full_uri())
                and pkt.get_content_format() is None)

    def __init__(self,
                 path: str or Lwm2mResourcePath,
                 content: EscapedBytes = b'',
                 msg_id: int = ANY,
                 token: EscapedBytes = ANY,
                 options: List[coap.Option] = ANY):
        if isinstance(path, str):
            path = Lwm2mResourcePath(path)

        super().__init__(type=coap.Type.CONFIRMABLE,
                         code=coap.Code.REQ_POST,
                         msg_id=msg_id,
                         token=token,
                         options=concat_if_not_any(path.to_uri_options(),
                                                   options),
                         content=content)

    def summary(self):
        return ('Execute %s: %s' % (self.get_full_uri(),
                                    self.content_summary()))


class Lwm2mCreate(Lwm2mMsg):
    @staticmethod
    def _pkt_matches(pkt: coap.Packet):
        return (pkt.type in (None, coap.Type.CONFIRMABLE)
                and pkt.code == coap.Code.REQ_POST
                and is_lwm2m_nonempty_path(pkt.get_uri_path())
                and pkt.get_content_format() == coap.ContentFormat.APPLICATION_LWM2M_TLV)

    def __init__(self,
                 path: str or Lwm2mObjectPath,
                 content: EscapedBytes = b'',
                 msg_id: int = ANY,
                 token: EscapedBytes = ANY,
                 options: List[coap.Option] = ANY,
                 format: coap.ContentFormatOption = coap.Option.CONTENT_FORMAT.APPLICATION_LWM2M_TLV):
        if isinstance(path, str):
            path = Lwm2mObjectPath(path)

        if isinstance(format, int):
            format = coap.Option.CONTENT_FORMAT(format)

        super().__init__(type=coap.Type.CONFIRMABLE,
                         code=coap.Code.REQ_POST,
                         msg_id=msg_id,
                         token=token,
                         options=concat_if_not_any(
                             path.to_uri_options(),
                             [format],
                             options),
                         content=content)

    def summary(self):
        return 'Create %s: %s' % (self.get_full_uri(), self.content_summary())


class Lwm2mDelete(Lwm2mMsg):
    @staticmethod
    def _pkt_matches(pkt: coap.Packet):
        # TODO: this should be done by checking the packet source/target
        # if REQ_DELETE is sent by server. it's Delete; otherwise - De-Register
        # assumption: De-Register will never be called on a path
        # that resembles /OID/IID
        return (pkt.type in (None, coap.Type.CONFIRMABLE)
                and pkt.code == coap.Code.REQ_DELETE
                and is_lwm2m_path(pkt.get_uri_path()))

    def __init__(self,
                 path: str or Lwm2mPath,
                 msg_id: int = ANY,
                 token: EscapedBytes = ANY,
                 options: List[coap.Option] = ANY):
        if isinstance(path, str):
            path = Lwm2mPath(path)
        if path.resource_id is not None:
            raise ValueError('LWM2M Resource path is not applicable to a Delete: %s' % (path,))

        super().__init__(type=coap.Type.CONFIRMABLE,
                         code=coap.Code.REQ_DELETE,
                         msg_id=msg_id,
                         token=token,
                         options=concat_if_not_any(path.to_uri_options(),
                                                   options))

    def summary(self):
        return 'Delete ' + self.get_full_uri()


# Classes defined below are responses that should be matched to some request.
# Therefeore, msg_id and token in the constructor are mandatory.

class Lwm2mContent(Lwm2mResponse):
    @staticmethod
    def _pkt_matches(pkt: coap.Packet):
        return pkt.code == coap.Code.RES_CONTENT

    def __init__(self,
                 msg_id: int,
                 token: EscapedBytes,
                 content: EscapedBytes = ANY,
                 format: coap.ContentFormatOption = ANY,
                 type: coap.Type = coap.Type.ACKNOWLEDGEMENT,
                 options: List[coap.Option] = ANY):
        if isinstance(format, int):
            format = coap.Option.CONTENT_FORMAT(format)

        all_opts = [format] if format is not ANY else ANY
        if options is not ANY:
            all_opts = (all_opts + options) if all_opts is not ANY else options

        super().__init__(type=type,
                         code=coap.Code.RES_CONTENT,
                         msg_id=msg_id,
                         token=token,
                         options=all_opts,
                         content=content)

    def make_content_summary(self):
        format_opts = self.get_options(coap.Option.CONTENT_FORMAT)
        if format_opts:
            fmt = format_opts[0]
        else:
            fmt = coap.Option.CONTENT_FORMAT.TEXT_PLAIN

        if fmt == coap.Option.CONTENT_FORMAT.TEXT_PLAIN:
            return self.content_summary()
        else:
            return '(%s; %d bytes)' % (fmt.content_to_str(), len(self.content))

    def summary(self):
        return 'Content ' + self.make_content_summary()


class Lwm2mNotify(Lwm2mContent):
    @staticmethod
    def _pkt_matches(pkt: coap.Packet):
        return (Lwm2mContent._pkt_matches(pkt)
                and pkt.get_options(coap.Option.OBSERVE))

    def __init__(self,
                 token: EscapedBytes,
                 content: EscapedBytes = ANY,
                 format: coap.ContentFormatOption = ANY,
                 confirmable: bool = False,
                 options: List[coap.Option] = ANY):
        if isinstance(format, int):
            format = coap.Option.CONTENT_FORMAT(format)

        super().__init__(type=coap.Type.CONFIRMABLE if confirmable else coap.Type.NON_CONFIRMABLE,
                         msg_id=ANY,
                         token=token,
                         content=content,
                         format=format,
                         options=options)

    def summary(self):
        observe_opts = self.get_options(coap.Option.OBSERVE)
        seq = '/'.join(str(opt.content_to_int()) for opt in observe_opts)
        return 'Notify (%s, seq %s, token %s) %s' % (
            str(self.type), seq, hexlify(self.token), self.make_content_summary())


class Lwm2mCreated(Lwm2mResponse):
    @staticmethod
    def _pkt_matches(pkt: coap.Packet):
        return pkt.code == coap.Code.RES_CREATED

    def __init__(self,
                 msg_id: int,
                 token: EscapedBytes,
                 location: str or CoapPath = ANY,
                 options: List[coap.Option] = ANY):
        if isinstance(location, str):
            location = CoapPath(location)

        super().__init__(type=coap.Type.ACKNOWLEDGEMENT,
                         code=coap.Code.RES_CREATED,
                         msg_id=msg_id,
                         token=token,
                         options=concat_if_not_any(
                             location.to_uri_options(opt=coap.Option.LOCATION_PATH),
                             options))

    def summary(self):
        location = self.get_location_path()
        return 'Created ' + (location or '(no location-path)')


class Lwm2mDeleted(Lwm2mResponse):
    @staticmethod
    def _pkt_matches(pkt: coap.Packet):
        return pkt.code == coap.Code.RES_DELETED

    def __init__(self,
                 msg_id: int,
                 token: EscapedBytes,
                 location: str or CoapPath = ANY,
                 options: List[coap.Option] = ANY):
        if location is not ANY and isinstance(location, str):
            location = CoapPath(location)

        super().__init__(type=coap.Type.ACKNOWLEDGEMENT,
                         code=coap.Code.RES_DELETED,
                         msg_id=msg_id,
                         token=token,
                         options=concat_if_not_any(
                             location.to_uri_options(coap.Option.LOCATION_PATH),
                             options))

    def summary(self):
        location = self.get_location_path()
        return 'Deleted ' + (location or '(no location-path)')


class Lwm2mChanged(Lwm2mResponse):
    @staticmethod
    def _pkt_matches(pkt: coap.Packet):
        return pkt.code == coap.Code.RES_CHANGED

    def __init__(self,
                 msg_id: int,
                 token: EscapedBytes,
                 location: str or CoapPath = ANY,
                 options: List[coap.Option] = ANY,
                 content: EscapedBytes = ANY):
        if location is not ANY and isinstance(location, str):
            location = CoapPath(location)

        super().__init__(type=coap.Type.ACKNOWLEDGEMENT,
                         code=coap.Code.RES_CHANGED,
                         msg_id=msg_id,
                         token=token,
                         options=concat_if_not_any(
                             location.to_uri_options(coap.Option.LOCATION_PATH),
                             options),
                         content=content)

    def summary(self):
        location = self.get_location_path()
        return 'Changed ' + (location or '(no location path)')


class Lwm2mErrorResponse(Lwm2mResponse):
    @staticmethod
    def _pkt_matches(pkt: coap.Packet):
        return (pkt.type in (None, coap.Type.ACKNOWLEDGEMENT)
                and pkt.code.cls in (4, 5))

    def __init__(self,
                 code: coap.Code,
                 msg_id: int,
                 token: EscapedBytes,
                 options: List[coap.Option] = ANY):
        if code.cls not in (4, 5):
            raise ValueError('Error responses must have code class 4 or 5')

        super().__init__(type=coap.Type.ACKNOWLEDGEMENT,
                         code=code,
                         msg_id=msg_id,
                         token=token,
                         options=options)

    def summary(self):
        content_str = shorten(
            hexlify_nonprintable(self.content)) if self.content else '(no details available)'
        return '%s: %s' % (str(self.code), content_str)


class Lwm2mEmpty(Lwm2mResponse):
    @staticmethod
    def _pkt_matches(pkt: coap.Packet):
        return (pkt.code == coap.Code.EMPTY
                and pkt.token == b''
                and pkt.options == []
                and pkt.content == b'')

    def __init__(self,
                 type: coap.Type = coap.Type.ACKNOWLEDGEMENT,
                 msg_id: int = ANY):
        super().__init__(type=type,
                         code=coap.Code.EMPTY,
                         msg_id=msg_id,
                         token=b'',
                         content=b'')

    def summary(self):
        return 'Empty %s, msg_id = %d' % (str(self.type), self.msg_id)


class Lwm2mReset(Lwm2mEmpty):
    @staticmethod
    def _pkt_matches(pkt: coap.Packet):
        return (Lwm2mEmpty._pkt_matches(pkt)
                and pkt.type == coap.Type.RESET)

    def __init__(self, msg_id: int = ANY):
        super().__init__(msg_id=msg_id, type=coap.Type.RESET)

    def summary(self):
        return 'Reset, msg_id = %d' % (self.msg_id,)


class Lwm2mContinue(Lwm2mResponse):
    @staticmethod
    def _pkt_matches(pkt: coap.Packet):
        return pkt.code == coap.Code.RES_CONTINUE

    def __init__(self,
                 msg_id: int,
                 token: EscapedBytes,
                 type: coap.Type = coap.Type.ACKNOWLEDGEMENT,
                 options: List[coap.Option] = ANY):
        super().__init__(type=type,
                         code=coap.Code.RES_CONTINUE,
                         msg_id=msg_id,
                         token=token,
                         options=options)

    def summary(self):
        return 'Continue, msg_id = %d, token = %s' \
            % (self.msg_id if self.msg_id is not None else 'None', self.token)


def _get_ordered_types_list():
    def _sequence_preserving_uniq(seq):
        seen = set()
        return [x for x in seq if not (x in seen or seen.add(x))]

    msg_subclasses = [v for v in sys.modules[__name__].__dict__.values()
                      if isinstance(v, type) and issubclass(v, Lwm2mMsg)]

    # for each Lwm2mMsg subclass, list the subclass and all its base classes
    # up to and including Lwm2mMsg
    # make sure that bases are before subclasses
    types = []
    for cls in msg_subclasses:
        types += [base for base in reversed(cls.mro())
                  if issubclass(base, Lwm2mMsg)]

    # leave only the first occurrence of every class
    # reverse the result so that subclasses are always first
    ordered_types = list(reversed(_sequence_preserving_uniq(types)))

    # sanity check: for any class in ORDERED_TYPES all its subclasses are
    # BEFORE it on the list
    for left_idx, left in enumerate(ordered_types):
        for right in ordered_types[left_idx + 1:]:
            assert not issubclass(right, left)

    return ordered_types


TYPES = _get_ordered_types_list()


def get_lwm2m_msg(pkt: coap.Packet):
    for t in TYPES:
        try:
            return t.from_packet(pkt)
        except TypeError:
            pass

    raise ValueError('should never happen')
