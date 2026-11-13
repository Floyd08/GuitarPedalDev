"""Control panel that builds itself from controls.json, written by the effect program.

The effect program (C) declares its controls and writes them to controls.json
when it starts. This panel reads that file, creates one widget per control, and
sends each control's value back to the effect over UDP as a line of text:

    "gain 0.500"        (values are normalized, 0.0 to 1.0)

A message is sent whenever a control changes, and every value is re-sent once a
second so a receiver that starts later still picks up the current settings.

The effect replies on the same socket, in the same form, with the value each
control's setter actually applied, in real units:

    "trm_rate 1.57"     (shown under the percentage as "1.57 Hz")

Only controls with "units" in controls.json get this value line.

controls.json also lists the control sets ("groups"), one per effect. Each set
is drawn as an outline around its controls, with the set's label on the top
left. The outline and the label share the set's colour.

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
import math
import socket
import sys

from PySide6.QtCore import QSocketNotifier, Qt, QTimer, Signal
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (QApplication, QDial, QGridLayout, QGroupBox,
                               QHBoxLayout, QLabel, QPushButton, QVBoxLayout,
                               QWidget)


# ---------------------------------------------------------------------------
# Showing real values
# ---------------------------------------------------------------------------
# Units that read better scaled up by 1000 ("1.2 kHz" rather than "1200 Hz").
SCALED_UNITS = {"Hz": "kHz", "ms": "s"}


def format_value(value, units, trim=False):
    """Turn a value from the effect into text like "1.57 Hz" or "-6.0 dB".
    Uses fewer decimals as the number gets bigger, so the text stays short.
    With trim=True, zeros after the decimal point are dropped ("7", not "7.00")."""
    if not math.isfinite(value):
        return "--"
    if units in SCALED_UNITS and abs(value) >= 1000:
        value /= 1000
        units = SCALED_UNITS[units]
    size = abs(value)
    decimals = 0 if size >= 100 else 1 if size >= 10 else 2
    number = f"{value:.{decimals}f}"
    if trim and "." in number:
        number = number.rstrip("0").rstrip(".")
    return f"{number} {units}".strip()


class ValueLine(QLabel):
    """The grey line under a control that shows its value in real units.
    Shows "--" until the effect reports a value."""

    def __init__(self, units, trim=False):
        super().__init__()
        self.units = units
        self.trim = trim
        self.setAlignment(Qt.AlignCenter)
        self.setStyleSheet("color: gray;")
        self.setText("--")

    def show_value(self, value):
        self.setText(format_value(value, self.units, self.trim))


# ---------------------------------------------------------------------------
# Widgets. Each takes (label, initial value, units, positions) and has a
# valueChanged signal that carries a number from 0.0 to 1.0, a value() method,
# and a set_display(value) method for the real value the effect reports.
# Only the rotary uses positions; the others accept it and ignore it.
# ---------------------------------------------------------------------------
class Knob(QWidget):
    """A labelled dial with a percentage readout, and below it the real value
    (if the control has units). Value is normalized 0.0-1.0."""

    valueChanged = Signal(float)
    trim_value = False      # show the real value as "7.00" (False) or "7" (True)

    def __init__(self, title, initial=0.5, units=None, positions=None, steps=100):
        super().__init__()
        # The dial moves in whole steps from 0 to self.steps. A knob has 100,
        # which is fine enough to feel smooth.
        self.steps = steps
        self.dial = QDial()
        self.dial.setRange(0, steps)
        self.dial.setPageStep(max(1, steps // 10))      # how far a click beside the handle moves it
        self.dial.setValue(round(initial * steps))
        self.dial.setNotchesVisible(True)
        self.dial.setMinimumSize(90, 90)

        self.title = QLabel(title)
        self.title.setAlignment(Qt.AlignCenter)
        self.title.setStyleSheet("font-weight: bold; font-size: 16px;")
        self.readout = QLabel()
        self.readout.setAlignment(Qt.AlignCenter)

        self.display = ValueLine(units, self.trim_value) if units is not None else None

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self.title)
        layout.addWidget(self.dial)
        layout.addWidget(self.readout)
        if self.display is not None:
            layout.addWidget(self.display)

        self.dial.valueChanged.connect(self._on_dial)
        self._on_dial(self.dial.value())

    def value(self):
        return self.dial.value() / self.steps

    def set_display(self, value):
        if self.display is not None:
            self.display.show_value(value)

    def readout_text(self, step):
        """The text under the dial when it is on the given step."""
        return f"{step}%"

    def _on_dial(self, step):
        self.readout.setText(self.readout_text(step))
        self.valueChanged.emit(step / self.steps)


class Rotary(Knob):
    """A dial that clicks between a fixed number of positions, like a rotary
    switch. The readout shows the position ("7 of 12"). The value it sends is
    still 0.0-1.0: the positions are spread evenly, so with 12 positions the
    first sends 0.0, the second 1/11, and the last 1.0."""

    trim_value = True

    def __init__(self, title, initial=0.0, units=None, positions=2):
        # One dial step per position, so the dial can only rest on a position.
        super().__init__(title, initial, units, steps=positions - 1)

    def readout_text(self, step):
        return f"{step + 1} of {self.steps + 1}"


class Switch(QWidget):
    """A labelled on/off button. Its value is 0.0 (off) or 1.0 (on)."""

    valueChanged = Signal(float)

    def __init__(self, title, initial=0.0, units=None, positions=None):
        super().__init__()
        self.display = ValueLine(units) if units is not None else None
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
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addStretch()
        layout.addWidget(self.title)
        layout.addWidget(self.button, alignment=Qt.AlignCenter)
        if self.display is not None:
            layout.addWidget(self.display)
        layout.addStretch()

        self.button.toggled.connect(self._on_toggled)
        self._update_text()

    def value(self):
        return 1.0 if self.button.isChecked() else 0.0

    def set_display(self, value):
        if self.display is not None:
            self.display.show_value(value)

    def _update_text(self):
        self.button.setText("ON" if self.button.isChecked() else "OFF")

    def _on_toggled(self, _checked):
        self._update_text()
        self.valueChanged.emit(self.value())


# The control types the panel knows how to draw. The "type" in controls.json
# picks the widget from this table. To support a new kind of control, write a
# widget class like the ones above and add it here.
WIDGETS = {"knob": Knob, "switch": Switch, "rotary": Rotary}


# ---------------------------------------------------------------------------
# Control sets. Each effect's controls sit inside one outline, with the
# effect's name on the top left. The outline and the name share one colour.
# ---------------------------------------------------------------------------
DEFAULT_GROUP_COLOR = "gray"        # used when a set has no colour, or one Qt doesn't know


class ControlGroup(QGroupBox):
    """A box around one set of controls. Inside, the controls are laid out in
    rows. With outlined=False it draws nothing, and just holds the controls."""

    def __init__(self, label="", color=DEFAULT_GROUP_COLOR, outlined=True):
        super().__init__(label if outlined else "")
        if outlined:
            # "QGroupBox::title" is the label. Putting it in the top margin
            # makes it sit on the outline, which breaks around the text.
            self.setStyleSheet(f"""
                QGroupBox {{
                    border: 2px solid {color};
                    border-radius: 8px;
                    margin-top: 10px;
                    padding-top: 8px;
                    font-size: 14px;
                    font-weight: bold;
                }}
                QGroupBox::title {{
                    subcontrol-origin: margin;
                    subcontrol-position: top left;
                    left: 12px;
                    padding: 0 6px;
                    color: {color};
                }}
            """)
        else:
            self.setStyleSheet("QGroupBox { border: none; }")
        self.rows = {}                  # row number -> the QHBoxLayout for that row

    def row(self, number):
        """The layout for one row of this set, made the first time it's asked for."""
        if number not in self.rows:
            layout = QHBoxLayout()
            layout.setSpacing(10)              # This is the spacing around each knob, so tweeking this moves them apart or together
            self.rows[number] = layout
        return self.rows[number]
        # return self.rows.setdefault(number, QHBoxLayout())

    def finish(self):
        """Call once every control is added. Stacks the rows top to bottom, in
        row-number order."""
        layout = QVBoxLayout(self)
        for number in sorted(self.rows):
            layout.addLayout(self.rows[number])

    def first_row(self):
        return min(self.rows)

    def last_row(self):
        return max(self.rows)

    def widest_row(self):
        """How many controls the fullest row holds. Used to share out the width
        of the window, so a set with three knobs gets three times the room of a
        set with one."""
        return max(row.count() for row in self.rows.values())


