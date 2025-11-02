#!/usr/bin/python3
#
# Setup environment and pull in all of the items we need from gpiozero.  The
# gpiozero library is the new one that supports Raspberry PI 5's (and I suspect
# will be new direction for all prior version the RPIi.)
#
from gpiozero import CPUTemperature
import time
import os

class DriveStats:
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
        self._last : list[int]  = []
        self._device = device
        self._stats : list[int] = []
        self._readStats()
    
    def _readStats( self ):
        '''
        Read the disk statistics.  The stored statics in sysfs are stored as a single file
        so that when the data is read, all of the stats correlate to the same time.  The data
        is from the time the device has come online.
        
        last and set to the old version of the data, and the latest data is stored in stats
        
        '''
        try:
            self._last = self._stats
            with open( f"/sys/block/{self._device}/stat", "r", encoding="utf8") as f:
                curStats = f.readline().strip().split(" ")
                self._stats = [int(l) for l in curStats if l]
        except Exception as e:
            print( f"Failure reading disk statistics for {self._device} error {e}" )
        
    def _getStats( self ) -> list[int]:
        '''
        Read the devices statistics from the device,and return it.
        
        Returns:
            An array containing all of the data colleected about the device.
        '''
        curData : list[int] = []
        
        self._readStats()
        if self._last == []:
            curData = self._stats[:]
        else:
            curData = [ d-self._last[i] for i,d in enumerate( self._stats ) ]
        return curData

    @property
    def name(self) -> str:
        return self._device
    
    def readAllStats( self ) -> list[int]:
        '''
        read all of the drive statisics from sysfs for the device.
        
        Returns
            A list of all of the device stats
        '''
        return self._getStats()
        
    def readSectors( self )-> int:
        return self._getStats()[DriveStats.READ_SECTORS]
    
    def writeSectors( self ) -> int:
        return self._getStats()[DriveStats.WRITE_SECTORS]
        
    def discardSectors( self ) -> int:
        return self._getStats()[DriveStats.DISCARD_SECTORS]
        
    def readWriteSectors( self ) -> tuple[int,int]:
        curData = self._getStats()
        return (curData[DriveStats.READ_SECTORS],curData[DriveStats.WRITE_SECTORS])
    
    def readWriteBytes( self ) -> tuple[int,int]:
        curData = self._getStats()
        return (curData[DriveStats.READ_SECTORS]*512,curData[DriveStats.WRITE_SECTORS]*512)

class multiDriveStat():
    '''
    This class allow for monitoring multiple drives at the same time. There are
    two mechanisms used to create this class.  The first is with no parameters,
    in this case, the system will automatically grab all of the drives and setup
    to monitor them.
    
    The second method is to provide this class with a list of drives to monitor.
    In this cased all of the drives that are NOT on that list are filtered out and
    only the drives left will be processed.
    
    If there is a missing drive from the filter, that drive is eliminated.
    
    '''
    def __init__(self,driveIgnoreList : list[str]=[]):
        #
        # Get all drives
        #
        self._drives = []
        with os.popen( 'ls -1 /sys/block | grep -v -e loop -e ram') as command:
            lsblk_raw = command.read()
            for l in lsblk_raw.split('\n'):
                if len(l) == 0:
                    continue
                if not l in driveIgnoreList:
                    self._drives.append( l )
        self._stats = [ DriveStats(_) for _ in self._drives ]
            
    @property
    def drives(self) -> list[str]:
        '''
        This attribute is used to list all of the drives that are being monitored
        
        Returns:
            A list of drives
        '''
        return self._drives
    
    def driveSize( self, _drive ) -> int:
        '''
        This function is called to obtain the size of the drive requested.
        
        Parameters:
            _drive - the drive to lookup
            
        Returns:
            The size in bytes, or 0 if the drive does not exist
        '''
        try:
            byteCount = 0
            with os.popen(f'cat /sys/block/{_drive}/size') as command:
                sectorCount = command.read().strip()
                byteCount = int(sectorCount) * 512
            return byteCount
        except:
            return 0
        
    def driveTemp(self,_drive) -> float:
        smartOutRaw = ""
        cmd = f'sudo smartctl -A /dev/{_drive}'
        try:
            command = os.popen( cmd )
            smartOutRaw = command.read()
        except Exception as error:
            print( f"Could not launch {cmd} error is {error}" )
            return 0.0
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
                    return float(parts[9])
            except IndexError:
                pass
        
        return float(0.0)
        
    def readWriteSectors( self )-> dict[str,tuple[int,int]]:
        '''
        Obtain the number of sectors read and written since the last
        time this function as called.
        
        Returns:
             A dictionary of the data, they key is the drive name, the value is the
             read/write tuple.
        '''
        curData = {}
        for _ in self._stats:
            curData[_.name] = _.readWriteSectors()
        return curData
    
    def readWriteBytes( self ) -> dict[str,tuple[int,int]]:
        '''
        Just like the readWriteSectors function but returns the data in Bytes
        
        '''
        curData = {}
        for _ in self._stats:
            curData[_.name] = _.readWriteBytes()
        return curData
   
