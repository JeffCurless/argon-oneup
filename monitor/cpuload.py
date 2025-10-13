#!/usr/bin/env python3
import os
import time

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
    load = CPULoad()
    print( f"Number of CPU's = {len(load)}" )
    while True:
        time.sleep( 1 )
        percentage : dict[str:float] = load.getPercentages()
        print( f"percentage: {percentage}" )
        for item in percentage:
            print( f"{item} : {percentage[item]:.02f}" )