# ---------------------------------------------------------------------------
# Laying out the sets
# ---------------------------------------------------------------------------
def arrange(boxes):
    """Work out where every box goes on the panel.

    The panel is a grid. Each row of controls is one grid row, and each control
    is one grid column wide. A box with controls on rows 0 and 1 is two grid
    rows tall, so one-row boxes can sit beside it on row 0 AND on row 1.

    Returns a list of slots. A slot is a rectangle of grid cells, and the boxes
    that sit side by side inside it (usually one). It is a dict with "boxes",
    and "top", "bottom", "left", "right". Bottom and right are one past the
    last cell, so the slot covers rows top..bottom-1 and columns left..right-1.
    """
    # Number the rows that are in use 0, 1, 2... so a gap in the row numbers
    # (rows 0, 1 and 5, say) doesn't leave an empty band on the panel.
    numbers = sorted({number for box in boxes for number in box.rows})
    grid_row = {number: i for i, number in enumerate(numbers)}

    taken = set()                       # every (row, column) cell that is in use

    def is_free(rows, columns):
        return not any((r, c) in taken for r in rows for c in columns)

    def take(rows, columns):
        taken.update((r, c) for r in rows for c in columns)

    # Step 1. In the order the effects were registered, each box takes the
    # leftmost spot that is free on all of its rows.
    slots = []
    for box in boxes:
        top = grid_row[box.first_row()]
        bottom = grid_row[box.last_row()] + 1
        width = box.widest_row()
        left = 0
        while not is_free(range(top, bottom), range(left, left + width)):
            left += 1
        right = left + width
        take(range(top, bottom), range(left, right))

        # One-row boxes that end up next to each other share a slot, so they
        # can share out any spare width between them.
        one_row = bottom - top == 1
        for slot in slots:
            if (one_row and slot["top"] == top and slot["bottom"] == bottom
                    and slot["right"] == left):
                slot["boxes"].append(box)
                slot["right"] = right
                break
        else:
            slots.append({"boxes": [box], "top": top, "bottom": bottom,
                          "left": left, "right": right})

    # Step 2. Fill the gaps. Each slot grows to the right for as long as the
    # cells beside it are empty, then downwards and upwards for as long as the
    # cells below and above it are empty. Rightmost slots go first, so they
    # reach the edge.
    width = max(slot["right"] for slot in slots)
    height = len(numbers)
    for slot in sorted(slots, key=lambda s: s["left"], reverse=True):
        rows = range(slot["top"], slot["bottom"])
        while slot["right"] < width and is_free(rows, [slot["right"]]):
            take(rows, [slot["right"]])
            slot["right"] += 1
    for slot in slots:
        columns = range(slot["left"], slot["right"])
        while slot["bottom"] < height and is_free([slot["bottom"]], columns):
            take([slot["bottom"]], columns)
            slot["bottom"] += 1
        while slot["top"] > 0 and is_free([slot["top"] - 1], columns):
            take([slot["top"] - 1], columns)
            slot["top"] -= 1
    return slots


