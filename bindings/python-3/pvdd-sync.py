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

class pvddsync(pvdd.pvdd):
    def __init__(self, TO = None):
        super().__init__()

        # We also create a hidden pvdd connection for synchronous
        # requests/replies pattern
        self.syncPvd = pvdd.pvdd()
        self.myqueue = queue.Queue()
        self.TO = TO

    def syncResult(self, *args):
        self.myqueue.put(args)

    def connect(self, **args):
        super().connect(**args)
        self.syncPvd.on("pvdList", self.syncResult)
        self.syncPvd.on("pvdAttributes", self.syncResult)
        self.syncPvd.on("pvdAttribute", self.syncResult)
        self.syncPvd.connect(**args)

    def disconnect(self):
        super().disconnect()
        self.syncPvd.disconnect()

    def getTO(self, TO):
        return self.TO if TO == None else TO
    
    def getQueue(self, TO):
        try:
            return self.myqueue.get(True, TO)
        except Exception:
            return None

    def getSyncList(self, TO = None):
        self.syncPvd.getList()
        pvdList = self.getQueue(self.getTO(TO))
        if pvdList != None:
            _, pvdList = pvdList
        return pvdList

    def getSyncAttributes(self, pvd, TO = None):
        self.syncPvd.getAttributes(pvd)
        pvdAttributes = self.getQueue(self.getTO(TO))
        if pvdAttributes != None:
            _, pvdname, pvdAttributes = pvdAttributes
        return pvdAttributes

    def getSyncAttribute(self, pvd, attrName, TO = None):
        self.syncPvd.getAttribute(pvd, attrName)
        attrValue = self.getQueue(self.getTO(TO))
        if attrValue != None:
            _, pvdname, attrName, attrValue = attrValue
        return attrValue

if __name__ == "__main__":
    def printArgs(pvdd, args):
        print(args)

    pvddCnx = pvddsync(TO = 0.05)
    pvddCnx.connect(autoReconnect = True)
    pvddCnx.on("pvdList", printArgs)
    pvdList = pvddCnx.getSyncList(TO = 0.1)
    print("List of pvds : ", pvdList)
    pvddCnx.getList()
    if pvdList != None:
        for pvd in pvdList:
            print("Attributes for", pvd, ":", pvddCnx.getSyncAttributes(pvd))
            print("Addresses for", pvd, ":", pvddCnx.getSyncAttribute(pvd, "addresses"))

    pvddCnx.disconnect()
