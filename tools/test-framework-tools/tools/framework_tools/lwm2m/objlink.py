# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

class Objlink:
    def __init__(self, ObjID, ObjInstID):
        self.ObjID = ObjID
        self.ObjInstID = ObjInstID

    def __str__(self):
        return '%d:%d' % (self.ObjID, self.ObjInstID)
