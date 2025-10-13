#!/usr/bin/env python3
import os
import time

class CPULoad:
    def __init__( self ):
        self._previousData = self._getRawData()
        self._names = []
        for item in self._previousData:
            self._names.append( item )
        
    def _getRawData( self ):
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

    def getPercentages( self ):
        results = {}
        current = self._getRawData()
        for item in current:
            total = current[item][0] - self._previousData[item][0]
            idle  = current[item][1] - self._previousData[item][1]
            percent = ((total - idle)/total) * 100
            results[item] = percent
        self._previousData = current
        return results
    
    @property
    def cpuNames( self ):
        return self._names
    
    def __len__(self):
        return len(self._previousData)

if __name__ == "__main__":
    load = CPULoad()
    print( f"Number of CPU's = {len(load)}" )
    while True:
        time.sleep( 1 )
        percentage = load.getPercentages()
        for item in percentage:
            print( f"{item} : {percentage[item]:.02f}" )