# ---------------------------------------------------------------------------
# Reading controls.json
# ---------------------------------------------------------------------------
def clean_color(text):
    """Turn a colour from the file into the form "#rrggbb". Accepts anything Qt
    understands: "#d98c2b", "#f80", or a name like "orange" or "steelblue".
    A missing or unknown colour becomes the default."""
    if text is not None:
        color = QColor(str(text))
        if color.isValid():
            return color.name()
        print(f"Unknown colour {text!r}, using {DEFAULT_GROUP_COLOR}", file=sys.stderr)
    return QColor(DEFAULT_GROUP_COLOR).name()


def clean_group(group):
    """Check one entry from the "groups" list. Always returns a usable dict:
    a missing label becomes "" and a missing colour becomes the default."""
    if not isinstance(group, dict):
        group = {}
    label = group.get("label")
    return {
        "label": "" if label is None else str(label),
        # The file may spell the key either way.
        "color": clean_color(group.get("colour", group.get("color"))),
    }


def clean_spec(spec):
    """Check one entry from the file. Returns a tidy dict with any missing
    fields filled in, or None (with a message) if the entry is unusable."""
    try:
        name = str(spec["name"])
        kind = spec.get("type", "knob")
        if kind not in WIDGETS:
            raise ValueError(f"unknown control type {kind!r}")
        # None (or missing) means no value line; "" means a number with no units.
        units = spec.get("units")
        # Which entry of the "groups" list this control belongs to. None (or
        # missing) means it isn't in a set, and gets no outline.
        group = spec.get("group")
        # Only a rotary has positions, and it needs at least two.
        positions = None
        if kind == "rotary":
            positions = int(spec["positions"])
            if positions < 2:
                raise ValueError(f"a rotary needs at least 2 positions, not {positions}")
        return {
            "positions": positions,
            "units": None if units is None else str(units),
            "group": None if group is None else int(group),
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
    """Read the controls and their groups from the file. Returns (specs, groups),
    two lists of tidy dicts, or None if the file isn't there yet, isn't valid,
    or has no usable controls (for example, if the effect program is still in
    the middle of writing it). A file with no "groups" gives an empty list."""
    try:
        with open(path, encoding="utf-8") as f:
            raw = json.load(f)
        groups = [clean_group(g) for g in raw.get("groups", [])]
        specs = [s for s in (clean_spec(r) for r in raw["controls"]) if s is not None]
    except (OSError, ValueError, KeyError, TypeError, AttributeError):
        return None
    if not specs:
        return None
    # A control that points at a group the file doesn't list is left ungrouped.
    for spec in specs:
        if spec["group"] is not None and not 0 <= spec["group"] < len(groups):
            spec["group"] = None
    return specs, groups


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
        # Bind to any free port now, so we can receive replies before the first
        # send (on Windows, recvfrom on an unbound socket is an error).
        self.sock.bind(("", 0))
        self.sock.setblocking(False)
        # On Windows, sending to an effect that isn't running makes the next
        # recvfrom fail with "connection reset". Turn that off where possible.
        if hasattr(socket, "SIO_UDP_CONNRESET"):
            self.sock.ioctl(socket.SIO_UDP_CONNRESET, False)
        # Qt calls receive() whenever a reply is waiting, so nothing blocks the UI.
        self.notifier = QSocketNotifier(self.sock.fileno(), QSocketNotifier.Read, self)
        self.notifier.activated.connect(self.receive)

        # Every control, by name, across all rows. Filled in by build();
        # send_all() loops over it.
        self.controls = {}

        # The sets of controls go into this grid once the file is read.
        self.controls_area = QGridLayout()
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
        loaded = load_controls(self.controls_path)
        if loaded is None:
            self.status.setText(f"Waiting for {self.controls_path} (start the effect program)...")
            return
        self.poll.stop()
        specs, groups = loaded
        self.build(specs, groups)

    def build(self, specs, groups):
        # Make one box for each group that has controls, in the order the groups
        # first appear in the file. Inside its box, each control goes into the
        # row its spec asks for. Controls with no group share a box with no outline.
        boxes = {}
        for spec in specs:
            number = spec["group"]
            if number not in boxes:
                if number is None:
                    boxes[number] = ControlGroup(outlined=False)
                else:
                    boxes[number] = ControlGroup(groups[number]["label"], groups[number]["color"])
            self.add_control(boxes[number].row(spec["row"]), spec)

        for box in boxes.values():
            box.finish()

        # Put the boxes on the grid. arrange() decides which cells each slot
        # covers. The boxes in a slot go side by side, each as wide as the
        # number of controls across it.
        columns = 0
        for slot in arrange(list(boxes.values())):
            band = QHBoxLayout()
            for box in slot["boxes"]:
                band.addWidget(box, box.widest_row())
            self.controls_area.addLayout(band, slot["top"], slot["left"],
                                         slot["bottom"] - slot["top"],
                                         slot["right"] - slot["left"])
            columns = max(columns, slot["right"])

        # Give every grid column an equal share of the window's width.
        for column in range(columns):
            self.controls_area.setColumnStretch(column, 1)

        host, port = self.addr
        self.status.setText(f"{len(self.controls)} controls from {self.controls_path}"
                            f"  ->  sending to udp://{host}:{port}")
        self.adjustSize()                 # grow the window to fit the new controls
        self.heartbeat.start(1000)
        self.send_all()

    def add_control(self, row, spec):
        widget = WIDGETS[spec["type"]](spec["label"], spec["default"], spec["units"],
                                       spec["positions"])
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

    def receive(self):
        """Read every waiting reply from the effect, and show each value under
        its control. Anything malformed or for an unknown control is ignored."""
        while True:
            try:
                data, _sender = self.sock.recvfrom(256)
            except (BlockingIOError, InterruptedError):
                return                  # nothing more waiting
            except OSError:
                return                  # e.g. a reset on Windows; the next reply will come through
            try:
                name, text = data.decode("ascii").split()
                value = float(text)
            except (UnicodeDecodeError, ValueError):
                continue
            widget = self.controls.get(name)
            if widget is not None:
                widget.set_display(value)


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
