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

import os
import threading
import socket
import re
import json

def getenv(v):
    if v in os.environ:
        return os.environ[v]
    else:
        return None

def setval(*arg):
    for v in arg:
        if v != None:
            return v
    return None

def getJson(s):
    try:
        return json.loads(s)
    except Exception:
        return None

class eventEmitter:
    def __init__(self):
        self.listeners = {}

    def addListener(self, signal, closure):
        if signal not in self.listeners:
            theListeners = []
        else:
            theListeners = self.listeners[signal]

        if closure not in theListeners:
            theListeners.append(closure)

        self.listeners[signal] = theListeners

    def delListener(self, signal, closure):
        if signal in self.listeners:
            theListeners = self.listeners[signal]
            if closure in theListeners:
                theListeners.remove(closure)

                if len(theListeners) == 0:
                    del(self.listeners[signal])
                else:
                    self.listeners[signal] = theListeners

    def notifyListeners(self, signal, *args):
        if signal in self.listeners:
            for closure in self.listeners[signal]:
                closure(*args)

class pvdd(threading.Thread):
    def __init__(self):
        threading.Thread.__init__(self)
        self.multiLines = False
        self.fullMsg = None
        self.sock = None
        self.timer = None
        self.emitter = eventEmitter()
        self.m = threading.Lock()

    def emit(self, signal, *args) :
        self.emitter.notifyListeners(signal, self, *args)

    def handleMultiLine(self, msg):
        r = re.split("PVD_ATTRIBUTES +([^ \n]+)\n([\s\S]+)", msg)
        if len(r) == 4:
            attr = getJson(r[2])
            if attr != None:
                pvdd.emit(self, "pvdAttributes", r[1], attr)
                if self.verbose:
                    print("Attributes for ", r[1], " = ", r[2])
            return

        r = re.split("PVD_ATTRIBUTE +([^ ]+) +([^ \n]+)\n([\s\S]+)", msg)
        if len(r) == 5:
            attr = getJson(r[3])
            if attr != None:
                pvdd.emit(self, "pvdAttribute", r[1], r[2], attr)
                pvdd.emit(self, "on" + r[2], r[1], attr)

    def handleOneLine(self, msg):
        # We check the beginning of a multi-lines section
        # before anything else, to reset the buffer in case
        # a previous multi-lines was improperly closed
        if msg == "PVD_BEGIN_MULTILINE":
                self.multiLines = True
                self.fullMsg = None
                return

        # End of a multi-lines section ?
        if msg == "PVD_END_MULTILINE":
                if self.fullMsg != None:
                        pvdd.handleMultiLine(self, self.fullMsg)
                self.multiLines  = False
                return

        # Are we in a mult-line section ?
        if self.multiLines:
                if self.fullMsg == None:
                    self.fullMsg = msg
                else:
                    self.fullMsg = self.fullMsg + "\n" + msg
                return

        # Single line messages
        r = re.split("PVD_LIST +(.*)", msg)
        if len(r) == 3:
            newListPvD = r[1].split(" ")
            if newListPvD == None:
                    newListPvD = []

            if self.verbose:
                print("pvd list =", newListPvD)

            pvdd.emit(self, "pvdList", newListPvD)
            return

        r = re.split("PVD_NEW_PVD +([^ ]+)", msg)
        if len(r) == 3:
            pvdd.emit(self, "newPvd", r[1])
            return

        r = re.split("PVD_DEL_PVD +([^ ]+)", msg)
        if len(r) == 3:
            pvdd.emit(self, "delPvd", r[1])
            return

        r = re.split("PVD_ATTRIBUTES +([^ ]+) +(.+)", msg)
        if len(r) == 4:
            attr = getJson(r[2])
            if attr != None:
                pvdd.emit(self, "pvdAttributes", r[1], attr)
                if self.verbose:
                    print("Attributes for ", r[1], " = ", r[2])
            return

        r = re.split("PVD_ATTRIBUTE +([^ ]+) +([^ ]+) +(.+)", msg)
        if len(r) == 5:
            attr = getJson(r[3])
            if attr != None:
                pvdd.emit(self, "pvdAttribute", r[1], r[2], attr)
                pvdd.emit(self, "on" + r[2], r[1], attr)
            return

        return

    def on(self, signal, closure):
        self.emitter.addListener(signal, closure)

    def off(self, signal, closure):
        self.emitter.delListener(signal, closure)

    def run(self):
        # Loop reading messages until the connection is closed
        while self.sock != None:
                data = self.sock.recv(4096)
                if data == b'':
                    pvdd.emit(self, "error", "connection broken")
                    if self.verbose:
                        print("Disconnected from pvdd")
                    pvdd.sockclose(self)
                else:
                    data = data.decode('utf8', errors='strict')
                    for line in data.split("\n"):
                        if line != '':
                            pvdd.emit(self, "data", line)
                            pvdd.handleOneLine(self, line)

    def sockwrite(self, msg):
        self.m.acquire()
        if self.sock != None:
            if self.sock.send(bytearray(msg, "utf-8")) == 0:
                pvdd.emit(self, "error", "connection broken")
                if self.verbose:
                    print("Disconnected from pvdd")
                pvdd.sockclose(True)
        self.m.release()

    def createPvd(self, pvdName):
        pvdd.sockwrite(self, "PVD_CREATE_PVD 0 " + pvdName + "\n")

    def setAttribute(self, pvdName, attrName, attrValue):
            pvdd.sockwrite(self, "PVD_BEGIN_TRANSACTION " + pvdName + "\n" +
                  "PVD_BEGIN_MULTILINE\n" +
                  "PVD_SET_ATTRIBUTE " + pvdName + " " +
                                    attrName + "\n" +
                                    JSON.stringify(attrValue, null, 12) + "\n" +
                  "PVD_END_MULTILINE\n" +
                  "PVD_END_TRANSACTION " + pvdName + "\n")

    def unsetAttribute(self, pvdName, attrName):
        pvdd.sockwrite(self, "PVD_UNSET_ATTRIBUTE " + pvdName + " " + attrName + "\n")

    def getList(self):
        pvdd.sockwrite(self, "PVD_GET_LIST\n")

    def getAttributes(self, pvdName):
        pvdd.sockwrite(self, "PVD_GET_ATTRIBUTES " + pvdName + "\n")

    def getAttribute(self, pvdName, attrName):
        pvdd.sockwrite(self, "PVD_GET_ATTRIBUTE " + pvdName + " " + attrName + "\n")

    def subscribeAttribute(self, attrName):
        pvdd.sockwrite(self, "PVD_SUBSCRIBE " + attrName + "\n")

    def subscribeNotifications(self):
        pvdd.sockwrite(self, "PVD_SUBSCRIBE_NOTIFICATIONS\n")

    # internalConnection performs the connection with the pvdd daemon
    # It also attempt to reconnect every second in autoReconnect mode
    def internalConnection(self):
        if self.sock == None:
            try:
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.sock.connect(("localhost", self.port))
            except Exception as e:
                pvdd.sockclose(self)
                pvdd.emit(self, "error", e)
                
            finally:
                self.start()    # background run() execution
                if self.controlConnection:
                    pvdd.sockwrite(self, "PVD_CONNECTION_PROMOTE_CONTROL\n")
                pvdd.emit(self, "connect")
                if self.verbose:
                    print("Connected with pvdd")

        if self.sock == None and self.autoReconnect:
            self.timer = threading.Timer(1.0, pvdd.internalConnection, self)
            self.timer.start()

    def connect(
            self,
            port = None,
            autoReconnect = False,
            controlConnection = False,
            verbose = False):

        if self.sock == None:
            self.port = setval(port, getenv("PVDD_PORT"), 10101)
            self.autoReconnect = autoReconnect
            self.controlConnection = controlConnection
            self.verbose = verbose

            pvdd.internalConnection(self)

    def sockclose(self, doLock = True):
        if doLock:
            self.m.acquire()
        if self.sock != None:
            self.sock.shutdown(socket.SHUT_RDWR)    # force an error in run()
            self.sock.close()
            self.sock = None
        if doLock:
            self.m.release()

    def disconnect(self):
        pvdd.sockclose(self, True)
        if self.timer != None:
            self.timer.cancel()
            self.timer = None


