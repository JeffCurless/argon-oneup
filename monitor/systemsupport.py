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
        self._stats : list[int] = []
        self._device = device
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
            with open( f"/sys/block/{self._device}/stat", "r") as f:
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
    

class systemData:
    def __init__( self, drive : str = 'nvme0n1' ):
        self._drive   = drive
        self._cpuTemp = CPUTemperature()
        self._stats   = DriveStats( self._drive )

    @property
    def CPUTemperature(self) -> int:
        return self._cpuTemp.temperature

    @property
    def fanSpeed( self ) -> int:
        speed= 0
        try:
            command = os.popen( 'cat /sys/devices/platform/cooling_fan/hwmon/*/fan1_input' )
            speed = int( command.read().strip())
        except Exception as error:
            print( f"Could not determine fan speed, error {error}" )
        finally:
            command.close()

        return speed

    @property
    def driveTemp(self) -> float:
        smartOutRaw = ""
        cmd = f'sudo smartctl -A /dev/{self._drive}'
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
                    return float(parts[0])
            except IndexError:
                pass
        
        return float(0.0)

    @property
    def driveStats(self) -> tuple[float,float]:
        data = self._stats.readWriteSectors()
        readMB = (float(data[0]) * 512.0) #/ (1024.0 * 1024.0)
        writeMB = (float(data[1]) * 512.0) #/ (1024.0 * 1024.0)
        return (readMB, writeMB )

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
    def __init__( self ):
        self._previousData : dict[str,tuple] = self._getRawData()
        self._names : list[str] = []
        for item in self._previousData:
            self._names.append( item )
        
    def _getRawData( self ) -> dict[str : tuple]:
        '''
        Obtain the raw CPU data from the system (located in /prop/stat), and
        return just the cpu0 -> cpux values.  No assumption is made on the number of
        cpus.
        
        Returns:
            A dictionary is returned, the format is name = (total, idle).  The total
            time and idle time are use to determine the percent utilization of the system.
        '''
        result = {}
        with open( "/proc/stat", "r") as f:
            allLines = f.readlines()
            for line in allLines:
                cpu = line.replace('\t', ' ').strip().split()
                if (len(cpu[0]) > 3) and (cpu[0][:3] == "cpu"):
                    total = 0
                    idle  = 0
                    for i in range( 1, len(cpu)):
                        total += int(cpu[i])
                        if i == 4 or i == 5:
                            idle += int(cpu[i])
                    result[cpu[0]] = (total,idle)
        return result

    def getPercentages( self ) -> dict[ str : float ]:
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
        for item in current:
            total = current[item][0] - self._previousData[item][0]
            idle  = current[item][1] - self._previousData[item][1]
            percent = ((total - idle)/total) * 100
            results[item] = round(percent,2)
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
    data = systemData()
    print( f"CPU Temp : {data.CPUTemperature}" )
    print( f"Fan Speed: {data.fanSpeed}" )
    print( f"NVME Temp: {data.driveTemp}" )
    print( f"Stats    : {data.driveStats}" )
    
    load = CPULoad()
    print( f"Number of CPU's = {len(load)}" )
    for i in range(10):
        time.sleep( 1 )
        percentage : dict[str:float] = load.getPercentages()
        print( f"percentage: {percentage}" )
        for item in percentage:
            print( f"{item} : {percentage[item]:.02f}" )
    