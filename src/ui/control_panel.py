"""Control panel that builds itself from controls.json, written by the effect program.

The effect program (C) declares its controls and writes them to controls.json
when it starts. This panel reads that file, creates one widget per control, and
sends each control's value back to the effect over UDP as a line of text:

    "gain 0.500"        (values are normalized, 0.0 to 1.0)

A message is sent whenever a control changes, and every value is re-sent once a
second so a receiver that starts later still picks up the current settings.

If controls.json doesn't exist yet, the panel waits and checks once a second, so
you can start the panel and the effect program in either order.

Requires: PySide6
Run:      python control_panel.py [--controls controls.json] [--host 127.0.0.1] [--port 9001]

(The default controls.json is looked for in the folder you run this from, which
is also where the effect program writes it if you run that from the same folder.
Otherwise, point --controls at the file.)
"""
import argparse
import json
import socket
import sys

from PySide6.QtCore import Qt, QTimer, Signal
from PySide6.QtWidgets import (QApplication, QDial, QHBoxLayout, QLabel,
                               QPushButton, QVBoxLayout, QWidget)


# ---------------------------------------------------------------------------
# Widgets. Each takes (label, initial value) and has a valueChanged signal
# that carries a number from 0.0 to 1.0, plus a value() method.
# ---------------------------------------------------------------------------
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


class Switch(QWidget):
    """A labelled on/off button. Its value is 0.0 (off) or 1.0 (on)."""

    valueChanged = Signal(float)

    def __init__(self, title, initial=0.0):
        super().__init__()
        self.title = QLabel(title)
        self.title.setAlignment(Qt.AlignCenter)
        self.title.setStyleSheet("font-weight: bold; font-size: 16px;")

        self.button = QPushButton()
        self.button.setCheckable(True)
        self.button.setChecked(initial >= 0.5)
        self.button.setMinimumSize(90, 40)

        # The stretches above and below keep the switch vertically centred
        # when it sits in a row next to taller knobs.
        layout = QVBoxLayout(self)
        layout.addStretch()
        layout.addWidget(self.title)
        layout.addWidget(self.button, alignment=Qt.AlignCenter)
        layout.addStretch()

        self.button.toggled.connect(self._on_toggled)
        self._update_text()

    def value(self):
        return 1.0 if self.button.isChecked() else 0.0

    def _update_text(self):
        self.button.setText("ON" if self.button.isChecked() else "OFF")

    def _on_toggled(self, _checked):
        self._update_text()
        self.valueChanged.emit(self.value())


# The control types the panel knows how to draw. The "type" in controls.json
# picks the widget from this table. To support a new kind of control, write a
# widget class like the ones above and add it here.
WIDGETS = {"knob": Knob, "switch": Switch}


# ---------------------------------------------------------------------------
# Reading controls.json
# ---------------------------------------------------------------------------
def clean_spec(spec):
    """Check one entry from the file. Returns a tidy dict with any missing
    fields filled in, or None (with a message) if the entry is unusable."""
    try:
        name = str(spec["name"])
        kind = spec.get("type", "knob")
        if kind not in WIDGETS:
            raise ValueError(f"unknown control type {kind!r}")
        return {
            "name": name,
            "label": str(spec.get("label", name)),
            "type": kind,
            "row": int(spec.get("row", 0)),
            "default": min(1.0, max(0.0, float(spec.get("default", 0.5)))),
        }
    except (KeyError, TypeError, ValueError, AttributeError) as e:
        print(f"Skipping control {spec!r}: {e!r}", file=sys.stderr)
        return None


def load_controls(path):
    """Read the control list from the file. Returns a list of tidy dicts, or
    None if the file isn't there yet, isn't valid, or has no usable controls
    (for example, if the effect program is still in the middle of writing it)."""
    try:
        with open(path, encoding="utf-8") as f:
            raw = json.load(f)["controls"]
        specs = [s for s in (clean_spec(r) for r in raw) if s is not None]
    except (OSError, ValueError, KeyError, TypeError):
        return None
    return specs or None


# ---------------------------------------------------------------------------
# The panel
# ---------------------------------------------------------------------------
class ControlPanel(QWidget):
    def __init__(self, host, port, controls_path):
        super().__init__()
        self.setWindowTitle("Control Panel")
        self.addr = (host, port)
        self.controls_path = controls_path
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        # Every control, by name, across all rows. Filled in by build();
        # send_all() loops over it.
        self.controls = {}

        # The rows of controls go into controls_area once the file is read.
        self.controls_area = QVBoxLayout()
        self.status = QLabel()
        self.status.setAlignment(Qt.AlignCenter)
        self.status.setStyleSheet("color: gray;")

        layout = QVBoxLayout(self)
        layout.addLayout(self.controls_area)
        layout.addWidget(self.status)

        # Re-send every value once a second (UDP has no delivery guarantee, and
        # the receiver may be started after this panel). Started by build().
        self.heartbeat = QTimer(self)
        self.heartbeat.timeout.connect(self.send_all)

        # Until the file shows up, check for it once a second.
        self.poll = QTimer(self)
        self.poll.timeout.connect(self.try_load)
        self.poll.start(1000)
        self.try_load()

    def try_load(self):
        specs = load_controls(self.controls_path)
        if specs is None:
            self.status.setText(f"Waiting for {self.controls_path} (start the effect program)...")
            return
        self.poll.stop()
        self.build(specs)

    def build(self, specs):
        # Make one horizontal row for each row number that appears in the file,
        # and put each control into its row.
        rows = {}
        for spec in specs:
            row = rows.setdefault(spec["row"], QHBoxLayout())
            self.add_control(row, spec)

        # Stack the rows top to bottom, in row-number order.
        for index in sorted(rows):
            self.controls_area.addLayout(rows[index])

        host, port = self.addr
        self.status.setText(f"{len(self.controls)} controls from {self.controls_path}"
                            f"  ->  sending to udp://{host}:{port}")
        self.adjustSize()                 # grow the window to fit the new controls
        self.heartbeat.start(1000)
        self.send_all()

    def add_control(self, row, spec):
        widget = WIDGETS[spec["type"]](spec["label"], spec["default"])
        self.controls[spec["name"]] = widget
        row.addWidget(widget)
        # "n=name" makes this control remember its own name. Without it, every
        # control would end up using whatever name was set last.
        widget.valueChanged.connect(lambda v, n=spec["name"]: self.send(n, v))

    def send(self, name, value):
        try:
            self.sock.sendto(f"{name} {value:.3f}".encode("ascii"), self.addr)
        except OSError:
            pass  # nothing to do if the send fails; the next update retries

    def send_all(self):
        for name, widget in self.controls.items():
            self.send(name, widget.value())


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--controls", default="controls.json",
                   help="the file the effect program writes (default: controls.json)")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=9001)
    args, qt_args = p.parse_known_args()

    app = QApplication([sys.argv[0]] + qt_args)
    panel = ControlPanel(args.host, args.port, args.controls)
    panel.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
