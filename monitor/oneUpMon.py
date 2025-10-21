#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Application that monitors current CPU and Drive temp, along with fan speed and IO utilization
Requires: PyQt5 (including QtCharts)

"""

import sys
from systemsupport import systemData, CPULoad, multiDriveStat

# --------------------------
# Globals
# --------------------------
sysdata = systemData()
cpuload = CPULoad()
multiDrive = multiDriveStat()

# --------------------------
# UI
# --------------------------

from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtGui import QPainter
from PyQt5.QtWidgets import QApplication, QMainWindow, QWidget, QGridLayout
from PyQt5.QtChart import QChart, QChartView, QLineSeries, QValueAxis

class RollingChart(QWidget):
    '''
    A reusable chart widget with one or more QLineSeries and a rolling X window.

    Parameters:
        title       - Chart title.
        series_defs - List of (name, color_qt_str or None) for each line.
        y_min,y_max - Fixed Y axis range.
        window      - Number of points to keep (points are 1 per tick by default).
    '''
    def __init__(self, title: str, series_defs: list[tuple], y_min: float, y_max: float, window: int = 120, parent=None):
        super().__init__(parent)
        
        self.pointWindow = window
        self.xpos   = window - 1
        self.chart  = QChart()
        
        self.chart.setTitle(title)
        self.chart.legend().setVisible(len(series_defs) > 1)
        self.chart.legend().setAlignment(Qt.AlignBottom)

        self.series:list[QLineSeries] = []
        for name, color in series_defs:
            s = QLineSeries()
            s.setName(name)
            if color:
                s.setColor(color)  # QColor or string like "#RRGGBB"
            self.series.append(s)
            self.chart.addSeries(s)

        # Setup X Axis... Note, setVisible disables all of this, however whatI
        # want is the tick count etc, but NO lable on the axis.  There does not
        # appear to be a way to do that.
        self.axis_x = QValueAxis()
        self.axis_x.setRange(0, self.pointWindow)
        self.axis_x.setMinorTickCount( 2 )
        self.axis_x.setTickCount( 10 )
        self.axis_x.setLabelFormat("%d")
        self.axis_x.setVisible(False)

        # Setup Y Axis...
        self.axis_y = QValueAxis()
        self.axis_y.setRange(y_min, y_max)
        self.axis_y.setLabelFormat( "%d" )

        self.chart.addAxis(self.axis_x, Qt.AlignBottom)
        self.chart.addAxis(self.axis_y, Qt.AlignLeft)

        for s in self.series:
            s.attachAxis(self.axis_x)
            s.attachAxis(self.axis_y)

        self.view = QChartView(self.chart)
        self.view.setRenderHints(QPainter.RenderHint.Antialiasing)

        layout = QGridLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self.view, 0, 0)
        
    def append(self, values: list[float]):
        '''
        Append one sample (for each series) at the next x value. Handles rolling window.
        values must match the number of series.
        
        Parameters:
            values - A list of floating point numbers, on per data series in the
                    chart.
        '''
        self.xpos += 1
        for s, v in zip(self.series, values):
            # Handle NaN by skipping, or plot zero—here we clamp None/NaN to None and skip
            try:
                if v is None:
                    continue
                # If you want to clamp, do it here: v = max(self.axis_y.min(), min(self.axis_y.max(), v))
                s.append(self.xpos, float(v))
            except Exception as error:
                # ignore bad data points
                print( f"Exception error {error}" )
                pass
            
        # Trim series to rolling window
        min_x_to_keep = max(0, self.xpos - self.pointWindow)
        self.axis_x.setRange(min_x_to_keep, self.xpos)
        
        for s in self.series:
            # Efficient trim: remove points with x < min_x_to_keep
            # QLineSeries doesn't provide O(1) pop from front, so we rebuild if large
            points = s.pointsVector()
            if points and points[0].x() < min_x_to_keep:
                # binary search for first index >= min_x_to_keep
                lo, hi = 0, len(points)
                while lo < hi:
                    mid = (lo + hi) // 2
                    if points[mid].x() < min_x_to_keep:
                        lo = mid + 1
                    else:
                        hi = mid
                s.replace(points[lo:])  # keep tail only

class scaleValues:
    def __init__( self, range_y ):
        self.index      = 0
        self.valueRange = range_y
    
    @property
    def scale( self ):
        return self.valueRange[self.index][1]
    
    def scaleValue(self, value : float ):
        return value / self.scale
    
    def nextScale( self ):
        if (self.index + 1) < len(self.valueRange):
            self.index += 1
        #print( f"Switched scale to {self.valueRange[self.index]}")
    
    def prevScale( self ):
        if self.index > 0:
            self.index -= 1
        #print( f"Switches scale to {self.valueRange[self.index]}")
            
    def scalePointsDown( self, points ):
        amount = self.valueRange[self.index][1]
        for point in points:
            point.setY(point.y() / amount)
            
    def scaleDown( self, value ):
        return value / 1024
    
    def scaleUp( self, value ):
        return value * 1024
            
    def scalePointsUp( self, points ):
        amount = self.valueRange[self.index][1]
        for point in points:
            point.setY(point.y() * amount)

    @property
    def name( self ):
        return self.valueRange[self.index][0]
    
    
class RollingChartDynamic(RollingChart):
    def __init__(self, title : str, series_defs: list[tuple], range_y : list[tuple], window=120,parent=None):
        self.maxY = 512
        super().__init__(title,series_defs,0,self.maxY,window,parent)
        self.title = title
        self.max   = 0
        self.scale = scaleValues(range_y)
        self.chart.setTitle( title+ f" ({self.scale.name})" )
    
    def getBestFit( self, value ):
        values = [4,8,16,32,64,128,256,512,1024]
        for i in values:
            if value < i:
                return i
        return 4
    def append(self, values: list[float]):
        '''
        Append one sample (for each series) at the next x value. Handles rolling window.
        values must match the number of series.
        
        Parameters:
            values - A list of floating point numbers, on per data series in the
                    chart.
        '''
        scaleUp = False
        self.xpos += 1
        for s, v in zip(self.series, values):
            # Handle NaN by skipping, or plot zero—here we clamp None/NaN to None and skip
            try:
                if v is None:
                    continue
                sv = self.scale.scaleValue(v)
                if sv > 1024:
                    scaleUp = True
                # If you want to clamp, do it here: v = max(self.axis_y.min(), min(self.axis_y.max(), v))
                #if v:
                #    print( f"value : {v} scaled: {sv} " )
                s.append(self.xpos, float(sv))
            except Exception as error:
                # ignore bad data points
                print( f"Exception error {error}" )
                pass
            
        # Trim series to rolling window
        min_x_to_keep = max(0, self.xpos - self.pointWindow)
        self.axis_x.setRange(min_x_to_keep, self.xpos)
            
        if scaleUp:
            self.scale.nextScale()
            self.chart.setTitle(self.title + f" ({self.scale.name})" )

        maxV = 0
        for s in self.series:
            drop = 0
            points = s.pointsVector()
            for index, point in enumerate(points):
                if point.x() < min_x_to_keep:
                    drop = index
                if scaleUp:
                    point.setY( self.scale.scaleDown(point.y()))
                if maxV < point.y():
                    maxV = point.y()
            s.replace( points[drop:] )
      
        if maxV > 1:
            self.axis_y.setRange( 0, self.getBestFit(maxV) )
        
        #print( f"maxV left is {maxV}" )
        if maxV < 1:
            self.scale.prevScale()
            self.chart.setTitle( self.title + f" ({self.scale.name})")
            for s in self.series:
                points = s.pointsVector()
                for point in points:
                    point.setY( self.scale.scaleUp(point.y()))
                s.replace(points)
                
class MonitorWindow(QMainWindow):
    '''
    Creating a window to monitor various system aspects.
    
    Parameters:
        refresh_ms  - Time between refreshes of data on screen, in milliseconds, the
                      default is 1 second.
        window      - How much data do we want to store in the graph?  Each data point
                      is a data refresh period.
        Parent      - Owning parent of this window... default is None.
    '''
    def __init__(self, refresh_ms: int = 1000, window = 120, parent=None):
        super().__init__(parent)
        
        self.setWindowTitle("System Monitor")
        self.setMinimumSize(900, 900)

        central = QWidget(self)
        grid = QGridLayout(central)
        grid.setContentsMargins(8, 8, 8, 8)
        grid.setHorizontalSpacing(8)
        grid.setVerticalSpacing(8)
        self.setCentralWidget(central)

        # Charts
        self.use_chart = RollingChart(
            title="CPU Utilization",
            series_defs=[ (name, None) for name in cpuload.cpuNames ],
            y_min=0, y_max=100,
            window=120
            )
        
        series = [("CPU", None)]
        for name in multiDrive.drives:
            series.append( (name,None) )
        self.cpu_chart = RollingChart(
            title="Temperature (°C)",
            series_defs= series,
            y_min=20, y_max=80,
            window=window
            )

        self.fan_chart = RollingChart(
            title="Fan Speed",
            series_defs=[("RPM",None)],
            y_min=0,y_max=6000,
            window=window
        )

        series = []
        for name in multiDrive.drives:
            series.append( (f"{name} Read", None) )
            series.append( (f"{name} Write", None ) )
            
        self.io_chart = RollingChartDynamic(
            title="Disk I/O",
            series_defs=series,
            range_y=[("Bytes/s", 1),("KiB/s",1024),("MiB/s", 1024*1024),("GiB/s",1024*1024*1024)],
            window=window,
        )

        # Layout: 2x2 grid (CPU, NVMe on top; IO full width bottom)
        grid.addWidget(self.use_chart, 0, 0, 1, 2 )
        grid.addWidget(self.io_chart,  1, 0, 1, 2 )
        grid.addWidget(self.cpu_chart, 2, 0, 1, 1 )
        grid.addWidget(self.fan_chart, 2, 1, 1, 1 )

        # Get the initial information from the syste
        self.refresh_metrics()
        
        # Timer
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.refresh_metrics)
        self.timer.start(refresh_ms)

    def refresh_metrics(self):
        '''
        This routine is called periodically, as setup in the __init__ functon.  Since this
        routine calls out to other things, we want to make sure that there is no possible
        exception, so everything needs to be wrapped in a handler.
        
        '''
        # Obtain the CPU temperature
        try:
            cpu_c = float(sysdata.CPUTemperature)
        except Exception:
            cpu_c = None

        # Obtain the current fan speed
        try:
            fan_speed = sysdata.fanSpeed
        except Exception:
            fan_speed = None

        # Obtain the NVMe device temperature
        try:
            nvme_c = sysdata.driveTemp
        except Exception:
            nvme_c = None

        # Obtain the NVMe Device read and write rates
        try:
            rwData = []
            drives = multiDrive.readWriteBytes()
            for drive in drives:
                rwData.append( float(drives[drive][0]))
                rwData.append( float(drives[drive][1]))
        except Exception :
            rwData = [ None, None ]
            
        # Get the CPU load precentages
        try:
            p = cpuload.getPercentages()
            values = [p[name] for name in cpuload.cpuNames]
        except Exception:
            values = [ None for name in cpuload.cpuNames ]

        # Append to charts
        self.cpu_chart.append([cpu_c,nvme_c])
        self.fan_chart.append([fan_speed])
        self.io_chart.append( rwData )
        self.use_chart.append( values )

def main():
    app = QApplication(sys.argv)
    w = MonitorWindow(refresh_ms=1000)
    w.show()
    sys.exit(app.exec_())

if __name__ == "__main__":
    main()

