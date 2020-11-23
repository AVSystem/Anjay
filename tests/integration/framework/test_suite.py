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

import enum
import inspect
import os
import re
import shutil
import subprocess
import threading
import time
import unittest
import logging
from typing import TypeVar

from .asserts import Lwm2mAsserts
from .lwm2m_test import *
from framework.lwm2m.coap.transport import Transport

try:
    import dpkt

    _DPKT_AVAILABLE = True
except ImportError:
    _DPKT_AVAILABLE = False

T = TypeVar('T')


class LogType(enum.Enum):
    Console = 'console'
    Valgrind = 'valgrind'
    Pcap = 'pcap'

    def extension(self):
        if self == LogType.Pcap:
            return '.pcapng'
        else:
            return '.log'


def read_some_with_timeout(fd, timeout_s):
    import select
    deadline = time.time() + timeout_s
    while True:
        partial_timeout = deadline - time.time()
        if partial_timeout < 0:
            return b''
        r, w, x = select.select([fd], [], [fd], partial_timeout)
        if len(r) > 0 or len(x) > 0:
            buf = fd.read(65536)
            if buf is not None and len(buf) > 0:
                return buf


def ensure_dir(dir_path):
    try:
        os.makedirs(dir_path)
    except OSError:
        if not os.path.isdir(dir_path):
            raise


class CleanupList(list):
    def __call__(self):
        def merge_exceptions(old, new):
            """
            Adds the "old" exception as a context of the "new" one and returns
            the "new" one.

            If the "new" exception already has a context, the "old" one is added
            at the end of the chain, as the context of the innermost exception
            that does not have a context.

            When the returned exception is rethrown, it will be logged by the
            standard Python exception formatter as something like:

                Exception: old exception

                During handling of the above exception, another exception occurred:

                Traceback (most recent call last):
                  ...
                Exception: new exception

            :param old: "old" exception
            :param new: "new" exception
            :return: "new" exception with updated context information
            """
            tmp = new
            while tmp.__context__ is not None:
                if tmp.__context__ is old:
                    return new
                tmp = tmp.__context__
            tmp.__context__ = old
            return new

        exc = None
        for cleanup_func in self:
            try:
                cleanup_func()
            except Exception as e:
                exc = merge_exceptions(exc, e)

        if exc is not None:
            raise exc

    def __enter__(self):
        return self

    def __exit__(self, _type, value, _traceback):
        return self()


