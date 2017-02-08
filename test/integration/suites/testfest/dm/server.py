from framework.lwm2m_test import *

from .utils import DataModel, ValueValidator

class Test551_ServerObject_QueryingTheReadableResourcesOfObject(DataModel.Test):
    def setUp(self):
        super().setUp()

        # these Resources are not initialized by default
        # TODO: they probably should be
        self.test_write(ResPath.Server[1].DefaultMinPeriod, '1')
        self.test_write(ResPath.Server[1].DefaultMaxPeriod, '120')
        self.test_write(ResPath.Server[1].DisableTimeout, '120')

    def runTest(self):
        # A READ operation from server on the resource has been received by the
        # client. This test has to be run on the following resources:
        # a) Short server ID
        # b) Lifetime
        # c) Default minimum period
        # d) Default maximum period
        # e) Disable timeout
        # f) Notification storing when disabled or offline
        # g) Binding

        # 1. The server receives the status code “2.05” for READ success
        #    (done by test_read)
        # 2. The value returned by the client is admissible with regards of
        #    LWM2M technical specification
        self.test_read(ResPath.Server[1].ShortServerID,    ValueValidator.integer(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Server[1].Lifetime,         ValueValidator.integer(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Server[1].DefaultMinPeriod, ValueValidator.integer(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Server[1].DefaultMaxPeriod, ValueValidator.integer(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Server[1].DisableTimeout,   ValueValidator.integer(), coap.ContentFormat.TEXT_PLAIN)

        self.test_read(ResPath.Server[1].NotificationStoring, ValueValidator.boolean())
        # possible values as described in 5.3.1.1
        self.test_read(ResPath.Server[1].Binding,
                       ValueValidator.from_values(b'U', b'UQ', b'S', b'SQ', b'US', b'UQS'))

        # 3. The server receives the requested information and displays the
        #    resource value to the user


class Test555_ServerObject_SettingTheWritableResources(DataModel.Test):
    def runTest(self):
        # A WRITE operation from server on the resource has been received by the
        # client. This test has to be run for the following resources:
        # a) Lifetime
        # b) Default minimum period
        # c) Default maximum period
        # d) Disable timeout
        # e) Notification storing when disabled or offline
        # f) Binding

        # 1. The server receives the status code “2.04”
        # 2. The resource value has changed according to WRITE request
        # (both checked by test_write_validated)
        self.test_write_validated(ResPath.Server[1].Lifetime, '60')
        self.test_write_validated(ResPath.Server[1].DefaultMinPeriod, '5')
        self.test_write_validated(ResPath.Server[1].DefaultMaxPeriod, '15')
        self.test_write_validated(ResPath.Server[1].DisableTimeout, '120')
        self.test_write_validated(ResPath.Server[1].NotificationStoring, '1')

        self.test_write(ResPath.Server[1].Binding, 'UQ')
        self.assertDemoUpdatesRegistration(lifetime=60, binding='UQ') # triggered by Binding change
        self.assertEqual(b'UQ', self.test_read(ResPath.Server[1].Binding))

        # TODO: ugly workaround to avoid 93s delay in queue_exec; see T858
        self.test_write(ResPath.Server[1].Binding, 'U')
        self.assertDemoUpdatesRegistration(binding='U') # triggered by Binding change


class Test560_ServerObject_ObservationAndNotificationOfObservableResources(DataModel.Test):
    def setUp(self):
        super().setUp()

        # these Resources are not initialized by default
        # TODO: they probably should be
        self.test_write(ResPath.Server[1].DefaultMinPeriod, '1')
        self.test_write(ResPath.Server[1].DefaultMaxPeriod, '120')
        self.test_write(ResPath.Server[1].DisableTimeout, '120')

    def runTest(self):
        # The Server establishes an Observation relationship with the Client to
        # acquire condition notifications about observable resources. This test
        # has to be run for the following resources:
        # a) Short server ID
        # b) Lifetime
        # c) Default minimum period
        # d) Default maximum period
        # e) Disable timeout
        # f) Notification storing when disabled or offline
        # g) Binding

        # 1. The server has received the requested information and displays
        # the resource value to the user
        # 2. The value returned by the client is admissible with regards of
        # LWM2M technical specification
        # 3. The values returned by the client in each Notify are relevant
        # with regards to the threshold value, the min/max period and the
        # step
        self.test_observe(ResPath.Server[1].ShortServerID,    ValueValidator.integer())
        self.test_observe(ResPath.Server[1].Lifetime,         ValueValidator.integer())
        self.test_observe(ResPath.Server[1].DefaultMinPeriod, ValueValidator.integer())
        self.test_observe(ResPath.Server[1].DefaultMaxPeriod, ValueValidator.integer())
        self.test_observe(ResPath.Server[1].DisableTimeout,   ValueValidator.integer())

        self.test_observe(ResPath.Server[1].NotificationStoring, ValueValidator.boolean())
        self.test_observe(ResPath.Server[1].Binding,
                          ValueValidator.from_values(b'U', b'UQ', b'S', b'SQ', b'US', b'UQS'))


