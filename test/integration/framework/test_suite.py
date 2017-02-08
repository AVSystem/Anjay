import os
import re
import sys
import unittest
import subprocess
import inspect
import time

from . import lwm2m
from .lwm2m.server import Lwm2mServer
from .lwm2m_test import *

from .asserts import Lwm2mAsserts

def read_until_match(fd, regex, timeout_s):
    import select
    out = ''
    rlist = [ fd ]
    xlist = [ fd ]

    start_time = time.time()
    while time.time() - start_time < timeout_s:
        match = re.search(regex, out)
        if match:
            return match
        r,w,x = select.select(rlist, [], xlist)
        if len(r) < 1: break
        buf = fd.read()
        out += buf

def test_case_name(test_filepath):
    import re
    test_file_name = os.path.basename(test_filepath)
    return re.sub(r'\.py$', '', test_file_name)

def ensure_dir(dir_path):
    try:
        os.makedirs(dir_path)
    except OSError:
        if not os.path.isdir(dir_path):
            raise

class Lwm2mDmOperations(Lwm2mAsserts):
    def _perform_action(self, server, request, expected_response):
        server.send(request)
        res = server.recv(timeout_s=2)
        self.assertMsgEqual(expected_response, res)
        return res

    def _make_expected_res(self, req, success_res_cls, expect_error_code):
        req.fill_placeholders()

        if expect_error_code is None:
            return success_res_cls.matching(req)()
        else:
            return Lwm2mErrorResponse.matching(req)(code=expect_error_code)

    def create_instance(self, server, oid, iid=None, expect_error_code=None):
        instance_tlv = None if iid is None else TLV.make_instance(instance_id=iid).serialize()
        req = Lwm2mCreate('/%d' % oid, instance_tlv)
        expected_res = self._make_expected_res(req, Lwm2mCreated, expect_error_code)
        return self._perform_action(server, req, expected_res)

    def delete_instance(self, server, oid, iid, expect_error_code=None):
        req = Lwm2mDelete('/%d/%d' % (oid, iid))
        expected_res = self._make_expected_res(req, Lwm2mDeleted, expect_error_code)
        return self._perform_action(server, req, expected_res)

    def _read_path(self, server, path, expect_error_code, accept=None):
        req = Lwm2mRead(path, accept=accept)
        expected_res = self._make_expected_res(req, Lwm2mContent, expect_error_code)
        return self._perform_action(server, req, expected_res)

    def read_resource(self, server, oid, iid, rid, expect_error_code=None, accept=None):
        return self._read_path(server, '/%d/%d/%d' % (oid, iid, rid), expect_error_code,
                               accept=accept)

    def read_instance(self, server, oid, iid, expect_error_code=None):
        return self._read_path(server, '/%d/%d' % (oid, iid), expect_error_code)

    def read_object(self, server, oid, expect_error_code=None):
        return self._read_path(server, '/%d' % oid, expect_error_code)

    def write_instance(self, server, oid, iid, content=b'', partial=False, expect_error_code=None):
        req = Lwm2mWrite('/%d/%d' % (oid, iid), content,
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV,
                         update=partial)
        expected_res = self._make_expected_res(req, Lwm2mChanged, expect_error_code)
        return self._perform_action(server, req, expected_res)

    def write_resource(self, server, oid, iid, rid, content=b'', partial=False,
                       format=coap.ContentFormat.APPLICATION_LWM2M_TLV,
                       expect_error_code=None):
        req = Lwm2mWrite('/%d/%d/%d' % (oid, iid, rid), content, format=format,
                         update=partial)
        expected_res = self._make_expected_res(req, Lwm2mChanged, expect_error_code)
        return self._perform_action(server, req, expected_res)

    def execute_resource(self, server, oid, iid, rid, expect_error_code=None):
        req = Lwm2mExecute('/%d/%d/%d' % (oid, iid, rid))
        expected_res = self._make_expected_res(req, Lwm2mChanged, expect_error_code)
        return self._perform_action(server, req, expected_res)

    @staticmethod
    def make_path(*args):
        def ensure_valid_path(args):
            import itertools
            valid_args = list(itertools.takewhile(lambda x: x is not None, list(args)))
            if not all(x is None for x in args[len(valid_args):]):
                raise AttributeError
            return valid_args

        return '/' + '/'.join(map(lambda arg: '%d' % arg, ensure_valid_path(list(args))))

    def discover(self, server, oid=None, iid=None, rid=None, expect_error_response=None):
        req = Lwm2mDiscover(self.make_path(oid, iid, rid))
        expected_res = self._make_expected_res(req, Lwm2mContent, expect_error_response)
        return self._perform_action(server, req, expected_res)

    def observe(self, server, oid=None, iid=None, rid=None, expect_error_code=None):
        req = Lwm2mObserve(Lwm2mDmOperations.make_path(oid, iid, rid))
        expected_res = self._make_expected_res(req, Lwm2mContent, expect_error_code)
        return self._perform_action(server, req, expected_res)

    def write_attributes(self, server, oid=None, iid=None, rid=None, query=[], expect_error_code=None):
        req = Lwm2mWriteAttributes(Lwm2mDmOperations.make_path(oid, iid, rid), query=query)
        expected_res = self._make_expected_res(req, Lwm2mChanged, expect_error_code)
        return self._perform_action(server, req, expected_res)