class Lwm2mDmOperations(Lwm2mAsserts):
    DEFAULT_OPERATION_TIMEOUT_S = 5

    def _perform_action(self, server, request, expected_response, timeout_s=None):
        server.send(request)
        if timeout_s is None:
            timeout_s = self.DEFAULT_OPERATION_TIMEOUT_S
        res = server.recv(timeout_s=timeout_s)
        self.assertMsgEqual(expected_response, res)
        return res

    def _make_expected_res(self, req, success_res_cls, expect_error_code):
        req.fill_placeholders()

        if expect_error_code is None:
            return success_res_cls.matching(req)()
        else:
            return Lwm2mErrorResponse.matching(req)(code=expect_error_code)

    def create_instance_with_arbitrary_payload(self, server, oid,
                                               format=coap.ContentFormat.APPLICATION_LWM2M_TLV,
                                               iid=None, payload=b'', expect_error_code=None,
                                               **kwargs):
        if iid is None:
            raise ValueError("IID cannot be None")

        req = Lwm2mCreate(path='/%d' % oid, content=payload, format=format)
        expected_res = self._make_expected_res(
            req, Lwm2mCreated, expect_error_code)
        return self._perform_action(server, req, expected_res, **kwargs)

    def create_instance_with_payload(self, server, oid, iid=None, payload=b'',
                                     expect_error_code=None, **kwargs):
        if iid is None:
            raise ValueError("IID cannot be None")

        instance_tlv = TLV.make_instance(
            instance_id=iid, content=payload).serialize()
        return self.create_instance_with_arbitrary_payload(server=server, oid=oid, iid=iid,
                                                           payload=instance_tlv,
                                                           expect_error_code=expect_error_code,
                                                           **kwargs)

    def create_instance(self, server, oid, iid=None, expect_error_code=None, **kwargs):
        instance_tlv = None if iid is None else TLV.make_instance(
            instance_id=iid).serialize()
        req = Lwm2mCreate('/%d' % oid, instance_tlv)
        expected_res = self._make_expected_res(
            req, Lwm2mCreated, expect_error_code)
        return self._perform_action(server, req, expected_res, **kwargs)

    def create(self, server, path, expect_error_code=None, **kwargs):
        req = Lwm2mCreate(Lwm2mPath(path), None)
        expected_res = self._make_expected_res(
            req, Lwm2mCreated, expect_error_code)
        return self._perform_action(server, req, expected_res, **kwargs)

    def delete_instance(self, server, oid, iid, expect_error_code=None, **kwargs):
        req = Lwm2mDelete('/%d/%d' % (oid, iid))
        expected_res = self._make_expected_res(
            req, Lwm2mDeleted, expect_error_code)
        return self._perform_action(server, req, expected_res, **kwargs)

    def read_path(self, server, path, expect_error_code=None, accept=None, **kwargs):
        req = Lwm2mRead(path, accept=accept)
        expected_res = self._make_expected_res(
            req, Lwm2mContent, expect_error_code)
        return self._perform_action(server, req, expected_res, **kwargs)

    def read_resource(self, server, oid, iid, rid, expect_error_code=None, accept=None, **kwargs):
        return self.read_path(server, '/%d/%d/%d' % (oid, iid, rid), expect_error_code,
                              accept=accept, **kwargs)

    def read_instance(self, server, oid, iid, expect_error_code=None, accept=None, **kwargs):
        return self.read_path(server, '/%d/%d' % (oid, iid), expect_error_code,
                              accept=accept, **kwargs)

    def read_object(self, server, oid, expect_error_code=None, accept=None, **kwargs):
        return self.read_path(server, '/%d' % oid, expect_error_code, accept=accept, **kwargs)

    def write_object(self, server, oid, content=b'', expect_error_code=None,
                     format=coap.ContentFormat.APPLICATION_LWM2M_TLV, **kwargs):
        req = Lwm2mWrite('/%d' % (oid,), content, format=format)
        expected_res = self._make_expected_res(
            req, Lwm2mChanged, expect_error_code)
        return self._perform_action(server, req, expected_res, **kwargs)

    def write_instance(self, server, oid, iid, content=b'', partial=False, expect_error_code=None,
                       format=coap.ContentFormat.APPLICATION_LWM2M_TLV, **kwargs):
        req = Lwm2mWrite('/%d/%d' % (oid, iid), content,
                         format=format,
                         update=partial)
        expected_res = self._make_expected_res(
            req, Lwm2mChanged, expect_error_code)
        return self._perform_action(server, req, expected_res, **kwargs)

    def write_resource(self, server, oid, iid, rid, content=b'', partial=False,
                       format=coap.ContentFormat.TEXT_PLAIN,
                       expect_error_code=None, **kwargs):
        req = Lwm2mWrite('/%d/%d/%d' % (oid, iid, rid), content, format=format,
                         update=partial)
        expected_res = self._make_expected_res(
            req, Lwm2mChanged, expect_error_code)
        return self._perform_action(server, req, expected_res, **kwargs)

    def execute_resource(self, server, oid, iid, rid, content=b'', expect_error_code=None,
                         **kwargs):
        req = Lwm2mExecute('/%d/%d/%d' % (oid, iid, rid), content=content)
        expected_res = self._make_expected_res(
            req, Lwm2mChanged, expect_error_code)
        return self._perform_action(server, req, expected_res, **kwargs)

    @staticmethod
    def make_path(*args):
        def ensure_valid_path(args):
            import itertools
            valid_args = list(itertools.takewhile(
                lambda x: x is not None, list(args)))
            if not all(x is None for x in args[len(valid_args):]):
                raise AttributeError
            return valid_args

        return '/' + '/'.join(map(lambda arg: '%d' % arg, ensure_valid_path(list(args))))

    def discover(self, server, oid=None, iid=None, rid=None, expect_error_code=None, **kwargs):
        req = Lwm2mDiscover(self.make_path(oid, iid, rid))
        expected_res = self._make_expected_res(
            req, Lwm2mContent, expect_error_code)
        return self._perform_action(server, req, expected_res, **kwargs)

    def observe(self, server, oid=None, iid=None, rid=None, riid=None, expect_error_code=None,
                **kwargs):
        req = Lwm2mObserve(
            Lwm2mDmOperations.make_path(oid, iid, rid, riid), **kwargs)
        expected_res = self._make_expected_res(
            req, Lwm2mContent, expect_error_code)
        return self._perform_action(server, req, expected_res)

    def write_attributes(self, server, oid=None, iid=None, rid=None, query=[],
                         expect_error_code=None, **kwargs):
        req = Lwm2mWriteAttributes(
            Lwm2mDmOperations.make_path(oid, iid, rid), query=query)
        expected_res = self._make_expected_res(
            req, Lwm2mChanged, expect_error_code)
        return self._perform_action(server, req, expected_res, **kwargs)


