#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
PyQt5 CPU/NVMe Monitor

- getCPUTemp()  -> float (°C)
- getDriveTemp() -> float (°C)
- getIORate()   -> tuple[float, float] in MB/s as (read_mb_s, write_mb_s)

Replace the stub return values with your real implementations later.
"""

import sys
from typing import Tuple
from systemsupport import systemData, CPULoad

sysdata = systemData()

# --------------------------
# Metrics function stubs
# --------------------------
def getCPUTemp() -> float:
    """Return current CPU temperature in °C."""
    return float( sysdata.CPUTemperature )

def getDriveTemp() -> float:
    """Return current NVMe drive temperature in °C."""
    return sysdata.driveTemp

def getIORate() -> Tuple[float, float]:
    """Return current NVMe IO rates (read_MBps, write_MBps)."""
    return sysdata.driveStats


# --------------------------
# UI
# --------------------------
from PyQt5.QtCore import Qt, QTimer, QDateTime
from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QWidget, QGridLayout, QLabel, QProgressBar, QHBoxLayout
)

class MetricRow(QWidget):
    """A compact row with a label, numeric value, unit, and optional progress bar."""
    def __init__(self, title: str, show_bar: bool = False, bar_min: int = 0, bar_max: int = 110, parent=None):
        super().__init__(parent)
        self.title_lbl = QLabel(title)
        self.title_lbl.setStyleSheet("font-weight: 600;")
        self.value_lbl = QLabel("--")
        self.value_lbl.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        self.unit_lbl = QLabel("")
        self.unit_lbl.setAlignment(Qt.AlignLeft | Qt.AlignVCenter)

        layout = QGridLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self.title_lbl, 0, 0, 1, 1)
        layout.addWidget(self.value_lbl, 0, 1, 1, 1)
        layout.addWidget(self.unit_lbl, 0, 2, 1, 1)

        self.bar = None
        if show_bar:
            self.bar = QProgressBar()
            self.bar.setMinimum(bar_min)
            self.bar.setMaximum(bar_max)
            self.bar.setTextVisible(False)
            layout.addWidget(self.bar, 1, 0, 1, 3)

        layout.setColumnStretch(0, 1)
        layout.setColumnStretch(1, 0)
        layout.setColumnStretch(2, 0)

    def set_value(self, value: float, unit: str = "", bar_value: float = None):
        self.value_lbl.setText(f"{value:.1f}")
        self.unit_lbl.setText(unit)
        if self.bar is not None and bar_value is not None:
            self.bar.setValue(int(bar_value))


class MonitorWindow(QMainWindow):
    def __init__(self, refresh_ms: int = 1000, parent=None):
        super().__init__(parent)
        self.setWindowTitle("CPU & NVMe Monitor")
        self.setMinimumWidth(420)

        central = QWidget(self)
        grid = QGridLayout(central)
        grid.setContentsMargins(16, 16, 16, 16)
        grid.setVerticalSpacing(12)
        self.setCentralWidget(central)

        # Rows
        self.cpu_row = MetricRow("CPU Temperature", show_bar=True, bar_max=90)
        self.nvme_row = MetricRow("NVMe Temperature", show_bar=True, bar_max=90)

        # IO row: two side-by-side values
        self.io_title = QLabel("NVMe I/O Rate")
        self.io_title.setStyleSheet("font-weight: 600;")
        self.io_read = QLabel("Read: -- MB/s")
        self.io_write = QLabel("Write: -- MB/s")
        io_box = QHBoxLayout()
        io_box.addWidget(self.io_read, 1)
        io_box.addWidget(self.io_write, 1)

        # Last updated
        self.updated_lbl = QLabel("Last updated: --")
        self.updated_lbl.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        self.updated_lbl.setStyleSheet("color: #666; font-size: 11px;")

        # Layout
        grid.addWidget(self.cpu_row, 0, 0, 1, 2)
        grid.addWidget(self.nvme_row, 1, 0, 1, 2)
        grid.addWidget(self.io_title, 2, 0, 1, 2)
        grid.addLayout(io_box, 3, 0, 1, 2)
        grid.addWidget(self.updated_lbl, 4, 0, 1, 2)

        # Timer
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.refresh_metrics)
        self.timer.start(refresh_ms)

        # Initial fill
        self.refresh_metrics()

    def refresh_metrics(self):
        try:
            cpu_c = float(getCPUTemp())
        except Exception:
            cpu_c = float("nan")

        try:
            nvme_c = float(getDriveTemp())
        except Exception:
            nvme_c = float("nan")

        try:
            read_mb, write_mb = getIORate()
            read_mb = float(read_mb)
            write_mb = float(write_mb)
        except Exception:
            read_mb, write_mb = float("nan"), float("nan")

        # Update rows
        self.cpu_row.set_value(cpu_c if cpu_c == cpu_c else 0.0, "°C", bar_value=cpu_c if cpu_c == cpu_c else 0)
        self.nvme_row.set_value(nvme_c if nvme_c == nvme_c else 0.0, "°C", bar_value=nvme_c if nvme_c == nvme_c else 0)
        self.io_read.setText(f"Read: {read_mb:.1f} MB/s" if read_mb == read_mb else "Read: -- MB/s")
        self.io_write.setText(f"Write: {write_mb:.1f} MB/s" if write_mb == write_mb else "Write: -- MB/s")

        self.updated_lbl.setText(f"Last updated: {QDateTime.currentDateTime().toString('yyyy-MM-dd hh:mm:ss')}")

def main():
    app = QApplication(sys.argv)
    w = MonitorWindow(refresh_ms=1000)
    w.show()
    sys.exit(app.exec_())

if __name__ == "__main__":
    main()

