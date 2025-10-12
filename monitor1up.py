#!/usr/bin/python3
#
# Setup environment and pull in all of the items we need from gpiozero.  The
# gpiozero library is the new one that supports Raspberry PI 5's (and I suspect
# will be new direction for all prior version the RPIi.)
#
from gpiozero import CPUTemperature
import time
import os

class DriveStat:
    '''
    DriveStat class - 
    
    This class gets the drive statistics from sysfs for the device passed
    in.  There are several statistics can can be obtained.  Note that since
    all of the data is pulled at the same time, it is upto the caller to 
    make sure all the stats needed are obtained at the same time.
    
    See: https://www.kernel.org/doc/html/latest/block/stat.html
    
    Parameters:
        device - the name of the device to track
    '''
    
    READ_IOS        = 0
    READ_MERGES     = 1
    READ_SECTORS    = 2
    READ_TICKS      = 3
    WRITE_IOS       = 4
    WRITE_MERGES    = 5
    WRITE_SECTORS   = 6
    WRITE_TICKS     = 7
    IN_FLIGHT       = 8
    IO_TICKS        = 9
    TIME_IN_QUEUE   = 10
    DISCARD_IOS     = 11
    DISCARD_MERGES  = 12
    DISCARD_SECTORS = 13
    DISCARD_TICS    = 14
    FLUSH_IOS       = 15
    FLUSH_TICKS     = 16
    
    def __init__( self, device:str ):
        self.last  = []
        self.stats = []
        self.device = device
        self._readStats()
    
    def _readStats( self ):
        '''
        Read the disk statistics.  The stored statics in sysfs are stored as a single file
        so that when the data is read, all of the stats correlate to the same time.  The data
        is from the time the device has come online.
        
        last and set to the old version of the data, and the latest data is stored in stats
        
        '''
        try:
            self.last = self.stats
            with open( f"/sys/block/{self.device}/stat", "r") as f:
                stats = f.readline().strip().split(" ")
                self.stats = [int(l) for l in stats if l]
        except Exception as e:
            print( f"Failure reading disk statistics for {device} error {e}" )
        
    def _getStats( self ) -> list[int]:
        '''
        Read the devices statistics from the device,and return it.
        
        Returns:
            An array containing all of the data colleected about the device.
        '''
        self._readStats()
        if self.last == []:
            data = self.stats[:]
        else:
            data = [ d-self.last[i] for i,d in enumerate( self.stats ) ]
        return data
        
    def readAllStats( self ) -> list[int]:
        '''
        read all of the drive statisics from sysfs for the device.
        
        Returns
            A list of all of the device stats
        '''
        return self._getStats()
        
    def readSectors( self )-> int:
        return self._getStats()[DriveStat.READ_SECTORS]
    
    def writeSectors( self ) -> int:
        return self._getStats()[DriveStat.WRITE_SECTORS]
        
    def discardSectors( self ) -> int:
        return self._getStats()[DriveStat.DISCARD_SECTORS]
        
    def readWriteSectors( self ) -> (int,int):
        data = self._getStats()
        return (data[DriveStat.READ_SECTORS],data[DriveStat.WRITE_SECTORS])
    

def setupTemperatureObject():
    '''
    Get a cpu temperature object, and set the min and max range.  

    When the ranges are set to the non-default values, if the temperature is
    less than min_temp we get 0, and when the temperature reaches the max we get
    a value of 1.  This value can be used directly as a duty cycle for the fan.

    Return:
        A CPU temperature object
    '''
    cpuTemp = None
    try:
        cpuTemp = CPUTemperature()
    except Exception as error:
        log.error( f"Error creating CPU temperature object, error is {error}" )

    return cpuTemp

def getFanSpeed() -> int:
    '''
    Obtain the speed of the fan attached to the CPU.  This is accomplished reading
    the information from sysfs.
    
    NOTE:  There is an assumption that the fan s hanging off of /hwmon/hwmon3.  This may
    or may not be the case in all situations.
    '''
    fanSpeed = 0
    try:
        command = os.popen( 'cat /sys/devices/platform/cooling_fan/hwmon/*/fan1_input' )
        fanSpeed = command.read().strip()
    except:
        pass
    return int(fanSpeed)

def getNVMETemp(device : str) -> float:
    '''
    Obtain the temperature of the device passed in, using smartctl.
    
    Parameters :
        device - A string containing the device name.
        
    Returns:
        The temperature as a float
    '''
    smartOutRaw = ""
    try:
        command = os.popen( f'smartctl -A /dev/{device}' )
        smartOutRaw = command.read()
    except Exception as e:
        return 0
    finally:
        command.close()
        
    smartOut = [ l for l in smartOutRaw.split('\n') if l]
    for smartAttr in ["Temperature:","194","190"]:
        try:
            line = [l for l in smartOut if l.startswith(smartAttr)][0]
            parts = [p for p in line.replace('\t',' ').split(' ') if p]
            if smartAttr == "Temperature:":
                return float(parts[1])
            else:
                return float(parts[0])
        except IndexError:
            pass
        
    return float(0.0)
    
def argonsysinfo_kbstr(kbval, wholenumbers = True):
    remainder = 0
    suffixidx = 0
    suffixlist = ["B","KiB", "MiB", "GiB", "TiB"]
    while kbval > 1023 and suffixidx < len(suffixlist):
        remainder = kbval % 1024
        kbval     = kbval // 1024
        suffixidx = suffixidx + 1
    return f"{kbval} {suffixlist[suffixidx]}"

    
stats = DriveStat( 'nvme0n1' )
cpuTemp = setupTemperatureObject()

while True:
    os.system( "clear ")
    print( f"CPU  : {cpuTemp.temperature}" )
    print( f"Fan  : {getFanSpeed()}" )
    print( f"NVME : {getNVMETemp('nvme0n1')}" )
    data = stats.readWriteSectors()
    print( f"Read : {argonsysinfo_kbstr(data[0]*512)}/s" )
    print( f"Write: {argonsysinfo_kbstr(data[1]*512)}/s" )
    time.sleep(1) 