class Lwm2mTest(unittest.TestCase, Lwm2mAsserts):
    DEFAULT_MSG_TIMEOUT = 9000.0
    DEFAULT_COMM_TIMEOUT = 9000.0

    def __init__(self, test_method_name):
        super().__init__(test_method_name)

        self.servers = []
        self.bootstrap_server = None

    def setUp(self, *args, **kwargs):
        self.setup_demo_with_servers(*args, **kwargs)

    @unittest.skip
    def runTest(self):
        raise NotImplementedError('runTest not implemented')

    def tearDown(self, *args, **kwargs):
        self.teardown_demo_with_servers(*args, **kwargs)

    def set_config(self, config):
        self.config = config

    def log_filename(self, extension='.log'):
        return os.path.join(self.suite_name(), self.test_name() + extension)

    def test_name(self):
        return self.__class__.__name__

    def suite_name(self):
        test_root = self.config.suite_root_path or os.path.dirname(
            os.path.dirname(os.path.abspath(__file__)))
        name = os.path.abspath(inspect.getfile(type(self)))

        if name.endswith('.py'):
            name = name[:-len('.py')]
        name = name[len(test_root):] if name.startswith(test_root) else name
        name = name.lstrip('/')
        return name.replace('/', '.')

    def make_demo_args(self,
                       endpoint_name,
                       servers,
                       fw_updated_marker_path,
                       ciphersuites=(0xC030, 0xC0A8, 0xC0AE)):
        """
        Helper method for easy generation of demo executable arguments.
        """
        # 0xC030 = TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384 - used by TLS (over TCP, including HTTPS) in tests
        # Default ciphersuites mandated by LwM2M:
        # 0xC0A8 = TLS_PSK_WITH_AES_128_CCM_8
        # 0xC0AE = TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8

        security_modes = set(serv.security_mode() for serv in servers)

        self.assertLessEqual(len(security_modes), 1,
                             'Attempted to mix security modes')

        security_mode = next(iter(security_modes), 'nosec')
        if security_mode == 'nosec':
            protocol = 'coap'
        else:
            protocol = 'coaps'

        args = ['--endpoint-name', endpoint_name,
                '--security-mode', security_mode]
        if fw_updated_marker_path is not None:
            args += ['--fw-updated-marker-path', fw_updated_marker_path]
        if ciphersuites is not None:
            args += ['--ciphersuites', ','.join(map(hex, ciphersuites))]

        for serv in servers:
            args += ['--server-uri', '%s://127.0.0.1:%d' %
                     (protocol, serv.get_listen_port(),)]

        return args

    def logs_path(self, log_type, log_root=None, **kwargs):
        assert type(log_type) == LogType

        dir_path = os.path.join(
            log_root or self.config.logs_path, log_type.value)
        log_path = os.path.join(dir_path, self.log_filename(
            **kwargs, extension=log_type.extension()))
        ensure_dir(os.path.dirname(log_path))
        return log_path

    def read_log_until_match(self, regex, timeout_s):
        deadline = time.time() + timeout_s
        out = bytearray()
        while True:
            # Retain only the last two lines - two, because the regexes sometimes check for the end-of-line
            last_lf = out.rfind(b'\n')
            if last_lf >= 0:
                second_to_last_lf = out.rfind(b'\n', 0, last_lf)
                if second_to_last_lf >= 0:
                    del out[0:second_to_last_lf + 1]

            partial_timeout = min(deadline - time.time(), 1.0)
            if partial_timeout < 0:
                return None
            out += read_some_with_timeout(self.demo_process.log_file, partial_timeout)

            match = re.search(regex, out)
            if match:
                return match

            if self.demo_process.poll() is not None:
                return None

    def _get_valgrind_args(self):
        import shlex

        valgrind_list = []
        if 'VALGRIND' in os.environ and os.environ['VALGRIND']:
            valgrind_list = shlex.split(os.environ['VALGRIND'])
            valgrind_list += ['--log-file=' + self.logs_path(LogType.Valgrind)]

        return valgrind_list

    def _start_demo(self, cmdline_args, timeout_s=30, prepend_args=None):
        """
        Starts the demo executable with given CMDLINE_ARGS.
        """
        demo_executable = os.path.join(
            self.config.demo_path, self.config.demo_cmd)

        def is_file_executable(file_path):
            return os.path.isfile(file_path) and os.access(file_path, os.X_OK)

        if not is_file_executable(demo_executable):
            print('ERROR: %s is NOT executable' % (demo_executable,), file=sys.stderr)
            sys.exit(-1)

        args_prefix = []
        if (os.environ.get('RR')
                or ('RRR' in os.environ
                    and test_or_suite_matches_query_regex(self, os.environ['RRR']))):
            logging.info('*** rr-recording enabled ***')
            # ignore valgrind if rr was requested
            args_prefix = ['rr', 'record']
        else:
            args_prefix = self._get_valgrind_args()

        demo_args = (prepend_args or []) + args_prefix + [demo_executable] + cmdline_args

        import shlex
        console_log_path = self.logs_path(LogType.Console)
        console = open(console_log_path, 'w')
        console.write(
            (' '.join(map(shlex.quote, demo_args)) + '\n\n'))
        console.flush()

        logging.debug('starting demo: %s', ' '.join(
            '"%s"' % arg for arg in demo_args))
        import subprocess
        self.demo_process = subprocess.Popen(demo_args,
                                             stdin=subprocess.PIPE,
                                             stdout=console,
                                             stderr=console,
                                             bufsize=0)
        self.demo_process.log_file_write = console
        self.demo_process.log_file_path = console_log_path
        self.demo_process.log_file = open(
            console_log_path, mode='rb', buffering=0)

        if timeout_s is not None:
            # wait until demo process starts
            if self.read_log_until_match(regex=re.escape(b'*** ANJAY DEMO STARTUP FINISHED ***'),
                                         timeout_s=timeout_s) is None:
                raise self.failureException(
                    'demo executable did not start in time')

    DUMPCAP_COMMAND = 'dumpcap'

    @staticmethod
    def dumpcap_available():
        return not os.getenv('NO_DUMPCAP') and shutil.which(Lwm2mTest.DUMPCAP_COMMAND) is not None

    def _start_dumpcap(self, udp_ports):
        self.dumpcap_process = None
        if not self.dumpcap_available():
            return

        udp_ports = list(udp_ports)

        def _filter_expr():
            """
            Generates a pcap_compile()-compatible filter program so that dumpcap will only capture packets that are
            actually relevant to the current tests.

            Captured packets will include:
            - UDP datagrams sent or received on any of the udp_ports
            - ICMP Port Unreachable messages generated in response to a UDP datagram sent or received on any of the
              udp_ports
            """
            if len(udp_ports) == 0:
                return ''

            # filter expression for "source or destination UDP port is any of udp_ports"
            udp_filter = ' or '.join('(udp port %s)' % (port,)
                                     for port in udp_ports)

            # below is the generation of filter expression for the ICMP messages
            #
            # note that icmp[N] syntax accesses Nth byte since the beginning of ICMP header
            # and icmp[N:M] syntax accesses M-byte value starting at icmp[N]
            # - icmp[0] - ICMP types; 3 ~ Destination Unreachable
            # - icmp[1] - ICMP code; for Destination Unreachable: 3 ~ Destination port unreachable
            # - icmp[8] is the first byte of the IP header of copy of the packet that caused the error
            #   - icmp[17] is the IP protocol number; 17 ~ UDP
            #   - IPv4 header is normally 20 bytes long (we don't anticipate options), so UDP header starts at icmp[28]
            #   - icmp[28:2] is the source UDP port of the original packet
            #   - icmp[30:2] is the destination UDP port of the original packet
            icmp_pu_filter = ' or '.join(
                '(icmp[28:2] = 0x%04x) or (icmp[30:2] = 0x%04x)' % (port, port) for port in
                udp_ports)
            return '%s or ((icmp[0] = 3) and (icmp[1] = 3) and (icmp[17] = 17) and (%s))' % (
                udp_filter, icmp_pu_filter)

        self.dumpcap_file_path = self.logs_path(LogType.Pcap)
        dumpcap_command = [self.DUMPCAP_COMMAND, '-w',
                           self.dumpcap_file_path, '-i', 'lo', '-f', _filter_expr()]
        self.dumpcap_process = subprocess.Popen(dumpcap_command,
                                                stdin=subprocess.DEVNULL,
                                                stdout=subprocess.DEVNULL,
                                                stderr=subprocess.PIPE,
                                                bufsize=0)

        # It takes a little while (around 0.5-0.6 seconds on a normal PC) for dumpcap to initialize and actually start
        # capturing packets. We want all relevant packets captured, so we need to wait until dumpcap reports it's ready.
        # Also, if we haven't done this, there would be a possibility that _terminate_dumpcap() would be called before
        # full initialization of dumpcap - it would then essentially ignore the SIGTERM and our test would hang waiting
        # for dumpcap's termination that would never come.
        dumpcap_stderr = bytearray(b'')
        MAX_DUMCAP_STARTUP_WAIT_S = 30
        deadline = time.time() + MAX_DUMCAP_STARTUP_WAIT_S
        while time.time() < deadline:
            dumpcap_stderr += read_some_with_timeout(
                self.dumpcap_process.stderr, 1)
            if b'File:' in dumpcap_stderr:
                break
            if self.dumpcap_process.poll() is not None:
                raise ChildProcessError(
                    'Could not start %r\n' % (dumpcap_command,))
        else:
            raise ChildProcessError(
                'Could not start %r\n' % (dumpcap_command,))

        def _reader_func():
            try:
                while True:
                    data = self.dumpcap_process.stderr.read()
                    if len(data) == 0:  # EOF
                        break
            except:
                pass

        self.dumpcap_stderr_reader_thread = threading.Thread(
            target=_reader_func)
        self.dumpcap_stderr_reader_thread.start()

    def setup_demo_with_servers(self,
                                servers=1,
                                num_servers_passed=None,
                                bootstrap_server=False,
                                legacy_server_initiated_bootstrap_allowed=True,
                                extra_cmdline_args=[],
                                auto_register=True,
                                endpoint_name=DEMO_ENDPOINT_NAME,
                                lifetime=None,
                                binding=None,
                                fw_updated_marker_path=None,
                                **kwargs):
        """
        Starts the demo process and creates any required auxiliary objects (such as Lwm2mServer objects) or processes.

        :param servers:
        Lwm2mServer objects that shall be accessible to the test - they will be accessible through the self.servers
        list. May be either an iterable of Lwm2mServer objects, or an integer - in the latter case, an appropriate
        number of Lwm2mServer objects will be created.

        :param num_servers_passed:
        If passed, it shall be an integer that controls how many of the servers configured through the servers argument,
        will be passed to demo's command line. All of them are passed by default. This option may be useful if some
        servers are meant to be later configured e.g. via the Bootstrap Interface.

        :param bootstrap_server:
        Boolean value that controls whether to create a Bootstrap Server Lwm2mServer object. If true, it will be stored
        in self.bootstrap_server. The bootstrap server is not included in anything related to the servers and
        num_servers_passed arguments.

        :param extra_cmdline_args:
        List of command line arguments to pass to the demo process in addition to the ones generated from other
        arguments.

        :param auto_register:
        If true (default), self.assertDemoRegisters() will be called for each server provisioned via the command line.

        :param version:
        Passed down to self.assertDemoRegisters() if auto_register is true

        :param lifetime:
        Passed down to self.assertDemoRegisters() if auto_register is true

        :param binding:
        Passed down to self.assertDemoRegisters() if auto_register is true

        :return: None
        """
        demo_args = []

        if isinstance(servers, int):
            self.servers = [Lwm2mServer() for _ in range(servers)]
        else:
            self.servers = list(servers)

        servers_passed = self.servers
        if num_servers_passed is not None:
            servers_passed = servers_passed[:num_servers_passed]

        if bootstrap_server is True:
            self.bootstrap_server = Lwm2mServer()
        elif bootstrap_server:
            self.bootstrap_server = bootstrap_server
        else:
            self.bootstrap_server = None

        if self.bootstrap_server is not None:
            demo_args += [
                '--bootstrap' if legacy_server_initiated_bootstrap_allowed else '--bootstrap=client-initiated-only']
            all_servers = [self.bootstrap_server] + self.servers
            all_servers_passed = [self.bootstrap_server] + servers_passed
        else:
            all_servers = self.servers
            all_servers_passed = servers_passed

        if fw_updated_marker_path is None:
            fw_updated_marker_path = generate_temp_filename(
                dir='/tmp', prefix='anjay-fw-updated-')

        demo_args += self.make_demo_args(
            endpoint_name, all_servers_passed,
            fw_updated_marker_path, **kwargs)
        demo_args += extra_cmdline_args
        if lifetime is not None:
            demo_args += ['--lifetime', str(lifetime)]

        try:
            self._start_dumpcap(server.get_listen_port()
                                for server in all_servers)

            self._start_demo(demo_args)

            if auto_register:
                for serv in servers_passed:
                    if serv.security_mode() != 'nosec':
                        serv.listen()
                for serv in servers_passed:
                    self.assertDemoRegisters(serv,
                                             lifetime=lifetime,
                                             binding=binding)
        except Exception:
            try:
                self.teardown_demo_with_servers(auto_deregister=False)
            finally:
                raise

    def teardown_demo_with_servers(self,
                                   auto_deregister=True,
                                   shutdown_timeout_s=5.0,
                                   force_kill=False,
                                   *args,
                                   **kwargs):
        """
        Shuts down the demo process, either by:
        - closing its standard input ("Ctrl+D" on its command line)
        - sending SIGTERM to it
        - sending SIGKILL to it
        Each of the above methods is tried one after another.

        :param auto_deregister:
        If true (default), self.assertDemoDeregisters() is called before shutting down for each server in the
        self.servers list (unless overridden by the deregister_servers argument).

        :param shutdown_timeout_s:
        Number of seconds to wait after each attempted method of shutting down the demo process before moving to the
        next one (close input -> SIGTERM -> SIGKILL).

        :param force_kill:
        If set to True, demo will be forcefully terminated, and its exit code will be ignored.

        :param deregister_servers:
        If auto_deregister is true, specifies the list of servers to call self.assertDemoDeregisters() on, overriding
        the default self.servers.

        :param args:
        Any other positional arguments to this function are passed down to self.assertDemoDeregisters().

        :param kwargs:
        Any other keyword arguments to this function are passed down to self.assertDemoDeregisters().

        :return: None
        """
        if auto_deregister and not 'deregister_servers' in kwargs:
            kwargs = kwargs.copy()
            kwargs['deregister_servers'] = self.servers

        with CleanupList() as cleanup_funcs:
            if not force_kill:
                cleanup_funcs.append(
                    lambda: self.request_demo_shutdown(*args, **kwargs))

            cleanup_funcs.append(lambda: self._terminate_demo(
                timeout_s=shutdown_timeout_s, force_kill=force_kill))
            for serv in self.servers:
                cleanup_funcs.append(serv.close)

            if self.bootstrap_server:
                cleanup_funcs.append(self.bootstrap_server.close)

            cleanup_funcs.append(self._terminate_dumpcap)

    def seek_demo_log_to_end(self):
        self.demo_process.log_file.seek(
            os.fstat(self.demo_process.log_file.fileno()).st_size)

    def communicate(self, cmd, timeout=-1, match_regex=re.escape('(DEMO)>')):
        """
        Writes CMD to the demo process stdin. If MATCH_REGEX is not None,
        blocks until given regex is found on demo process stdout.
        """
        if timeout < 0:
            timeout = self.DEFAULT_COMM_TIMEOUT

        self.seek_demo_log_to_end()
        self.demo_process.stdin.write((cmd.strip('\n') + '\n').encode())
        self.demo_process.stdin.flush()

        if match_regex:
            result = self.read_log_until_match(match_regex.encode(), timeout_s=timeout)
            if result is not None:
                # we need to convert bytes-based match object to string-based one...
                return re.search(match_regex, result.group(0).decode(errors='replace'))

        return None

    def _terminate_demo_impl(self, demo, timeout_s, force_kill):
        if force_kill:
            demo.kill()
            demo.wait(timeout_s)
            return 0

        cleanup_actions = [
            (timeout_s, lambda _: None),  # check if the demo already stopped
            (timeout_s, lambda demo: demo.terminate()),
            (None, lambda demo: demo.kill())
        ]

        for timeout, action in cleanup_actions:
            action(demo)
            try:
                return demo.wait(timeout)
            except subprocess.TimeoutExpired:
                pass
            else:
                break
        return -1

    def _terminate_demo(self, timeout_s=5.0, force_kill=False):
        if self.demo_process is None:
            return

        exc = sys.exc_info()
        try:
            return_value = self._terminate_demo_impl(
                self.demo_process, timeout_s, force_kill)
            self.assertEqual(
                return_value, 0, 'demo terminated with nonzero exit code')
        except:
            if not exc[1]:
                raise
        finally:
            self.demo_process.log_file.close()
            self.demo_process.log_file_write.close()

    def _terminate_dumpcap(self):
        if self.dumpcap_process is None:
            logging.debug('dumpcap not started, skipping')
            return

        # wait until all packets are written
        last_size = -1
        size = 0

        MAX_DUMCAP_SHUTDOWN_WAIT_S = 30
        deadline = time.time() + MAX_DUMCAP_SHUTDOWN_WAIT_S
        while time.time() < deadline:
            if size != last_size:
                break
            time.sleep(0.5)
            last_size = size
            size = os.stat(self.dumpcap_file_path).st_size
        else:
            logging.warn(
                'dumpcap did not shut down on time, terminating anyway')

        self.dumpcap_process.terminate()
        self.dumpcap_process.wait()
        self.dumpcap_stderr_reader_thread.join()
        logging.debug('dumpcap terminated')

    def request_demo_shutdown(self, deregister_servers=[], *args, **kwargs):
        """
        Attempts to cleanly terminate demo by closing its STDIN.

        If DEREGISTER_SERVERS is a non-empty list, the function waits until
        demo deregisters from each server from the list.
        """
        logging.debug('requesting clean demo shutdown')
        if self.demo_process is None:
            logging.debug('demo not started, skipping')
            return

        self.demo_process.stdin.close()

        for serv in deregister_servers:
            self.assertDemoDeregisters(serv, reset=False, *args, **kwargs)

        logging.debug('demo terminated')

    def get_socket_count(self):
        return int(
            self.communicate('socket-count', match_regex='SOCKET_COUNT==([0-9]+)\n').group(1))

    def wait_until_socket_count(self, expected, timeout_s):
        deadline = time.time() + timeout_s
        while self.get_socket_count() != expected:
            if time.time() > deadline:
                raise TimeoutError('Desired socket count not reached')
            time.sleep(0.1)

    def get_non_lwm2m_socket_count(self):
        return int(self.communicate('non-lwm2m-socket-count',
                                    match_regex='NON_LWM2M_SOCKET_COUNT==([0-9]+)\n').group(1))

    def get_demo_port(self, server_index=None):
        if server_index is None:
            server_index = -1
        return int(
            self.communicate('get-port %s' % (server_index,), match_regex='PORT==([0-9]+)\n').group(
                1))

    def get_transport(self, socket_index=-1):
        return self.communicate('get-transport %s' % (socket_index,),
                                match_regex='TRANSPORT==([0-9a-zA-Z]+)\n').group(1)

    def get_all_connections_failed(self):
        return bool(int(self.communicate('get-all-connections-failed',
                                         match_regex='ALL_CONNECTIONS_FAILED==([0-9])\n').group(1)))

    def ongoing_registration_exists(self):
        result = self.communicate('ongoing-registration-exists',
                                  match_regex='ONGOING_REGISTRATION==(true|false)\n').group(1)
        if result == "true":
            return True
        elif result == "false":
            return False
        raise ValueError("invalid value")