class Lwm2mTest(unittest.TestCase, Lwm2mAsserts):
    DEFAULT_MSG_TIMEOUT = 9000.0
    DEFAULT_COMM_TIMEOUT = 9000.0

    def __init__(self, test_method_name):
        super().__init__(test_method_name)

        self.servers = []
        self.bootstrap_server = None

    def setUp(self):
        pass

    @unittest.skip
    def runTest(self):
        raise NotImplementedError('runTest not implemented')

    def tearDown(self):
        pass

    def set_config(self, config):
        self.config = config

    def log_filename(self):
        return os.path.join(self.suite_name(), self.test_name() + '.log')

    def test_name(self):
        return self.__class__.__name__

    def suite_name(self):
        test_root = self.config.suite_root_path or os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        name = os.path.abspath(inspect.getfile(type(self)))

        if name.endswith('.py'):
            name = name[:-len('.py')]
        name = name[len(test_root):] if name.startswith(test_root) else name
        name = name.lstrip('/')
        return name

    def make_demo_args(self,
                       servers,
                       security_mode='nosec'):
        """
        Helper method for easy generation of demo executable arguments.
        """
        args = ['--security-mode', security_mode]

        for serv in servers:
            args += ['--server-uri', 'coap://127.0.0.1:%d' % (serv.get_listen_port(),)]

        return args

    def logs_path(self, log_type, log_root=None):
        dir_path = os.path.join(log_root or self.config.logs_path, log_type.lower())
        log_path = os.path.join(dir_path, self.log_filename())
        ensure_dir(os.path.dirname(log_path))
        return log_path

    def _get_valgrind_args(self):
        import shlex

        valgrind_list = []
        if 'VALGRIND' in os.environ and os.environ['VALGRIND']:
            valgrind_list = shlex.split(os.environ['VALGRIND'])
            valgrind_list += ['--log-file=' + self.logs_path('valgrind')]

        return valgrind_list

    def start_demo(self, cmdline_args, timeout_s=30):
        """
        Starts the demo executable with given CMDLINE_ARGS.
        """
        demo_executable = os.path.join(self.config.demo_path, self.config.demo_cmd)
        demo_args = self._get_valgrind_args() + [demo_executable] + cmdline_args

        console_log_path = self.logs_path('console')
        console = open(console_log_path, 'wb')
        console.write((' '.join(demo_args) + '\n\n').encode('utf-8'))
        console.flush()

        import subprocess
        self.demo_process = subprocess.Popen(demo_args,
                                             stdin=subprocess.PIPE,
                                             stdout=console,
                                             stderr=console,
                                             bufsize=1)
        self.demo_process.log_file_write = console
        self.demo_process.log_file_path = console_log_path
        self.demo_process.log_file = open(console_log_path, 'r')

        if timeout_s is not None:
            # wait until demo process starts
            if self.communicate('', timeout=timeout_s) is None:
                raise self.failureException('demo executable did not start in time')

    def setup_demo_with_servers(self,
                                num_servers=1,
                                bootstrap_server=False,
                                extra_cmdline_args=[],
                                auto_register=True,
                                version=DEMO_LWM2M_VERSION,
                                lifetime=None):
        """
        Starts the demo process with NUM_SERVERS regular pre-configured servers,
        accessible through self.servers array.

        If BOOTSTRAP_SERVER is true, self.bootstrap_server is initialized to
        an Lwm2mServer instance and the demo is executed with --bootstrap
        optiom.

        Any EXTRA_CMDLINE_ARGS are appended to the demo command line.

        If AUTO_REGISTER is True, the function waits until demo registers to
        all initialized regular servers.
        """
        demo_args = []

        self.servers = [Lwm2mServer() for _ in range(num_servers)]

        if bootstrap_server:
            self.bootstrap_server = Lwm2mServer()
            demo_args += ['--bootstrap']
            servers = [self.bootstrap_server] + self.servers
        else:
            servers = self.servers

        demo_args += self.make_demo_args(servers)
        demo_args += extra_cmdline_args

        self.start_demo(demo_args)

        if auto_register:
            for serv in self.servers:
                self.assertDemoRegisters(serv, version=version, lifetime=lifetime)

    def teardown_demo_with_servers(self, auto_deregister=True, *args, **kwargs):
        """
        Attempts to gracefully shut down the demo. If that doesn't work, demo
        is terminated forcefully. All servers from self.servers, as well as
        self.bootstrap_server are closed if they exist.

        If AUTO_DEREGISTER is True, the function ensures that demo correctly
        de-registers from all servers in self.servers list.
        """
        servers = []
        try:
            self.request_demo_shutdown(self.servers if auto_deregister else [], *args, **kwargs)
        finally:
            self.terminate_demo()
            for serv in self.servers:
                serv.close()

            if self.bootstrap_server:
                self.bootstrap_server.close()

    def communicate(self, cmd, timeout=-1, match_regex=re.escape('(DEMO)>')):
        """
        Writes CMD to the demo process stdin. If MATCH_REGEX is not None,
        blocks until given regex is found on demo process stdout.
        """
        if timeout < 0:
            timeout = self.DEFAULT_COMM_TIMEOUT

        self.demo_process.log_file.seek(0, os.SEEK_END)
        self.demo_process.stdin.write((cmd.strip('\n') + '\n').encode())
        self.demo_process.stdin.flush()

        if match_regex:
            return read_until_match(self.demo_process.log_file, match_regex, timeout_s=timeout)

        return None

    def _terminate_demo(self, demo):
        cleanup_actions = [
            (5.0,  lambda _: None), # check if the demo already stopped
            (5.0,  lambda demo: demo.terminate()),
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

    def terminate_demo(self):
        if self.demo_process is None:
            return

        exc = sys.exc_info()
        try:
            return_value = self._terminate_demo(self.demo_process)
            self.assertEqual(return_value, 0, 'demo terminated with nonzero exit code')
        except:
            if not exc[1]: raise
        finally:
            self.demo_process.log_file.close()
            self.demo_process.log_file_write.close()

    def request_demo_shutdown(self, deregister_servers=[], *args, **kwargs):
        """
        Attempts to cleanly terminate demo by closing its STDIN.

        If DEREGISTER_SERVERS is a non-empty list, the function waits until
        demo deregisters from each server from the list.
        """
        if self.demo_process is None:
            return

        self.demo_process.stdin.close()

        for serv in deregister_servers:
            self.assertDemoDeregisters(serv, *args, **kwargs)

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

    def setUp(self,
              auto_register=True,
              lifetime=None):
        extra_args = ['--lifetime', str(lifetime)] if lifetime else []

        self.setup_demo_with_servers(num_servers=1,
                                     extra_cmdline_args=extra_args,
                                     auto_register=auto_register,
                                     lifetime=lifetime)

    def tearDown(self, *args, **kwargs):
        self.teardown_demo_with_servers(*args, **kwargs)
