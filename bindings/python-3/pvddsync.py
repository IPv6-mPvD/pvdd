#!/usr/bin/env pytnon3

#
#	Copyright 2017 Cisco
#
#	Licensed under the Apache License, Version 2.0 (the "License");
#	you may not use this file except in compliance with the License.
#	You may obtain a copy of the License at
#
#		http://www.apache.org/licenses/LICENSE-2.0
#
#	Unless required by applicable law or agreed to in writing, software
#	distributed under the License is distributed on an "AS IS" BASIS,
#	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#	See the License for the specific language governing permissions and
#	limitations under the License.
#

import pvdd
import queue
import threading

class pvddsync(pvdd.pvdd):
    def _delete(self):
        print("_delete called")

    def __init__(self, TO = None):
        super().__init__()

        # We also create a hidden pvdd connection for synchronous
        # requests/replies pattern
        self.syncPvd = pvdd.pvdd()
        self.syncPvd.on("pvdList", self._handlePvdList)
        self.syncPvd.on("pvdAttributes", self._handlePvdAttributes)
        self.syncPvd.on("pvdAttribute", self._handlePvdAttribute)
        self.condPvdList = threading.Condition()
        self.condAttributes = threading.Condition()
        self.condAttribute = threading.Condition()
        self.TO = TO

    def _handlePvdList(self, syncPvdd, pvdList):
        with self.condPvdList:
            self.pvdList = pvdList
            self.condPvdList.notify()

    def _handlePvdAttributes(self, syncPvdd, pvd, attributes):
        with self.condAttributes:
            self.pvdAttributes = (pvd, attributes)
            self.condAttributes.notify()

    def _handlePvdAttribute(self, syncPvdd, pvd, attrName, attrValue):
        with self.condAttribute:
            self.pvdAttribute = (pvd, attrName, attrValue)
            self.condAttribute.notify()

    def connect(self, **args):
        super().connect(**args)
        self.syncPvd.connect(**args)

    def disconnect(self):
        super().disconnect()
        self.syncPvd.disconnect()

    def leave(self):
        super().leave()
        self.syncPvd.leave()

    def getTO(self, TO):
        return self.TO if TO == None else TO

    def getSyncList(self, TO = None):
        with self.condPvdList:
            self.syncPvd.getList()
            self.pvdList = None
            self.condPvdList.wait(self.getTO(TO))

        return self.pvdList

    def getSyncAttributes(self, pvd, TO = None):
        with self.condAttributes:
            self.syncPvd.getAttributes(pvd)
            while True:
                rcvdAttrs = None
                self.pvdAttributes = None
                self.condAttributes.wait(self.getTO(TO))
                if self.pvdAttributes == None:  # timeout
                    break
                rcvdPvd, rcvdAttrs = self.pvdAttributes
                if rcvdPvd == pvd:
                    break

        return rcvdAttrs

    def getSyncAttribute(self, pvd, attrName, TO = None):
        with self.condAttribute:
            self.syncPvd.getAttribute(pvd, attrName)
            while True:
                rcvdAttr = None
                self.pvdAttribute = None
                self.condAttribute.wait(self.getTO(TO))
                if self.pvdAttribute == None:   # timeout
                    break
                rcvdPvd, rcvdAttrName, rcvdAttr = self.pvdAttribute
                if rcvdPvd == pvd and rcvdAttrName == attrName:
                    break

        return rcvdAttr

if __name__ == "__main__":
    def printArgs(pvdd, args):
        print("Asynchonous call :", args)

    pvddCnx = pvddsync(TO = 0.05)
    pvddCnx.connect(autoReconnect = True, verbose = False)
    pvddCnx.on("pvdList", printArgs)
    pvdList = pvddCnx.getSyncList(TO = 0.1)
    print("List of pvds : ", pvdList)
    pvddCnx.getList()   # asynchonous call
    if pvdList != None:
        for pvd in pvdList:
            print("Attributes for", pvd, ":", pvddCnx.getSyncAttributes(pvd))
            print("Addresses for", pvd, ":", pvddCnx.getSyncAttribute(pvd, "addresses", TO = 0.1))

    print("Attribute for foo.bar.com :", pvddCnx.getSyncAttributes("foo.bar.com", TO = 0.1))

    pvddCnx.leave()