class SingleServerAccessor:
    @property
    def serv(self) -> Lwm2mServer:
        return self.servers[0]

    @serv.setter
    def serv(self, new_serv: Lwm2mServer):
        self.servers[0] = new_serv

    @serv.deleter
    def serv(self):
        del self.servers[0]


class Lwm2mSingleServerTest(Lwm2mTest, SingleServerAccessor):
    def runTest(self):
        pass

    def setUp(self, extra_cmdline_args=None, psk_identity=None, psk_key=None, client_ca_path=None,
              client_ca_file=None, server_crt_file=None, server_key_file=None, binding=None,
              *args, **kwargs):
        assert ((psk_identity is None) == (psk_key is None))
        extra_args = []
        dtls_server_kwargs = {}
        if 'ciphersuites' in kwargs:
            dtls_server_kwargs['ciphersuites'] = kwargs['ciphersuites']
        if psk_identity:
            extra_args += ['--identity', str(binascii.hexlify(psk_identity), 'ascii'),
                           '--key', str(binascii.hexlify(psk_key), 'ascii')]
            coap_server = coap.DtlsServer(psk_identity=psk_identity, psk_key=psk_key,
                                          **dtls_server_kwargs)
        elif server_crt_file:
            coap_server = coap.DtlsServer(ca_path=client_ca_path, ca_file=client_ca_file,
                                          crt_file=server_crt_file, key_file=server_key_file,
                                          **dtls_server_kwargs)
        else:
            coap_server = coap.Server()
        if extra_cmdline_args is not None:
            extra_args += extra_cmdline_args

        if 'servers' not in kwargs:
            kwargs['servers'] = [Lwm2mServer(coap_server)]

        self.setup_demo_with_servers(extra_cmdline_args=extra_args,
                                     binding=binding,
                                     *args,
                                     **kwargs)

    def tearDown(self, *args, **kwargs):
        self.teardown_demo_with_servers(*args, **kwargs)