class CPUInfo:
    '''
    This class deals with getting data about a Raspberry PI CPU
    
    '''
    def __init__( self ):
        self._cputemp = CPUTemperature()
        
    @property
    def temperature( self ) -> float:
        '''
        Obtain the temperature of the CPU.  This utilizes a GPIO call to obtain
        the CPU temp, via the CPUTemperature object from gpiozero
        
        Returns:
            A floating point number represetning the temperature in degrees C
        '''
        return self._cputemp.temperature
    
    @property
    def CPUFanSpeed( self ) -> float:
        '''
        Obtain the speed of the CPU fan.  This is based on monitoring the hardware
        monitor, assuming that fan1_input is the fan connected to the CPU.
        
        Return:
            The fanspeed as a floating point number
        '''
        speed= 0
        try:
            command = os.popen( 'cat /sys/devices/platform/cooling_fan/hwmon/*/fan1_input' )
            speed = int( command.read().strip())
        except Exception as error:
            print( f"Could not determine fan speed, error {error}" )
        finally:
            command.close()

        return float(speed)

class CPULoad:
    '''
    A class to help with obtaining the CPU load of the system. If there is more information
    needed, we can add to this.
    
    Note:
       This code automatically attempts to load the data from the system to initialize the
       object with names, and an initial set of data.
       
       This may result in th first actual call return some not very consistent values, for
       the time period being observed, but that difference is minimal.  In otherwords if we
       the period of time being measured is 1 second, and it's been a minute since this class
       was initialized, the first period reported will be CPU load over the minute, not 1 second,
       and the second period reported will be for a second...
       
       This is usually not an issue.
    '''
    def __init__( self ) -> None:
        #
        # Get the current data
        #
        self._previousData : dict[str,tuple[int,int]] = self._getRawData()
        self._names : list[str] = []
        self._cputemp : float = CPUTemperature()
        
        #
        # For each CPU, reset the total and idle amount, and create the list
        # of names
        #
        for _item in self._previousData:
            self._previousData[_item] = (0,0)
            self._names.append(_item)
        
    def _getRawData( self ) -> dict[str,tuple[int,int]]:
        '''
        Obtain the raw CPU data from the system (located in /prop/stat), and
        return just the cpu0 -> cpux values.  No assumption is made on the number of
        cpus.
        
        Returns:
            A dictionary is returned, the format is name = (total, idle).  The total
            time and idle time are use to determine the percent utilization of the system.
        '''
        result = {}
        with open("/proc/stat", "r",encoding="utf8") as f:
            allLines = f.readlines()
            for line in allLines:
                cpu = line.replace('\t', ' ').strip().split()
                if (len(cpu[0]) > 3) and (cpu[0][:3] == "cpu"):
                    total = 0
                    idle  = 0
                    for _index in range( 1, len(cpu)):
                        total += int(cpu[_index])
                        if _index == 4 or _index == 5:
                            idle += int(cpu[_index])
                    result[cpu[0]] = (total,idle)
        return result

    def getPercentages( self ) -> dict[str,float]:
        '''
        Obtain the percent CPU utilization of the system for a period of time.
        
        This routine gets the current raw data from the system, and then performs
        a delta from the prior time this function was called.  This data is then run
        through the following equation:
             
             utilization = ((total - idle)/total) * 100
             
        If the snapshots are taken at relativy consistent intervals, the CPU
        utilization in percent, is reasonably lose to the actual percentage.
        
        Returns:
            A dictionary consisting of the name of the CPU, and a floating point
            number representing the current utilization of that CPU.
        '''
        results = {}
        current = self._getRawData()
        for _item in current:
            total = current[_item][0] - self._previousData[_item][0]
            idle  = current[_item][1] - self._previousData[_item][1]
            percent = ((total - idle)/total) * 100
            results[_item] = round(percent,2)
        self._previousData = current
        return results
    
    @property
    def cpuNames( self ) -> list[str]:
        '''
        Get a list of CPU names from the system.
        
        Returns:
            a list of strings
        '''
        return self._names
    
    def __len__(self) -> int:
        '''
        handle getting the length (or count of CPU's).
        
        Returns:
            Number of CPU's
        '''
        return len(self._previousData)

    
if __name__ == "__main__":
    
    load = CPULoad()
    print( f"Number of CPU's = {len(load)}" )
    for i in range(3):
        time.sleep( 1 )
        percentage : dict[str,float] = load.getPercentages()
        print( f"percentage: {percentage}" )
        for item in percentage:
            print( f"{item} : {percentage[item]:.02f}" )
    
    cpuinfo = CPUInfo()
    print( f"CPU Temperature = {cpuinfo.temperature}" )
    print( f"CPU Fan Speed  = {cpuinfo.CPUFanSpeed}" )
    
    test = multiDriveStat()
    print( test.drives )
    for drive in test.drives:
        print( f"Drive {drive} size is {test.driveSize( drive )}" )
    print( test.readWriteSectors() )
    