if __name__ == "__main__":
    def handleConnected(pvdd):
        print("Connected")
        pvdd.subscribeNotifications()
        pvdd.subscribeAttribute("*")
        pvdd.getList()

    def handleError(pvdd, e):
        print("Disconnection :", e)

    def handlePvdList(pvdd, pvdList):
        for pvd in pvdList:
            pvdd.getAttributes(pvd)
            pvdd.getAttribute(pvd, "name")
            pvdd.getAttribute(pvd, "hFlag")

    def handlePvdAttributes(pvdd, pvd, attrs):
        print("Attributes for", pvd, ":", json.dumps(attrs, indent = 4))

    def handlePvdAttribute(pvdd, pvd, attrName, attrValue):
        print("Attribute", attrName, "for", pvd, ":", json.dumps(attrValue, indent = 4))

    def handleNewPvd(pvdd, pvd):
        print("New pvd :", pvd)

    def handleDelPvd(pvdd, pvd):
        print(pvd, "vanishing")

    def handlePvdName(pvdd, pvd, pvdname):
        print("Field name for pvd", pvd, ":", pvdname)

    def handlePvdHFlag(pvdd, pvd, hFlag):
        print("Field hFlag for pvd", pvd, ":", hFlag)

    print("Standalone mode")
    pvddCnx = pvdd()
    pvddCnx.on("connect", handleConnected)
    pvddCnx.on("error", handleError)
    pvddCnx.on("pvdList", handlePvdList)
    pvddCnx.on("newPvd", handleNewPvd)
    pvddCnx.on("delPvd", handleDelPvd)
    pvddCnx.on("pvdAttributes", handlePvdAttributes)
    pvddCnx.on("pvdAttribute", handlePvdAttribute)
    pvddCnx.on("onname", handlePvdName)
    pvddCnx.on("onhFlag", handlePvdHFlag)

    pvddCnx.connect(autoReconnect = True)

    from time import sleep
    sleep(1)

    pvddCnx.disconnect()