class Lwm2mDtlsSingleServerTest(Lwm2mSingleServerTest):
    PSK_IDENTITY = b'test-identity'
    PSK_KEY = b'test-key'

    def setUp(self, *args, **kwargs):
        super().setUp(psk_identity=self.PSK_IDENTITY, psk_key=self.PSK_KEY, *args, **kwargs)


# This class **MUST** be specified as the first in superclass list, due to Python's method resolution order
# (see https://www.python-course.eu/python3_multiple_inheritance.php) and the fact that not all setUp() methods
# call super().setUp(). Failure to fulfill this requirement may lead to "make check" failing on systems
# without dpkt or dumpcap available.
class PcapEnabledTest(Lwm2mTest):
    def setUp(self, *args, **kwargs):
        if not (_DPKT_AVAILABLE and Lwm2mTest.dumpcap_available()):
            raise unittest.SkipTest('This test involves parsing PCAP file')
        return super().setUp(*args, **kwargs)

    def read_pcap(self):
        def decode_packet(data):
            # dumpcap captures contain Ethernet frames on Linux and
            # loopback ones on BSD
            for frame_type in [dpkt.ethernet.Ethernet, dpkt.loopback.Loopback]:
                pkt = frame_type(data)
                if isinstance(pkt.data, dpkt.ip.IP):
                    return pkt

            raise ValueError('Could not decode frame: %s' % pkt.hex())

        with open(self.dumpcap_file_path, 'rb') as f:
            r = dpkt.pcapng.Reader(f)
            for pkt in iter(r):
                yield decode_packet(pkt[1]).data

    def _wait_until_condition(self, timeout_s, step_s, condition: lambda pkts: True):
        if timeout_s is None:
            timeout_s = self.DEFAULT_MSG_TIMEOUT
        deadline = time.time() + timeout_s
        while True:
            if condition(self.read_pcap()):
                return
            if time.time() >= deadline:
                raise TimeoutError(
                    'Condition was not true in specified time interval')
            time.sleep(step_s)

    def _count_packets(self, condition: lambda pkts: True):
        result = 0
        for pkt in self.read_pcap():
            if condition(pkt):
                result += 1
        return result

    @staticmethod
    def is_icmp_unreachable(pkt):
        return isinstance(pkt, dpkt.ip.IP) \
               and isinstance(pkt.data, dpkt.icmp.ICMP) \
               and isinstance(pkt.data.data, dpkt.icmp.ICMP.Unreach)

    @staticmethod
    def is_dtls_client_hello(pkt):
        header = b'\x16'  # Content Type: Handshake
        header += b'\xfe\xfd'  # Version: DTLS 1.2
        header += b'\x00\x00'  # Epoch: 0
        if isinstance(pkt, dpkt.ip.IP) and isinstance(pkt.data, dpkt.udp.UDP):
            return pkt.udp.data[:len(header)] == header
        else:
            return False

    @staticmethod
    def is_nosec_register(pkt):
        try:
            # If it successfully parses as Lwm2mRegister it is a register
            Lwm2mRegister.from_packet(coap.Packet.parse(pkt.data.data))
            return True
        except:
            return False

    def count_nosec_register_packets(self):
        return self._count_packets(PcapEnabledTest.is_nosec_register)

    def count_icmp_unreachable_packets(self):
        return self._count_packets(PcapEnabledTest.is_icmp_unreachable)

    def count_dtls_client_hello_packets(self):
        return self._count_packets(PcapEnabledTest.is_dtls_client_hello)

    def wait_until_icmp_unreachable_count(self, value, timeout_s=None, step_s=0.1):
        def count_of_icmps_is_expected(pkts):
            return self.count_icmp_unreachable_packets() >= value

        try:
            self._wait_until_condition(
                timeout_s=timeout_s, step_s=step_s, condition=count_of_icmps_is_expected)
        except TimeoutError:
            raise TimeoutError('ICMP Unreachable packet not generated')


