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
        readMB = (float(data[0]) * 512.0) / (1024.0 * 1024.0)
        writeMB = (float(data[1]) * 512.0) / (1024.0 * 1024.0)
        return (readMB, writeMB )

    
if __name__ == "__main__":
    data = systemData()
    print( f"CPU Temp : {data.CPUTemperature}" )
    print( f"Fan Speed: {data.fanSpeed}" )
    print( f"NVME Temp: {data.driveTemp}" )
    print( f"Stats    : {data.driveStats}" )

