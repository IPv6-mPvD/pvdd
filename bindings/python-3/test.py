
import pvddsync

def printPvdAttributes(pvddsync, pvd, attrs):
    print("Async", pvd, "attributes :", attrs)

def printPvdList(pvddsync, pvdList):
    print("Async pvd list : ", pvdList)
    for pvd in pvdList:
        print("Attributes for", pvd, ": ", pvddsync.getSyncAttributes(pvd))
        pvddsync.getAttributes(pvd)

pvdd = pvddsync.pvddsync(TO = 0.5)
pvdd.on("pvdList", printPvdList)
pvdd.on("pvdAttributes", printPvdAttributes)

pvdd.connect(autoReconnect = True, verbose = False)

pvdd.getList()

pvdList = pvdd.getSyncList()

print("Get sync pvd list :", pvdList)

for pvd in pvdList:
    print("Name/sequence number for", pvd, ":",
            pvdd.getSyncAttribute(pvd, "name"), "/",
            pvdd.getSyncAttribute(pvd, "sequenceNumber"))

from time import sleep
sleep(1)

pvdd.leave()

print("pvdd =", pvdd)

pvdd = None

print("pvdd =", pvdd)

sleep(1)