def get_test_name(test):
    if isinstance(test, Lwm2mTest):
        return test.test_name()
    return test.id()


def get_full_test_name(test):
    if isinstance(test, Lwm2mTest):
        return test.suite_name() + '.' + test.test_name()
    return test.id()


def get_suite_name(suite):
    suite_names = []
    for test in suite:
        if isinstance(test, Lwm2mTest):
            suite_names.append(test.suite_name())
        elif isinstance(test, unittest.TestSuite):
            suite_names.append(get_suite_name(test))
        else:
            suite_names.append(test.id())

    suite_names = set(suite_names)
    assert len(suite_names) == 1

    return next(iter(suite_names)).replace('/', '.')


def test_or_suite_matches_query_regex(test_or_suite, query_regex):
    """
    Test or test suite matches regex query when at least one of following
    matches the regex:

    * test name,
    * suite name,
    * "suite_name.test_name" string.

    Substring matches are allowed unless the regex is anchored using ^ or $.
    """
    if isinstance(test_or_suite, unittest.TestCase):
        return (re.search(query_regex, get_test_name(test_or_suite))
                or re.search(query_regex, get_full_test_name(test_or_suite)))
    elif isinstance(test_or_suite, unittest.TestSuite):
        return re.search(query_regex, get_suite_name(test_or_suite))
    else:
        raise TypeError('Neither a test nor suite: %r' % test_or_suite)
