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

def noerr(closure, *args):
    try:
        return closure(*args)
    except Exception:
        return None

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

class eventEmitter:
    """Implements a basic event emitter """
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
                if closure(*args):
                    return True

class pvdd(threading.Thread):
    """Provides an object connected to the pvdd

Messages are received from the pvdd via a socket
An internal thread is started to read these messages
to parse them and to emit specific events

Typycal use :
def handleConnect(pvddCnx)
    print("Connected")
    pvddCnx.getList()

pvddCnx = pvdd.pvdd()
pvddCnx.on("connect", handleConnect)
pvddCnx.on("error", handleError)
pvddCnx.on("pvdList", handlePvdList)
...
pvddCnx.connect(autoReconnect = True)

    """

    def __init__(self):
        threading.Thread.__init__(self)
        self.multiLines = False
        self.fullMsg = None
        self.sock = None
        self.timer = None
        self.emitter = eventEmitter()
        self.m = threading.Lock()

    def emit(self, signal, *args):
        return self.emitter.notifyListeners(signal, self, *args)

    def handleMultiLine(self, msg):
        r = re.split("PVD_ATTRIBUTES +([^ \n]+)\n([\s\S]+)", msg)
        if len(r) == 4:
            attr = noerr(json.loads, r[2])
            if attr != None:
                self.emit("pvdAttributes", r[1], attr)
                if self.verbose:
                    print("Attributes for ", r[1], " = ", r[2])
            return

        r = re.split("PVD_ATTRIBUTE +([^ ]+) +([^ \n]+)\n([\s\S]+)", msg)
        if len(r) == 5:
            attr = noerr(json.loads, r[3])
            if attr != None:
                self.emit("pvdAttribute", r[1], r[2], attr)
                self.emit("on" + r[2], r[1], attr)

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
                        self.handleMultiLine(self.fullMsg)
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

            self.emit("pvdList", newListPvD)
            return

        r = re.split("PVD_NEW_PVD +([^ ]+)", msg)
        if len(r) == 3:
            self.emit("newPvd", r[1])
            return

        r = re.split("PVD_DEL_PVD +([^ ]+)", msg)
        if len(r) == 3:
            self.emit("delPvd", r[1])
            return

        r = re.split("PVD_ATTRIBUTES +([^ ]+) +(.+)", msg)
        if len(r) == 4:
            attr = noerr(json.loads, r[2])
            if attr != None:
                self.emit("pvdAttributes", r[1], attr)
                if self.verbose:
                    print("Attributes for ", r[1], " = ", r[2])
            return

        r = re.split("PVD_ATTRIBUTE +([^ ]+) +([^ ]+) +(.+)", msg)
        if len(r) == 5:
            attr = noerr(json.loads, r[3])
            if attr != None:
                self.emit("pvdAttribute", r[1], r[2], attr)
                self.emit("on" + r[2], r[1], attr)
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
                    self.emit("error", "connection broken")
                    if self.verbose:
                        print("Disconnected from pvdd")
                    self.sockclose()
                else:
                    data = data.decode('utf8', errors='strict')
                    for line in data.split("\n"):
                        if line != '':
                            self.emit("data", line)
                            self.handleOneLine(line)

    def sockwrite(self, msg):
        self.m.acquire()
        if self.sock != None:
            if self.sock.send(bytearray(msg, "utf-8")) == 0:
                self.emit("error", "connection broken")
                if self.verbose:
                    print("Disconnected from pvdd")
                self.sockclose(doLock = False)
        self.m.release()

    def createPvd(self, pvdName):
        self.sockwrite("PVD_CREATE_PVD 0 " + pvdName + "\n")

    def setAttribute(self, pvdName, attrName, attrValue):
            self.sockwrite("PVD_BEGIN_TRANSACTION " + pvdName + "\n" +
                  "PVD_BEGIN_MULTILINE\n" +
                  "PVD_SET_ATTRIBUTE " + pvdName + " " +
                                    attrName + "\n" +
                                    JSON.stringify(attrValue, null, 12) + "\n" +
                  "PVD_END_MULTILINE\n" +
                  "PVD_END_TRANSACTION " + pvdName + "\n")

    def unsetAttribute(self, pvdName, attrName):
        self.sockwrite("PVD_UNSET_ATTRIBUTE " + pvdName + " " + attrName + "\n")

    def getList(self):
        self.sockwrite("PVD_GET_LIST\n")

    def getAttributes(self, pvdName):
        self.sockwrite("PVD_GET_ATTRIBUTES " + pvdName + "\n")

    def getAttribute(self, pvdName, attrName):
        self.sockwrite("PVD_GET_ATTRIBUTE " + pvdName + " " + attrName + "\n")

    def subscribeAttribute(self, attrName):
        self.sockwrite("PVD_SUBSCRIBE " + attrName + "\n")

    def subscribeNotifications(self):
        self.sockwrite("PVD_SUBSCRIBE_NOTIFICATIONS\n")

    # internalConnection performs the connection with the pvdd daemon
    # It also attempt to reconnect every second in autoReconnect mode
    def internalConnection(self):
        if self.sock == None:
            try:
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.sock.connect(("localhost", self.port))
                self.start()    # background run() execution
                if self.controlConnection:
                    self.sockwrite("PVD_CONNECTION_PROMOTE_CONTROL\n")
                self.emit("connect")
                if self.verbose:
                    print("Connected with pvdd")
            except Exception as e:
                self.sockclose()
                self.emit("error", e)

        if self.sock == None and self.autoReconnect:
            self.timer = threading.Timer(1.0, self.internalConnection)
            self.timer.start()

    def connect(
            self,
            port = None,
            autoReconnect = False,
            controlConnection = False,
            verbose = False):

        if self.sock == None:
            self.port = int(setval(port, getenv("PVDD_PORT"), 10101))
            self.autoReconnect = autoReconnect
            self.controlConnection = controlConnection
            self.verbose = verbose

            self.internalConnection()

    def sockclose(self, doLock = True):
        if doLock:
            self.m.acquire()
        if self.sock != None:
            noerr(self.sock.shutdown, socket.SHUT_RDWR)    # force an error in run()
            noerr(self.sock.close)
            self.sock = None
        if doLock:
            self.m.release()

    def disconnect(self):
        self.sockclose()
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

    pvddCnx.connect(autoReconnect = True, verbose = False)

    from time import sleep
    sleep(5)

    pvddCnx.disconnect()
