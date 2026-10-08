"""Virtual control panel with two knobs: Gain and Volume.

Requires: pip install PySide6
Run:      python control_panel.py [--host 127.0.0.1] [--port 9001]

Each knob sends its value over UDP as a one-line ASCII message:
    "gain 0.500"      (value is normalized, 0.0 to 1.0)
    "volume 0.750"
A message is sent whenever a knob moves, and both values are re-sent once a
second so a receiver that starts later still picks up the current settings.
"""
import argparse
import socket
import sys

from PySide6.QtCore import Qt, QTimer, Signal
from PySide6.QtWidgets import (QApplication, QDial, QHBoxLayout, QLabel,
                               QVBoxLayout, QWidget)


class Knob(QWidget):
    """A labelled dial with a percentage readout. Value is normalized 0.0-1.0."""

    valueChanged = Signal(float)

    def __init__(self, title, initial=0.5):
        super().__init__()
        self.dial = QDial()
        self.dial.setRange(0, 100)
        self.dial.setValue(round(initial * 100))
        self.dial.setNotchesVisible(True)
        self.dial.setMinimumSize(120, 120)

        self.title = QLabel(title)
        self.title.setAlignment(Qt.AlignCenter)
        self.title.setStyleSheet("font-weight: bold; font-size: 16px;")
        self.readout = QLabel()
        self.readout.setAlignment(Qt.AlignCenter)

        layout = QVBoxLayout(self)
        layout.addWidget(self.title)
        layout.addWidget(self.dial)
        layout.addWidget(self.readout)

        self.dial.valueChanged.connect(self._on_dial)
        self._on_dial(self.dial.value())

    def value(self):
        return self.dial.value() / 100.0

    def _on_dial(self, v):
        self.readout.setText(f"{v}%")
        self.valueChanged.emit(v / 100.0)


class ControlPanel(QWidget):
    def __init__(self, host, port):
        super().__init__()
        self.setWindowTitle("Control Panel")
        self.addr = (host, port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        # Every knob, by name, across both rows. send_all() loops over this.
        self.knobs = {}

        # The two rows. Each one is a horizontal strip that knobs get added to.
        top_row = QHBoxLayout()
        mid_row = QHBoxLayout()
        bottom_row = QHBoxLayout()

        # add_knob(row, name, label):
        # row - which row to put the knob in
        # name - what is sent over UDP; must match the name in the C program
        # label - the text shown above the knob
        self.add_knob(top_row, "gain", "Gain")
        self.add_knob(top_row, "volume", "Volume")
        self.add_knob(top_row, "bias", "Bias", 0.25)

        self.add_knob(mid_row, "bass", "Bass", 0.3);
        self.add_knob(mid_row, "mid", "Mid", 0.8)
        self.add_knob(mid_row, "treble", "Treble")

        self.add_knob(bottom_row, "bass_sweep", "Bass Sweep", 0.4)
        self.add_knob(bottom_row, "mid_sweep", "Mid Sweep")
        self.add_knob(bottom_row, "treble_sweep", "Treble Sweep", 0.6)

        status = QLabel(f"sending to udp://{host}:{port}")
        status.setAlignment(Qt.AlignCenter)
        status.setStyleSheet("color: gray;")

        # Stack the two rows, with the status text underneath.
        layout = QVBoxLayout(self)
        layout.addLayout(top_row)
        layout.addLayout(mid_row)
        layout.addLayout(bottom_row)
        layout.addWidget(status)

        # Re-send everything once a second (UDP has no delivery guarantee, and
        # the receiver may be started after this panel).
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.send_all)
        self.timer.start(1000)
        self.send_all()

    def add_knob(self, row, name, label, initial = 0.5):
        knob = Knob(label)
        self.knobs[name] = knob
        row.addWidget(knob)
        # "n=name" makes this knob remember its own name. Without it, every
        # knob would end up using whatever name was set last.
        knob.valueChanged.connect(lambda v, n=name: self.send(n, v))

    def send(self, name, value):
        try:
            self.sock.sendto(f"{name} {value:.3f}".encode("ascii"), self.addr)
        except OSError:
            pass  # nothing to do if the send fails; the next update retries

    def send_all(self):
        for name, knob in self.knobs.items():
            self.send(name, knob.value())


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=9001)
    args, qt_args = p.parse_known_args()

    app = QApplication([sys.argv[0]] + qt_args)
    panel = ControlPanel(args.host, args.port)
    panel.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
