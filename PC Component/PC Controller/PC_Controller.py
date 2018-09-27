import threading
import audioStreamReceiver
import pimatrix
import time
import transcriptReceiver
from timeServer import TimeServer

deviceMan = pimatrix.deviceManager()
winPerfTimer = TimeServer()

def printMenu():
    print "\n"
    print "Welcome to Pi-Matrix Management Console"
    print "Device connected:", deviceMan.numDevices, "\n"
    print "1. Discover devices"
    if deviceMan.numDevices > 0:
        print "2. Connected devices' detail"
        print "----------------------------"
        if not deviceMan.deviceBusy:
            print "3. Record to disk"
        else:
            print "8. Stop all devices' current task"
        print "----------------------"
        print "9. Disconnect from all devices"
        print "0. Shutdown all devices"

while(True):
    printMenu()
    rawChoice = raw_input("Choice: ");
    if len(rawChoice) == 0:
        choice = 1
    else:
        choice = int(rawChoice)

    if choice == 1:
        print "Scanning......"
        deviceMan.discoverDevices()
        deviceMan.tabulateDevice()
        raw_input("Press Enter to continue...")     
    
    elif choice == 2:
        deviceMan.tabulateDevice()
        raw_input("Press Enter to continue...")

    elif choice == 3:
        digit = chr((int(time.clock())+2)%10+48)
        deviceMan.sendCommand("rec2sd", digit)

    elif choice == 8:
        deviceMan.sendCommand("stop")

    elif choice == 9:
        deviceMan.disconnectAll()
        #break
    
    elif choice == 0:
        if raw_input("Shutdown all devices? (y/n)") == "y":
            deviceMan.sendCommand("shutdown")
            break
        else:
            print "Abort"
    else:
        print "Not a valid choice"

print "\n\nThank you for using Pi-Matrix Management Console"
print "Have a nice day!\n"
winPerfTimer.stop()