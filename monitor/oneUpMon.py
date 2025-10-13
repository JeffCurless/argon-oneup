#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Application that monitors current CPU and Drive temp, along with fan speed and IO utilization
Requires: PyQt5 (including QtCharts)

"""

import sys
from typing import Tuple, List
from gpiozero import CPUTemperature
from oneUpSupport import systemData
from cpuload import CPULoad
import os

# --------------------------
# Globals
# --------------------------
sysdata = None
cpuload = CPULoad()

# --------------------------
# UI
# --------------------------

from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtGui import QPainter
from PyQt5.QtWidgets import QApplication, QMainWindow, QWidget, QGridLayout, QLabel
from PyQt5.QtChart import QChart, QChartView, QLineSeries, QValueAxis

class RollingChart(QWidget):
    """
    A reusable chart widget with one or more QLineSeries and a rolling X window.

    Args:
        title: Chart title
        series_defs: List of (name, color_qt_str or None) for each line
        y_min, y_max: Fixed Y axis range
        window: number of points to keep (points are 1 per tick by default)
    """
    def __init__(self, title: str, series_defs: List[tuple], y_min: float, y_max: float, window: int = 120, parent=None):
        super().__init__(parent)
        self.window = window
        self.x = 0
        self.series: List[QLineSeries] = []
        self.chart = QChart()
        self.chart.setTitle(title)
        self.chart.legend().setVisible(len(series_defs) > 1)
        self.chart.legend().setAlignment(Qt.AlignBottom)

        for name, color in series_defs:
            s = QLineSeries()
            s.setName(name)
            if color:
                s.setColor(color)  # QColor or string like "#RRGGBB"
            self.series.append(s)
            self.chart.addSeries(s)

        # Axes
        self.axis_x = QValueAxis()
        self.axis_x.setRange(0, self.window)
        #self.axis_x.setTitleText("Seconds")
        self.axis_x.setLabelFormat("%d")

        self.axis_y = QValueAxis()
        self.axis_y.setRange(y_min, y_max)

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

    def append(self, values: List[float]):
        """
        Append one sample (for each series) at the next x value. Handles rolling window.
        values must match the number of series.
        """
        self.x += 1
        for s, v in zip(self.series, values):
            # Handle NaN by skipping, or plot zero—here we clamp None/NaN to None and skip
            try:
                if v is None:
                    continue
                # If you want to clamp, do it here: v = max(self.axis_y.min(), min(self.axis_y.max(), v))
                s.append(self.x, float(v))
            except Exception:
                # ignore bad data points
                pass

        # Trim series to rolling window
        min_x_to_keep = max(0, self.x - self.window)
        self.axis_x.setRange(min_x_to_keep, self.x)

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


class MonitorWindow(QMainWindow):
    def __init__(self, refresh_ms: int = 1000, window = 120, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Argon 1UP Monitor")
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
        
        self.cpu_chart = RollingChart(
            title="Temperature (°C)",
            series_defs=[
                ("CPU", None),
                ("NVMe", None),
                ],
            y_min=20, y_max=80,
            window=window
            )

        self.fan_chart = RollingChart(
            title="Fan Speed",
            series_defs=[("RPM",None)],
            y_min=0,y_max=6000,
            window=window
        )

        self.io_chart = RollingChart(
            title="NVMe I/O (MB/s)",
            series_defs=[
                ("Read MB/s", None),
                ("Write MB/s", None),
            ],
            y_min=0, y_max=1100,  # adjust ceiling for your device
            window=window
        )

        # Layout: 2x2 grid (CPU, NVMe on top; IO full width bottom)
        grid.addWidget(self.use_chart, 0, 0, 1, 2 )
        grid.addWidget(self.io_chart,  1, 0, 1, 2 )
        grid.addWidget(self.cpu_chart, 2, 0, 1, 1 )
        grid.addWidget(self.fan_chart, 2, 1, 1, 1 )

        # Timer
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.refresh_metrics)
        self.timer.start(refresh_ms)

        self.refresh_metrics()

    def refresh_metrics(self):
        # Gather metrics with safety
        try:
            cpu_c = float(sysdata.CPUTemperature)
        except Exception:
            cpu_c = None

        try:
            fan_speed = sysdata.fanSpeed
        except Exception:
            fan_speed = None

        try:
            nvme_c = sysdata.driveTemp
        except Exception:
            nvme_c = None

        try:
            read_mb, write_mb = sysdata.driveStats
            read_mb = float(read_mb)
            write_mb = float(write_mb)
        except Exception:
            read_mb, write_mb = None, None
            
        try:
            p = cpuload.getPercentages()
            values = []
            for i in range( len(cpuload) ):
                values.append( round( p[f'cpu{i}'], 2 ) )
        except Exception:
            values = [ None for i in range( len( cpuload) ) ]

        # Append to charts
        self.cpu_chart.append([cpu_c,nvme_c])
        self.fan_chart.append([fan_speed])
        self.io_chart.append([read_mb, write_mb])
        self.use_chart.append( values )

def main():
    app = QApplication(sys.argv)
    w = MonitorWindow(refresh_ms=1000)
    w.show()
    sys.exit(app.exec_())

if __name__ == "__main__":
    sysdata = systemData()
    main()

