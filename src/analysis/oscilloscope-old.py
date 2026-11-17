"""Oscilloscope and spectrum analyser for the effect program.

The effect program (C) copies audio out of its callback with scope_write() and
streams it here over UDP (see scope_tap.h). This panel listens on the port,
keeps the last few seconds of each channel, and shows them two ways. One
button ("Show spectrum" / "Show oscilloscope", or F2) switches between them:

    Oscilloscope        the signal against time, as a bench scope would draw
                        it: a triggered, steady picture of a repeating signal
    Spectrum analyser   the signal against frequency: how strong it is at each
                        frequency, or how the two channels differ

    Channel A   by convention the reference signal (e.g. the dry input)
    Channel B   by convention the signal under test (e.g. the processed output)

Sample values are shown as they are in the C code: 1.0 is full scale (0 dBFS).

Both views
    Run / Stop      (or the space bar) freezes the picture. While stopped you
                    can still change how it is displayed, and switch views to
                    see the same captured samples the other way.
    Channel A / B   the tick boxes show or hide each trace.
    Test signal     plays a sine wave or noise in the effect program, in place
                    of its input (this needs scope_test_signal() in the audio
                    path, see scope_tap.h). It comes out of the speakers too,
                    so start quiet. It stops when this panel is closed.

The oscilloscope
    Time/div        how much time one horizontal division covers (mouse wheel
                    over the screen also changes it)
    Scale, Position per channel: how much signal one vertical division covers,
                    and where the channel's zero line sits
    Trigger         the picture is lined up so the source channel crosses Level,
                    in the direction of Edge, at the centre of the screen.
                    Auto draws something even when there is no crossing.
                    Normal only redraws on a crossing, and otherwise holds the
                    last picture, which is what you want for one-off events.
    Single          waits for the next trigger, draws it, and stops.
    Mouse           moving over the screen reads off the time and both channels'
                    values under the pointer. Click to drop a reference line;
                    the readout then also shows the time between the line and
                    the pointer (and the frequency that period would be).
                    Right-click removes the line.

The spectrum analyser
    Show            Spectrum draws each channel's level at each frequency, in
                    dB. A sine wave that peaks at 1.0 reads 0 dB.
                    Response draws how much louder or quieter B is than A at
                    each frequency: the frequency response of whatever sits
                    between the two taps (a tone stack, a cabinet filter).
                    Feed it noise from the test signal, so there is something
                    to measure at every frequency. Where the curve turns grey
                    the measurement can't be trusted: A has nothing there, or B
                    isn't just a filtered copy of A (distortion does that, and
                    so does a long delay or reverb).
    FFT size        how many samples each analysis covers. More gives finer
                    frequency detail, but reacts more slowly.
    Window          how each block of samples is faded in and out before it is
                    analysed (the tool tip says when to use which).
    Averaging       steadies a noisy picture.
    Peak hold       also draws, faintly, the highest level seen so far.
    Measurements    the strongest tone in each channel, and its distortion
                    (THD). Use the sine test signal for this.
    Mouse           moving over the screen reads off the frequency and each
                    curve's level under the pointer.

Either program can be started first.

Requires: PySide6, numpy
Run:      python oscilloscope.py [--host 127.0.0.1] [--port 9000] [--fps 40] [--spectrum]
"""
import argparse
import math
from collections import deque
import socket
import struct
import sys
import time

import numpy as np
from PySide6.QtCore import QLineF, QPointF, QRectF, Qt, QTimer
from PySide6.QtGui import (QColor, QKeySequence, QPainter, QPalette, QPen,
                           QPolygonF, QShortcut)
from PySide6.QtWidgets import (QApplication, QCheckBox, QComboBox,
                               QDoubleSpinBox, QFormLayout, QGridLayout,
                               QGroupBox, QHBoxLayout, QLabel, QMessageBox,
                               QPushButton, QSpinBox, QStackedWidget,
                               QVBoxLayout, QWidget)


# ---------------------------------------------------------------------------
# The wire format. This must match scope_tap.h.
# ---------------------------------------------------------------------------
MAGIC = b"SCP1"
# "<" = little-endian. 4s = the magic, H = channel, H = count, I = sample rate,
# I = dropped, Q = index of the first sample. 24 bytes, then count floats.
HEADER = struct.Struct("<4sHHIIQ")

# How many samples of each channel are kept (a power of 2). At 48 kHz this is
# about 5.5 seconds, which is more than the longest screen (2 seconds) and the
# longest FFT (65536 samples, 1.4 seconds).
HISTORY = 1 << 18


# ---------------------------------------------------------------------------
# The screens
# ---------------------------------------------------------------------------
H_DIVS = 10                 # divisions across
V_DIVS = 8                  # divisions up and down

# The choices on the Time/div and Scale controls, in the usual 1-2-5 steps.
# 0.25 is in the scales because at 0.25 per division the 8 divisions cover
# exactly -1.0 to +1.0, so full scale fills the screen.
TIME_STEPS_MS = [0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100, 200]
SCALE_STEPS = [0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.25, 0.5, 1, 2]

# After crossing the trigger level, the signal has to drop this far below it
# (in divisions) before it can trigger again. Without this, noise on a slow
# edge would trigger several times on the way through, and noise on an edge
# going the other way could trigger too. The price is that a signal smaller
# than this on the screen can't trigger; turn the channel's Scale up to see it.
TRIGGER_HYSTERESIS_DIVS = 0.3

# A channel that has sent nothing for this long is treated as not connected.
SIGNAL_TIMEOUT_S = 0.5

# The spectrum analyser's choices.
FFT_SIZES = [1024, 2048, 4096, 8192, 16384, 32768, 65536]
AVERAGING_STEPS = [1, 2, 4, 8, 16, 32, 64]
# The windows, and how many points either side of a tone's peak each one
# spreads the tone over (see make_window(), and the THD measurement).
WINDOWS = {"Hann": 2, "Blackman-Harris": 4, "Flat top": 5, "Rectangular": 1}

# The response (B / A) is only meaningful as an average over several blocks, so
# that view never averages fewer than this. And it is only trusted where the
# "coherence" between the channels is at least COHERENCE_FLOOR (see
# SpectrumAnalyzer.response()).
RESPONSE_MIN_AVERAGING = 8
COHERENCE_FLOOR = 0.5

# The most blocks the analyser works through in one frame. If the window has
# been busy and more than this have piled up, it skips to the newest.
MAX_BLOCKS_PER_FRAME = 16

CHANNEL_NAMES = ["A", "B"]
# The two trace colours. Chosen to stay apart for colour-blind viewers, and to
# be readable on the dark screen.
CHANNEL_COLORS = ["#c98500", "#3987e5"]

SCREEN_COLOR = "#1a1a19"    # the screen's background
GRID_COLOR = "#2c2c2a"      # division lines
AXIS_COLOR = "#4a4a46"      # the two centre lines, and the border
TEXT_COLOR = "#f0efec"      # values
LABEL_COLOR = "#c3c2b7"     # labels
MUTED_COLOR = "#898781"     # anything that should sit in the background


# ---------------------------------------------------------------------------
# Showing values
# ---------------------------------------------------------------------------
def format_time(seconds):
    """Text like "250 us", "1.25 ms" or "1.20 s"."""
    size = abs(seconds)
    if size < 1e-3:
        return f"{seconds * 1e6:.0f} us"
    if size < 1:
        return f"{seconds * 1e3:.3g} ms"
    return f"{seconds:.3g} s"


def format_frequency(hz):
    """Text like "82.4 Hz" or "1.20 kHz"."""
    if hz >= 1000:
        return f"{hz / 1000:.3f} kHz"
    return f"{hz:.1f} Hz"


def format_step(value):
    """A choice on the Time/div or Scale control, without a trailing ".0"."""
    return f"{value:g}"


def format_db(linear):
    """A linear level as dBFS (1.0 = 0 dB)."""
    if linear <= 0:
        return "-inf dB"
    return f"{20 * math.log10(linear):+.1f} dB"


def strip_writer(painter):
    """The readouts above and below each screen are written left to right, one
    item after another. This returns the two functions that write an item,
    text() and swatch(). Each returns the x where the next item can start."""
    metrics = painter.fontMetrics()

    def text(x, baseline, string, color):
        painter.setPen(QColor(color))
        painter.drawText(QPointF(x, baseline), string)
        return x + metrics.horizontalAdvance(string)

    def swatch(x, baseline, color):
        # Text stays in the neutral text colours. The small coloured square
        # beside it is what says which trace it belongs to.
        painter.setPen(Qt.NoPen)
        painter.setBrush(color)
        painter.drawRoundedRect(QRectF(x, baseline - 9, 10, 10), 2, 2)
        return x + 15

    return text, swatch


# ---------------------------------------------------------------------------
# Finding where a signal crosses a level
# ---------------------------------------------------------------------------
def rising_crossings(x, level, hysteresis, state=0):
    """Find every place x rises through level.

    A crossing only counts if the signal was at or below (level - hysteresis)
    at some point since the previous crossing. Returns (positions, state).

    positions are fractional indexes into x: a crossing that falls a third of
    the way between x[9] and x[10] is 9.33. Lining the picture up on that
    fraction, instead of on a whole sample, is what stops the trace jittering
    sideways when it is zoomed right in.

    state lets one search carry on from where the last one stopped. It
    describes x[0]: 1 means the signal is above the level (or has been, and
    hasn't yet dropped below the hysteresis band), -1 means it has dropped
    below and can trigger again, and 0 means unknown. The returned state
    describes the last sample, ready to be passed to the next call.
    """
    n = len(x)
    if n < 2:
        return np.empty(0), state

    # Mark each sample: 1 = at or above the level, -1 = below the band,
    # 0 = inside the band (neither).
    marks = np.zeros(n, dtype=np.int8)
    marks[x >= level] = 1
    marks[x <= level - hysteresis] = -1
    if state != 0:
        marks[0] = state

    # A sample inside the band takes the mark of the last sample that was
    # outside it. "last" is, for each sample, the index of that earlier sample.
    last = np.where(marks != 0, np.arange(n), 0)
    np.maximum.accumulate(last, out=last)
    states = marks[last]

    # A crossing is a step from -1 to 1.
    hits = np.nonzero((states[1:] == 1) & (states[:-1] == -1))[0] + 1

    # Work out where, between the two samples, the level was actually crossed.
    before = x[hits - 1].astype(np.float64)
    after = x[hits].astype(np.float64)
    rise = after - before
    fraction = np.ones(len(hits))
    np.divide(level - before, rise, out=fraction, where=rise > 0)
    np.clip(fraction, 0.0, 1.0, out=fraction)

    return hits - 1 + fraction, int(states[-1])


# ---------------------------------------------------------------------------
# One channel: its recent samples, and how it is drawn
# ---------------------------------------------------------------------------
class Channel:
    """The last HISTORY samples of one channel.

    Samples are addressed by their index on the effect program's sample clock
    (the "index" field of each packet), not by where they sit in the array. So
    sample 1,000,000 of channel A and sample 1,000,000 of channel B were in the
    audio callback at the same moment, whenever their packets arrived here.
    """

    def __init__(self, name, color):
        self.name = name
        self.color = QColor(color)

        # How the channel is drawn. Set by the controls.
        self.enabled = True
        self.scale = 0.25               # signal per division
        self.position = 0.0             # where zero sits, in divisions above the centre

        self.data = np.zeros(HISTORY, dtype=np.float32)
        self.start = 0                  # index of the oldest sample still held
        self.end = 0                    # index one past the newest sample
        self.has_data = False

        self.last_packet = 0.0          # when the last packet arrived (time.monotonic)
        self.missing = 0                # samples that never arrived (shown in the status line)
        self.bad = 0                    # NaN or infinite samples (shown in the status line)

    def clear(self):
        self.start = self.end = 0
        self.has_data = False

    def write(self, index, samples):
        """Add a packet's samples. index is the position of the first one."""
        if not self.has_data or index < self.end or index - self.end > HISTORY:
            # The first packet, or the clock jumped: the effect program was
            # restarted (its clock starts again from 0). Start afresh.
            self.start = self.end = index
            self.has_data = True
        elif index > self.end:
            # Some samples never arrived. Fill the hole with silence so the
            # samples after it still land at the right index.
            gap = index - self.end
            self.missing += gap
            self._append(np.zeros(gap, dtype=np.float32))

        if not np.isfinite(samples).all():
            # A NaN or infinity can't be drawn, and would spoil every
            # measurement. Count it (it usually means a filter has blown up)
            # and draw it as zero.
            self.bad += int(np.count_nonzero(~np.isfinite(samples)))
            samples = np.nan_to_num(samples, nan=0.0, posinf=0.0, neginf=0.0)
        self._append(samples)

    def _append(self, samples):
        # The array is used as a ring: a sample's place is its index, wrapped
        # to the array's length. Copy in two pieces, up to the end of the
        # array and then the rest from the start.
        n = len(samples)
        place = self.end % HISTORY
        first = min(n, HISTORY - place)
        self.data[place:place + first] = samples[:first]
        self.data[:n - first] = samples[first:]
        self.end += n
        self.start = max(self.start, self.end - HISTORY)

    def read(self, begin, end):
        """The samples from index begin up to (not including) end, cut down to
        the part that is actually held. Returns (index of the first sample
        returned, the samples)."""
        begin = max(begin, self.start)
        end = min(end, self.end)
        n = end - begin
        if n <= 0:
            return begin, np.empty(0, dtype=np.float32)
        place = begin % HISTORY
        if place + n <= HISTORY:
            return begin, self.data[place:place + n]
        return begin, np.concatenate((self.data[place:], self.data[:place + n - HISTORY]))

    def value_at(self, position):
        """The signal at a fractional index (a straight line between the two
        samples either side), or None if those samples aren't held."""
        whole = math.floor(position)
        first, pair = self.read(whole, whole + 2)
        if first != whole or len(pair) < 2:
            return None
        fraction = position - whole
        return float(pair[0]) * (1 - fraction) + float(pair[1]) * fraction


# ---------------------------------------------------------------------------
# The oscilloscope's screen
# ---------------------------------------------------------------------------
class ScopeScreen(QWidget):
    """Draws the time view: the grid, the traces, the markers and the readouts.

    It holds no settings of its own. Everything it draws comes from the
    Oscilloscope it is given."""

    # Room around the grid: the top and bottom strips hold the readouts, and
    # the left and right margins hold the zero-line and trigger markers.
    LEFT, TOP, RIGHT, BOTTOM = 16, 28, 16, 26

    def __init__(self, scope):
        super().__init__()
        self.scope = scope
        self.pointer = None             # where the mouse is, or None when it is off the grid
        self.setMinimumSize(640, 440)
        self.setMouseTracking(True)     # report mouse moves even with no button held

    # -- geometry -----------------------------------------------------------
    def grid_rect(self):
        return QRectF(self.LEFT, self.TOP, self.width() - self.LEFT - self.RIGHT,
                      self.height() - self.TOP - self.BOTTOM)

    def level_to_y(self, grid, channel, level):
        """Where a signal level sits on the screen, for one channel."""
        division = grid.height() / V_DIVS
        return grid.center().y() - (level / channel.scale + channel.position) * division

    def x_to_offset(self, grid, x):
        """A horizontal place on the screen, as samples from the centre."""
        return (x - grid.center().x()) / grid.width() * self.scope.window_samples()

    def offset_to_x(self, grid, offset):
        return grid.center().x() + offset / self.scope.window_samples() * grid.width()

    # -- mouse --------------------------------------------------------------
    def mouseMoveEvent(self, event):
        position = event.position()
        self.pointer = position if self.grid_rect().contains(position) else None
        self.update()

    def leaveEvent(self, _event):
        self.pointer = None
        self.update()

    def mousePressEvent(self, event):
        grid = self.grid_rect()
        if event.button() == Qt.LeftButton and grid.contains(event.position()):
            self.scope.reference = self.x_to_offset(grid, event.position().x())
        elif event.button() == Qt.RightButton:
            self.scope.reference = None
        self.update()

    def wheelEvent(self, event):
        # Wheel up zooms in (less time per division).
        if event.angleDelta().y() != 0:
            self.scope.step_time_div(-1 if event.angleDelta().y() > 0 else 1)

    # -- drawing ------------------------------------------------------------
    def paintEvent(self, _event):
        scope = self.scope
        grid = self.grid_rect()
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor(SCREEN_COLOR))

        self.draw_grid(painter, grid)

        if scope.centre is not None:
            painter.setClipRect(grid)
            for channel in scope.channels:
                if channel.enabled:
                    self.draw_trace(painter, grid, channel)
            painter.setClipping(False)
        elif not any(channel.has_data for channel in scope.channels):
            painter.setPen(QColor(MUTED_COLOR))
            painter.drawText(grid, Qt.AlignCenter, scope.waiting_text)

        painter.setRenderHint(QPainter.Antialiasing, True)
        self.draw_markers(painter, grid)
        self.draw_cursors(painter, grid)
        self.draw_readouts(painter, grid)

    def draw_grid(self, painter, grid):
        painter.setRenderHint(QPainter.Antialiasing, False)
        column = grid.width() / H_DIVS
        row = grid.height() / V_DIVS

        painter.setPen(QPen(QColor(GRID_COLOR), 1))
        for i in range(1, H_DIVS):
            x = round(grid.left() + i * column)
            painter.drawLine(x, round(grid.top()), x, round(grid.bottom()))
        for i in range(1, V_DIVS):
            y = round(grid.top() + i * row)
            painter.drawLine(round(grid.left()), y, round(grid.right()), y)

        # The two centre lines are brighter, with small ticks every fifth of a
        # division, for reading values off by eye.
        painter.setPen(QPen(QColor(AXIS_COLOR), 1))
        centre_x = round(grid.center().x())
        centre_y = round(grid.center().y())
        painter.drawLine(centre_x, round(grid.top()), centre_x, round(grid.bottom()))
        painter.drawLine(round(grid.left()), centre_y, round(grid.right()), centre_y)
        for i in range(H_DIVS * 5 + 1):
            x = round(grid.left() + i * column / 5)
            painter.drawLine(x, centre_y - 2, x, centre_y + 2)
        for i in range(V_DIVS * 5 + 1):
            y = round(grid.top() + i * row / 5)
            painter.drawLine(centre_x - 2, y, centre_x + 2, y)
        painter.drawRect(grid)

    def draw_trace(self, painter, grid, channel):
        scope = self.scope
        window = scope.window_samples()
        left_edge = scope.centre - window / 2           # the sample index at the left of the grid

        first, y = channel.read(math.floor(left_edge), math.ceil(left_edge + window) + 1)
        n = len(y)
        if n < 2:
            return
        per_sample = grid.width() / window              # how many pixels apart two samples are
        x = grid.left() + (first - left_edge + np.arange(n)) * per_sample

        def to_y(level):
            y = grid.center().y() - (level / channel.scale + channel.position) * (grid.height() / V_DIVS)
            # Keep far-off-screen points within a sensible distance of the grid.
            return np.clip(y, grid.top() - 100, grid.bottom() + 100)

        if per_sample < 0.5:
            # Several samples share each pixel. Drawing them all would be slow,
            # and drawing every Nth would hide peaks. So each column of pixels
            # gets one vertical line, from the lowest sample that falls in it
            # to the highest, which shows the signal's true envelope.
            column = np.floor(x).astype(np.int64)
            starts = np.flatnonzero(np.diff(column, prepend=column[0] - 1))    # the first sample in each column
            low = np.minimum.reduceat(y, starts)
            high = np.maximum.reduceat(y, starts)

            # Stretch each line to reach the one before it, so a steep slope
            # is drawn as a continuous stroke rather than a row of dashes.
            bottom = low.copy()
            top = high.copy()
            bottom[1:] = np.minimum(low[1:], high[:-1])
            top[1:] = np.maximum(high[1:], low[:-1])

            top = to_y(top)                                 # (a higher level is a smaller y)
            bottom = np.maximum(to_y(bottom), top + 1)      # at least one pixel tall
            across = (column[starts] + 0.5).tolist()
            painter.setRenderHint(QPainter.Antialiasing, False)
            painter.setPen(QPen(channel.color, 1))
            painter.drawLines([QLineF(px, t, px, b) for px, t, b in zip(across, top.tolist(), bottom.tolist())])
            return

        # A one-pixel pen, on purpose. Qt draws thin lines with a fast routine,
        # but it outlines and fills anything wider, which takes hundreds of
        # milliseconds for a trace as jagged as noise.
        points = QPolygonF([QPointF(px, py) for px, py in zip(x.tolist(), to_y(y).tolist())])
        pen = QPen(channel.color, 1)
        pen.setCosmetic(True)
        painter.setRenderHint(QPainter.Antialiasing, True)
        painter.setPen(pen)
        painter.drawPolyline(points)

        # Zoomed right in, show where the actual samples are.
        if per_sample >= 10:
            painter.setPen(QPen(channel.color, 5, Qt.SolidLine, Qt.RoundCap))
            painter.drawPoints(points)

    def draw_markers(self, painter, grid):
        """The small arrows in the margins: each channel's zero line (left),
        the trigger level (right) and the trigger point (top)."""
        scope = self.scope
        painter.setPen(Qt.NoPen)

        # Zero lines. An arrow that would be off the screen stops at the edge.
        for channel in scope.channels:
            if channel.enabled:
                y = min(max(self.level_to_y(grid, channel, 0.0), grid.top()), grid.bottom())
                painter.setBrush(channel.color)
                painter.drawPolygon(QPolygonF([QPointF(grid.left() - 12, y - 5),
                                               QPointF(grid.left() - 2, y),
                                               QPointF(grid.left() - 12, y + 5)]))

        # Trigger level, in the trigger source's colour.
        source = scope.channels[scope.trig_source]
        y = min(max(self.level_to_y(grid, source, scope.trig_level), grid.top()), grid.bottom())
        painter.setBrush(source.color)
        painter.drawPolygon(QPolygonF([QPointF(grid.right() + 12, y - 5),
                                       QPointF(grid.right() + 2, y),
                                       QPointF(grid.right() + 12, y + 5)]))

        # Trigger point: the centre of the screen.
        x = grid.center().x()
        painter.setBrush(QColor(LABEL_COLOR))
        painter.drawPolygon(QPolygonF([QPointF(x - 5, grid.top() - 9),
                                       QPointF(x + 5, grid.top() - 9),
                                       QPointF(x, grid.top() - 1)]))

    def draw_cursors(self, painter, grid):
        scope = self.scope

        # The reference line (set by clicking).
        if scope.reference is not None:
            x = self.offset_to_x(grid, scope.reference)
            if grid.left() <= x <= grid.right():
                painter.setPen(QPen(QColor(LABEL_COLOR), 1, Qt.DashLine))
                painter.drawLine(QPointF(x, grid.top()), QPointF(x, grid.bottom()))

        # The line under the mouse, with a dot where it meets each trace.
        if self.pointer is None or scope.centre is None:
            return
        x = self.pointer.x()
        painter.setPen(QPen(QColor(MUTED_COLOR), 1))
        painter.drawLine(QPointF(x, grid.top()), QPointF(x, grid.bottom()))

        position = scope.centre + self.x_to_offset(grid, x)
        painter.setClipRect(grid.adjusted(-6, -6, 6, 6))
        for channel in scope.channels:
            value = channel.value_at(position) if channel.enabled else None
            if value is not None:
                # The ring in the screen's colour keeps the dot visible where
                # it sits on top of the other trace.
                painter.setPen(QPen(QColor(SCREEN_COLOR), 2))
                painter.setBrush(channel.color)
                painter.drawEllipse(QPointF(x, self.level_to_y(grid, channel, value)), 5, 5)
        painter.setClipping(False)

    def draw_readouts(self, painter, grid):
        """The text in the strips above and below the grid."""
        scope = self.scope
        metrics = painter.fontMetrics()
        gap = metrics.horizontalAdvance("    ")

        text, swatch = strip_writer(painter)

        # Top strip, left: each channel's scale, then the trigger settings.
        baseline = grid.top() - 10
        x = grid.left()
        for channel in scope.channels:
            x = swatch(x, baseline, channel.color)
            x = text(x, baseline, channel.name + "  ", LABEL_COLOR)
            if channel.enabled:
                x = text(x, baseline, f"{format_step(channel.scale)} /div", TEXT_COLOR)
            else:
                x = text(x, baseline, "off", MUTED_COLOR)
            x += gap
        x = max(x, grid.center().x() + 12)           # start to the right of the trigger-point arrow
        x = text(x, baseline, "Trig  ", LABEL_COLOR)
        x = swatch(x, baseline, scope.channels[scope.trig_source].color)
        text(x, baseline, f"{CHANNEL_NAMES[scope.trig_source]}  "
                          f"{'rising' if scope.trig_rising else 'falling'}  {scope.trig_level:+.3f}",
             TEXT_COLOR)

        # Top strip, right: what the scope is doing.
        width = metrics.horizontalAdvance(scope.state)
        text(grid.right() - width, baseline, scope.state, TEXT_COLOR)

        # Bottom strip, left: the timebase. Right: the sample rate.
        baseline = grid.bottom() + 18
        x = text(grid.left(), baseline, f"{format_step(scope.time_div_ms)} ms /div", TEXT_COLOR)
        rate = f"{scope.sample_rate / 1000:g} kHz"
        text(grid.right() - metrics.horizontalAdvance(rate), baseline, rate, MUTED_COLOR)

        # Bottom strip, middle: what is under the mouse.
        if self.pointer is None or scope.centre is None:
            return
        offset = self.x_to_offset(grid, self.pointer.x())
        x += gap
        x = text(x, baseline, "t  ", LABEL_COLOR)
        x = text(x, baseline, format_time(offset / scope.sample_rate), TEXT_COLOR) + gap
        for channel in scope.channels:
            value = channel.value_at(scope.centre + offset) if channel.enabled else None
            if value is not None:
                x = swatch(x, baseline, channel.color)
                x = text(x, baseline, f"{value:+.4f}", TEXT_COLOR) + gap
        if scope.reference is not None:
            delta = abs(offset - scope.reference) / scope.sample_rate
            x = text(x, baseline, "dt  ", LABEL_COLOR)
            string = format_time(delta)
            if delta > 0:
                string += f"  ({format_frequency(1 / delta)})"
            text(x, baseline, string, TEXT_COLOR)


# ---------------------------------------------------------------------------
# Frequency analysis
# ---------------------------------------------------------------------------
def make_window(name, n):
    """The window a block of samples is multiplied by before its FFT. A block
    cut out of a signal starts and stops abruptly, which smears every tone
    across the whole spectrum. A window fades the block in and out to stop
    that. Each one trades sharpness of the peaks against how far down the
    smearing is pushed."""
    k = 2 * np.pi * np.arange(n) / n
    if name == "Hann":
        return 0.5 - 0.5 * np.cos(k)
    if name == "Blackman-Harris":
        return 0.35875 - 0.48829 * np.cos(k) + 0.14128 * np.cos(2 * k) - 0.01168 * np.cos(3 * k)
    if name == "Flat top":
        return (0.21557895 - 0.41663158 * np.cos(k) + 0.277263158 * np.cos(2 * k)
                - 0.083578947 * np.cos(3 * k) + 0.006947368 * np.cos(4 * k))
    return np.ones(n)                   # "Rectangular": no window at all


class SpectrumAnalyzer:
    """Averaged spectra of the channels, and the response between them.

    The samples are cut into blocks of `size`, each starting half a block after
    the last. Each block is windowed and transformed (FFT), and the results are
    averaged. Every block of both channels starts at the same sample index, so
    the two spectra always describe the same moment, which is what lets them be
    divided to give the response.
    """

    def __init__(self, channels):
        self.channels = channels

        # Set by the controls (through configure()).
        self.size = 8192                # samples per block. Bigger = finer frequency detail, slower to react
        self.window_name = "Hann"
        self.averaging = 4              # how many blocks the average covers. 1 = no averaging
        self.hold = False               # also keep the highest level each frequency has reached
        self.min_averaging = 1          # the response needs averaging to mean anything (see response())

        self.window = None              # the window for the current size and name, made when first needed
        self.reset()

    def configure(self, **changes):
        for name, value in changes.items():
            setattr(self, name, value)
        self.window = None
        self.reset()

    def reset(self):
        """Throw the averages away and start again from the recent samples."""
        self.position = None            # the index where the next block starts, or None to start again
        self.used = ()                  # the names of the channels the averages were made from
        # The results of the last few blocks, newest last. Each is (a list of
        # the channels' power spectra, the cross-spectrum of A and B). The
        # averages below are the mean of these.
        self.blocks = deque(maxlen=max(self.averaging, self.min_averaging))
        self.power = [None] * len(self.channels)    # each channel's averaged power spectrum
        self.peak = [None] * len(self.channels)     # each channel's highest power so far (for Peak hold)
        self.cross = None               # the averaged cross-spectrum of channels A and B (for the response)

    def update(self, live):
        """Analyse every whole block that has arrived since the last call.
        live is the list of channels that are connected."""
        if not live:
            return
        n = self.size
        hop = n // 2
        start = max(channel.start for channel in live)
        end = min(channel.end for channel in live)

        used = tuple(channel.name for channel in live)
        if used != self.used or self.position is None or not start <= self.position <= end:
            # Starting again (new settings, a channel came or went, or the
            # effect program restarted). Begin far enough back that the average
            # is already full, instead of building up from one block.
            self.reset()
            self.used = used
            back = min(self.blocks.maxlen, MAX_BLOCKS_PER_FRAME) - 1
            self.position = max(start, end - n - back * hop)
        # If the window was busy and a lot has piled up, skip to the recent part.
        self.position = max(self.position, end - n - (MAX_BLOCKS_PER_FRAME - 1) * hop)

        if self.window is None:
            self.window = make_window(self.window_name, n)
        # Scaled so a sine wave reads its own amplitude: a sine that peaks at
        # 1.0 comes out as 1.0 (0 dB) whatever the window or block size.
        scale = 2 / self.window.sum()

        added = False
        while self.position + n <= end:
            powers = [None] * len(self.channels)
            spectra = [None] * len(self.channels)
            for channel in live:
                number = self.channels.index(channel)
                _first, x = channel.read(self.position, self.position + n)
                spectrum = np.fft.rfft(x * self.window) * scale
                power = spectrum.real ** 2 + spectrum.imag ** 2
                spectra[number] = spectrum
                # Kept as 32-bit floats: there can be 64 of these, each up to
                # 32769 numbers long.
                powers[number] = power.astype(np.float32)
                if self.hold:
                    self.peak[number] = power if self.peak[number] is None else np.maximum(self.peak[number], power)
            cross = None
            if spectra[0] is not None and spectra[1] is not None:
                cross = (np.conj(spectra[0]) * spectra[1]).astype(np.complex64)

            self.blocks.append((powers, cross))     # the oldest block drops off the other end
            self.position += hop
            added = True

        if added:
            # A plain average of the blocks that are held. (Not a running
            # "leaky" average: with this, when a tone stops it is gone from the
            # picture after that many blocks, instead of fading for seconds.)
            for number in range(len(self.channels)):
                held = [powers[number] for powers, _cross in self.blocks if powers[number] is not None]
                self.power[number] = np.mean(held, axis=0, dtype=np.float64) if held else None
            held = [cross for _powers, cross in self.blocks if cross is not None]
            self.cross = np.mean(held, axis=0, dtype=np.complex128) if held else None

    def frequencies(self, sample_rate):
        """The frequency of each point of the spectra, in Hz."""
        return np.fft.rfftfreq(self.size, 1 / sample_rate)

    def level_db(self, number, peak=False):
        """One channel's spectrum in dB (0 dB = a sine that peaks at 1.0), or
        None if there isn't one yet."""
        power = self.peak[number] if peak else self.power[number]
        if power is None:
            return None
        return 10 * np.log10(np.maximum(power, 1e-30))

    def response(self):
        """How much louder or quieter B is than A at each frequency, in dB.
        Returns (gain in dB, trusted), or None if it can't be worked out yet.

        This is not simply B's spectrum divided by A's. It uses the
        cross-spectrum, which only keeps the part of B that follows A, so noise
        and anything else in B that didn't come from A averages away.

        trusted marks the frequencies where the answer means something. The
        "coherence" says how much of B is explained by A, from 0 to 1. It is low
        where A has no energy to measure with, and where B isn't a filtered
        copy of A (distortion, noise, or a delay that is long compared with a
        block). Those frequencies are not to be trusted.
        """
        if self.cross is None or len(self.blocks) < 2:
            return None
        a = self.power[0]
        b = self.power[1]
        size = np.abs(self.cross)
        gain = 20 * np.log10(np.maximum(size / np.maximum(a, 1e-30), 1e-15))
        coherence = size ** 2 / np.maximum(a * b, 1e-60)
        # "A has energy here" means within 60 dB of A's strongest frequency.
        # Lower than that is mostly the skirts that the window spreads around
        # A's strong frequencies, and those carry the gain of the frequency
        # they came from, not of the frequency they have landed on.
        return gain, (coherence >= COHERENCE_FLOOR) & (a > max(1e-14, 1e-6 * float(a.max())))

    def measure(self, number, sample_rate):
        """The strongest tone in one channel: its frequency, its level, and
        its distortion. Returns a dict of text keyed like
        SPECTRUM_MEASUREMENTS, or None."""
        power = self.power[number]
        if power is None:
            return None
        bins = len(power)
        lowest = max(1, math.ceil(20 * self.size / sample_rate))     # ignore anything below 20 Hz (and DC)
        k = lowest + int(np.argmax(power[lowest:bins - 1]))
        if power[k] < 1e-16:
            return None                 # below -160 dB: there is nothing there

        # A windowed tone doesn't land on one point of the spectrum. It is
        # spread over a few points either side (how many depends on the
        # window). So a tone is measured over that whole spread: its power is
        # the sum of the points, and its frequency is their centre of gravity.
        # That reads true wherever the tone falls between two points.
        spread = WINDOWS[self.window_name] + 1

        def tone_at(centre):
            first = max(centre - spread, 1)
            points = power[first:min(centre + spread + 1, bins)]
            total = float(points.sum())
            return total, float((np.arange(first, first + len(points)) * points).sum()) / max(total, 1e-300)

        # A tone stands well clear of the spectrum either side of it. Noise
        # doesn't: its highest point is only a little above its neighbours, and
        # "measuring" that would give numbers that mean nothing. So compare the
        # top with the typical level just outside the tone's spread.
        beside = np.concatenate((power[max(k - spread - 10, 1):max(k - spread - 1, 1)],
                                 power[k + spread + 2:k + spread + 11]))
        if len(beside) == 0 or power[k] < 30 * float(np.median(beside)):
            return None                 # less than 15 dB clear: not a tone

        tone, centre = tone_at(k)
        # The sum counts the tone's power several times over (once per point
        # it is spread across). The window's "noise bandwidth" is how many.
        if self.window is None:
            self.window = make_window(self.window_name, self.size)
        bandwidth = self.size * float((self.window ** 2).sum()) / float(self.window.sum()) ** 2
        result = {"freq": format_frequency(centre * sample_rate / self.size),
                  "level": f"{10 * math.log10(tone / bandwidth):+.1f} dB",
                  "thd": "--"}

        # Distortion (THD): the power in the harmonics (2x, 3x ... 10x the
        # tone's frequency) compared with the power in the tone.
        # This only means something when the input is a single sine.
        harmonics = 0.0
        counted = 0
        for multiple in range(2, 11):
            place = round(multiple * centre)
            if place + spread >= bins:
                break                   # this harmonic is above half the sample rate
            harmonics += tone_at(place)[0]
            counted += 1
        if counted:
            percent = 100 * math.sqrt(harmonics / tone)
            result["thd"] = f"{percent:.3f} %" if percent >= 0.001 else "< 0.001 %"
        return result


class SpectrumScreen(QWidget):
    """Draws the frequency view: level (up) against frequency (across).

    Like the scope's screen it holds no settings of its own. Everything comes
    from the Oscilloscope it is given, and that Oscilloscope's analyzer."""

    # Room around the grid. The left margin holds the dB labels, and the bottom
    # one holds the frequency labels with the readout strip underneath.
    LEFT, TOP, RIGHT, BOTTOM = 48, 28, 16, 44

    def __init__(self, panel):
        super().__init__()
        self.panel = panel
        self.pointer = None             # where the mouse is, or None when it is off the grid
        self.setMinimumSize(640, 440)
        self.setMouseTracking(True)

    # -- geometry -----------------------------------------------------------
    def grid_rect(self):
        return QRectF(self.LEFT, self.TOP, self.width() - self.LEFT - self.RIGHT,
                      self.height() - self.TOP - self.BOTTOM)

    def frequency_range(self):
        """The frequencies at the left and right edges. A log axis can't start
        at 0, so it starts at 20 Hz, the bottom of what can be heard."""
        return (20.0 if self.panel.log_axis else 0.0), self.panel.sample_rate / 2

    def db_range(self):
        """The levels at the top and bottom edges."""
        panel = self.panel
        if panel.response_mode:
            return panel.response_span, -panel.response_span
        return panel.spectrum_top, panel.spectrum_top - panel.spectrum_range

    def frequency_to_x(self, grid, frequency):
        """Works on one frequency or an array of them."""
        low, high = self.frequency_range()
        if self.panel.log_axis:
            across = np.log(np.maximum(frequency, 1e-3) / low) / math.log(high / low)
        else:
            across = (frequency - low) / (high - low)
        return grid.left() + across * grid.width()

    def x_to_frequency(self, grid, x):
        low, high = self.frequency_range()
        across = (x - grid.left()) / grid.width()
        if self.panel.log_axis:
            return low * (high / low) ** across
        return low + across * (high - low)

    def db_to_y(self, grid, db):
        top, bottom = self.db_range()
        return grid.top() + (top - db) / (top - bottom) * grid.height()

    # -- mouse --------------------------------------------------------------
    def mouseMoveEvent(self, event):
        position = event.position()
        self.pointer = position if self.grid_rect().contains(position) else None
        self.update()

    def leaveEvent(self, _event):
        self.pointer = None
        self.update()

    # -- drawing ------------------------------------------------------------
    def paintEvent(self, _event):
        panel = self.panel
        grid = self.grid_rect()
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor(SCREEN_COLOR))

        self.draw_grid(painter, grid)
        curves = self.curves(grid)

        painter.setRenderHint(QPainter.Antialiasing, True)
        painter.setClipRect(grid)
        for curve in curves:
            self.draw_curve(painter, grid, curve)
        painter.setClipping(False)

        if not curves:
            painter.setPen(QColor(MUTED_COLOR))
            if not any(channel.has_data for channel in panel.channels):
                painter.drawText(grid, Qt.AlignCenter, panel.waiting_text)
            elif panel.response_mode and len(panel.analyzer.used) < 2:
                painter.drawText(grid, Qt.AlignCenter, "The response needs both channels connected")
            elif any(channel.enabled for channel in panel.channels):
                painter.drawText(grid, Qt.AlignCenter, "Not enough samples yet for this FFT size")

        self.draw_readouts(painter, grid, curves)

    def draw_grid(self, painter, grid):
        painter.setRenderHint(QPainter.Antialiasing, False)
        metrics = painter.fontMetrics()
        low, high = self.frequency_range()

        # Frequency lines. On a log axis: 20, 30 ... 90, 100, 200 ... with a
        # label on the 1, 2 and 5 of each decade. On a linear axis: evenly
        # spaced, all labelled.
        if self.panel.log_axis:
            lines = [(digit * decade, digit in (1, 2, 5))
                     for decade in (10, 100, 1000, 10000) for digit in range(1, 10)]
        else:
            step = 1000 * max(1, round(high / 12000))
            lines = [(f, True) for f in range(step, int(high) + 1, step)]
        for frequency, labelled in lines:
            if not low <= frequency <= high:
                continue
            x = round(float(self.frequency_to_x(grid, frequency)))
            painter.setPen(QPen(QColor(GRID_COLOR), 1))
            painter.drawLine(x, round(grid.top()), x, round(grid.bottom()))
            if labelled:
                label = f"{frequency / 1000:g}k" if frequency >= 1000 else f"{frequency:g}"
                painter.setPen(QColor(MUTED_COLOR))
                painter.drawText(QPointF(x - metrics.horizontalAdvance(label) / 2, grid.bottom() + 15), label)

        # Level lines, at the smallest round step that gives no more than 12.
        top, bottom = self.db_range()
        step = next(s for s in (1, 2, 5, 10, 20, 50) if (top - bottom) / s <= 12)
        level = math.ceil(bottom / step) * step
        while level <= top:
            y = round(self.db_to_y(grid, level))
            # In the response view the 0 dB line (no change) is the reference, so it is brighter.
            bright = self.panel.response_mode and level == 0
            painter.setPen(QPen(QColor(AXIS_COLOR if bright else GRID_COLOR), 1))
            painter.drawLine(round(grid.left()), y, round(grid.right()), y)
            label = f"{level:+d}" if self.panel.response_mode and level else f"{level:d}"
            painter.setPen(QColor(MUTED_COLOR))
            painter.drawText(QPointF(grid.left() - 6 - metrics.horizontalAdvance(label), y + 4), label)
            level += step
        # The units go in the left margin, level with each axis's labels.
        painter.drawText(QPointF(4, grid.top() - 10), "dB")
        painter.drawText(QPointF(4, grid.bottom() + 15), "Hz")

        painter.setPen(QPen(QColor(AXIS_COLOR), 1))
        painter.setBrush(Qt.NoBrush)
        painter.drawRect(grid)

    def curves(self, grid):
        """Everything to be drawn, as a list of dicts: the colour, the points
        (x in pixels, level in dB), and how to draw them."""
        panel = self.panel
        analyzer = panel.analyzer
        frequencies = analyzer.frequencies(panel.sample_rate)
        curves = []

        if panel.response_mode:
            response = analyzer.response()
            if response is not None:
                points = self.thin(grid, frequencies, response[0], response[1], use_peak=False)
                if points is not None:
                    # One curve: B measured against A. Drawn in B's colour.
                    curves.append({"color": panel.channels[1].color, "x": points[0], "db": points[1],
                                   "trusted": points[2], "faint": False, "readout": True})
            return curves

        # The held peaks go first, so the live spectra are drawn over them.
        for faint in ((True, False) if analyzer.hold else (False,)):
            for number, channel in enumerate(panel.channels):
                level = analyzer.level_db(number, peak=faint) if channel.enabled else None
                points = self.thin(grid, frequencies, level) if level is not None else None
                if points is not None:
                    curves.append({"color": channel.color, "x": points[0], "db": points[1],
                                   "trusted": None, "faint": faint, "readout": not faint})
        return curves

    def thin(self, grid, frequencies, db, trusted=None, use_peak=True):
        """Turn a spectrum into points to draw. Returns (x, db, trusted).

        At the top of a log axis hundreds of points share each pixel. They are
        merged into one point per pixel column: the highest of them for a
        spectrum (so a narrow peak is never lost), the mean for a response."""
        x = self.frequency_to_x(grid, frequencies)
        on_screen = (x >= grid.left()) & (x <= grid.right())
        if np.count_nonzero(on_screen) < 2:
            return None
        x = x[on_screen]
        db = db[on_screen]

        column = np.floor(x).astype(np.int64)
        starts = np.flatnonzero(np.diff(column, prepend=column[0] - 1))    # the first point in each column
        counts = np.diff(np.append(starts, len(x)))
        merged_x = np.add.reduceat(x, starts) / counts
        if use_peak:
            merged_db = np.maximum.reduceat(db, starts)
        else:
            merged_db = np.add.reduceat(db, starts) / counts
        if trusted is not None:
            # A column is trusted only if every point in it is.
            trusted = np.minimum.reduceat(trusted[on_screen].astype(np.int8), starts).astype(bool)
        return merged_x, merged_db, trusted

    def draw_curve(self, painter, grid, curve):
        y = np.clip(self.db_to_y(grid, curve["db"]), grid.top() - 100, grid.bottom() + 100)
        points = [QPointF(px, py) for px, py in zip(curve["x"].tolist(), y.tolist())]

        color = QColor(curve["color"])
        if curve["faint"]:
            color.setAlpha(110)
        pen = QPen(color, 1)            # one pixel wide, for speed (see ScopeScreen.draw_trace)
        pen.setCosmetic(True)

        trusted = curve["trusted"]
        if trusted is None:
            painter.setPen(pen)
            painter.drawPolyline(QPolygonF(points))
            return

        # A response: the whole curve in grey, then the trusted stretches over
        # it in colour. "edges" are the places trusted switches on or off.
        grey = QPen(QColor(MUTED_COLOR), 1)
        grey.setCosmetic(True)
        painter.setPen(grey)
        painter.drawPolyline(QPolygonF(points))
        painter.setPen(pen)
        edges = np.flatnonzero(np.diff(np.concatenate(([0], trusted.astype(np.int8), [0]))))
        for begin, end in zip(edges[0::2].tolist(), edges[1::2].tolist()):
            if end - begin >= 2:
                painter.drawPolyline(QPolygonF(points[begin:end]))

    def draw_readouts(self, painter, grid, curves):
        """The text in the strips above and below the grid, and the line under
        the mouse."""
        panel = self.panel
        analyzer = panel.analyzer
        metrics = painter.fontMetrics()
        gap = metrics.horizontalAdvance("    ")
        text, swatch = strip_writer(painter)

        # Top strip, left: what each colour is, then the analysis settings.
        baseline = grid.top() - 10
        x = grid.left()
        if panel.response_mode:
            x = swatch(x, baseline, panel.channels[1].color)
            x = text(x, baseline, "B relative to A", TEXT_COLOR) + gap
            x = swatch(x, baseline, QColor(MUTED_COLOR))
            x = text(x, baseline, "not reliable here", LABEL_COLOR) + gap
        else:
            for channel in panel.channels:
                x = swatch(x, baseline, channel.color)
                x = text(x, baseline, channel.name, TEXT_COLOR if channel.enabled else MUTED_COLOR)
                if not channel.enabled:
                    x = text(x, baseline, "  off", MUTED_COLOR)
                x += gap
        blocks = analyzer.blocks.maxlen
        settings = (f"{analyzer.size} points    {panel.sample_rate / analyzer.size:.3g} Hz apart    "
                    f"{analyzer.window_name}    " + (f"average of {blocks}" if blocks > 1 else "no averaging"))
        text(x, baseline, settings, MUTED_COLOR)

        # Top strip, right: what the panel is doing.
        state = panel.view_state()
        text(grid.right() - metrics.horizontalAdvance(state), baseline, state, TEXT_COLOR)

        # Bottom strip, right: the sample rate.
        baseline = grid.bottom() + 36
        rate = f"{panel.sample_rate / 1000:g} kHz"
        text(grid.right() - metrics.horizontalAdvance(rate), baseline, rate, MUTED_COLOR)

        # The line under the mouse, a dot where it meets each curve, and in
        # the bottom strip the frequency and each curve's level there.
        if self.pointer is None:
            return
        px = self.pointer.x()
        painter.setPen(QPen(QColor(MUTED_COLOR), 1))
        painter.drawLine(QPointF(px, grid.top()), QPointF(px, grid.bottom()))

        x = text(grid.left(), baseline, "f  ", LABEL_COLOR)
        x = text(x, baseline, format_frequency(float(self.x_to_frequency(grid, px))), TEXT_COLOR) + gap
        for curve in curves:
            if not curve["readout"] or not curve["x"][0] <= px <= curve["x"][-1]:
                continue
            level = float(np.interp(px, curve["x"], curve["db"]))
            y = float(self.db_to_y(grid, level))
            if grid.top() <= y <= grid.bottom():
                # The ring in the screen's colour keeps the dot visible where
                # it sits on top of the other curve.
                painter.setPen(QPen(QColor(SCREEN_COLOR), 2))
                painter.setBrush(curve["color"])
                painter.drawEllipse(QPointF(px, y), 5, 5)
            x = swatch(x, baseline, curve["color"])
            x = text(x, baseline, f"{level:+.1f} dB", TEXT_COLOR) + gap


# ---------------------------------------------------------------------------
# The controls beside the screens
# ---------------------------------------------------------------------------
def step_box(steps, value, suffix):
    """A drop-down list of fixed numeric steps (Time/div, Scale, FFT size...).
    Each item carries its number, read back with itemData()."""
    box = QComboBox()
    for step in steps:
        box.addItem(f"{format_step(step)} {suffix}".strip(), step)
    box.setCurrentIndex(steps.index(value))
    return box


def choice_box(choices, current=0):
    """A drop-down list of named choices."""
    box = QComboBox()
    box.addItems(choices)
    box.setCurrentIndex(current)
    return box


def color_chip(color):
    """A small coloured square. Labels stay in the normal text colour; the
    chip beside them is what says which trace they belong to."""
    chip = QLabel()
    chip.setFixedSize(10, 10)
    chip.setStyleSheet(f"background: {QColor(color).name()}; border-radius: 2px;")
    return chip


def muted_label(text):
    label = QLabel(text)
    label.setStyleSheet(f"color: {LABEL_COLOR};")
    return label


# The rows of the two measurement tables: (label, the key into the result of
# Oscilloscope.measure() or SpectrumAnalyzer.measure())
SCOPE_MEASUREMENTS = [("Max", "max"), ("Min", "min"), ("Pk-Pk", "pp"), ("RMS", "rms"),
                      ("RMS dBFS", "rms_db"), ("Mean", "mean"), ("Freq", "freq")]
SPECTRUM_MEASUREMENTS = [("Peak", "freq"), ("Level", "level"), ("THD", "thd")]

# The test signals: what the control shows, and the word sent to the effect
# program (see THE COMMANDS in scope_tap.h).
TEST_SIGNALS = [("Off", "off"), ("Sine", "sine"), ("White noise", "white"), ("Pink noise", "pink")]


class Oscilloscope(QWidget):
    """The window: the two screens (only one is shown at a time), the controls
    beside them, and everything they share: the socket, the channels' samples,
    and whether the picture is running or frozen."""

    def __init__(self, host, port, fps):
        super().__init__()
        self.setWindowTitle("Oscilloscope")

        self.channels = [Channel(name, color) for name, color in zip(CHANNEL_NAMES, CHANNEL_COLORS)]
        self.sample_rate = 48000        # replaced by the rate in the first packet

        # -- shared by both views --
        self.running = True
        self.frozen = []                # the channels that were connected when the picture was stopped
        self.spectrum_view = False      # which screen is showing
        self.waiting_text = f"Waiting for the effect program on udp://{host}:{port}"
        self.effect = None              # the effect program's address, learned from its packets. Commands go here

        # -- the oscilloscope --
        self.time_div_ms = 1
        self.trig_source = 0            # which channel the trigger watches
        self.trig_rising = True
        self.trig_level = 0.0
        self.trig_auto = True           # Auto (True) or Normal (False)
        self.single = False             # True while waiting for a Single capture
        self.state = "No signal"        # shown at the top right of the scope's screen

        # The picture. centre is the sample index at the middle of the screen
        # (the trigger point), as a fraction, or None before the first picture.
        self.centre = None
        self.reference = None           # the reference line, in samples from the centre, or None

        # The trigger search carries on from where it stopped last frame.
        self.searched_to = None         # the index it has searched up to, or None to start again
        self.search_state = 0           # see rising_crossings()
        self.last_trigger = time.monotonic()

        # -- the spectrum analyser --
        self.analyzer = SpectrumAnalyzer(self.channels)
        self.response_mode = False      # False: each channel's spectrum. True: B measured against A
        self.log_axis = True
        self.spectrum_top = 0           # the level at the top of the screen, in dB
        self.spectrum_range = 120       # how many dB the screen covers, top to bottom
        self.response_span = 24         # the response view covers this many dB either side of 0

        # The socket. Bound to the scope port, and never blocks: receive()
        # just takes whatever has arrived since it last looked.
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        # Room for about a second of packets, in case the window is busy
        # (being dragged, say) and can't read for a moment.
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
        self.sock.bind((host, port))    # raises OSError if the port is in use
        self.sock.setblocking(False)
        # On Windows, sending a command to an effect program that has gone
        # makes the next receive fail with "connection reset". Turn that off
        # where possible.
        if hasattr(socket, "SIO_UDP_CONNRESET"):
            self.sock.ioctl(socket.SIO_UDP_CONNRESET, False)
        self.packet = bytearray(65536)  # the largest a UDP datagram can be
        self.address = f"udp://{host}:{port}"

        self.screen = ScopeScreen(self)
        self.spectrum_screen = SpectrumScreen(self)
        self.screens = QStackedWidget()             # shows one screen at a time
        self.screens.addWidget(self.screen)
        self.screens.addWidget(self.spectrum_screen)

        scope_page = QWidget()
        layout = QVBoxLayout(scope_page)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self.build_horizontal())
        layout.addWidget(self.build_vertical())
        layout.addWidget(self.build_trigger())
        group, self.scope_labels = self.build_measurements(
            SCOPE_MEASUREMENTS, "Levels are measured over what is on the screen.\n"
                                "Frequency is measured over at least the last 100 ms.")
        layout.addWidget(group)
        layout.addStretch()

        spectrum_page = QWidget()
        layout = QVBoxLayout(spectrum_page)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self.build_display())
        layout.addWidget(self.build_analysis())
        group, self.spectrum_labels = self.build_measurements(
            SPECTRUM_MEASUREMENTS, "The strongest tone in each channel.\n"
                                   "THD compares its harmonics (2x to 10x its frequency) with the tone itself,\n"
                                   "so it only means something when the input is a single sine.")
        layout.addWidget(group)
        layout.addStretch()

        self.pages = QStackedWidget()               # the controls that go with the screen that is showing
        self.pages.addWidget(scope_page)
        self.pages.addWidget(spectrum_page)

        side = QVBoxLayout()
        side.addLayout(self.build_buttons())
        side.addLayout(self.build_show_row())
        side.addWidget(self.pages, 1)
        side.addWidget(self.build_test_signal())

        self.status = QLabel()
        self.status.setStyleSheet(f"color: {MUTED_COLOR};")

        top = QHBoxLayout()
        top.addWidget(self.screens, 1)  # the screen takes all the spare width
        top.addLayout(side)
        layout = QVBoxLayout(self)
        layout.addLayout(top, 1)
        layout.addWidget(self.status)

        QShortcut(QKeySequence(Qt.Key_Space), self, activated=self.toggle_running)
        QShortcut(QKeySequence(Qt.Key_F2), self, activated=self.toggle_view)

        # One timer drives everything: read the socket, look for a trigger,
        # analyse, redraw. A slower one updates the numbers, so they can be
        # read, and another repeats the test signal's settings.
        self.frame_timer = QTimer(self)
        self.frame_timer.setTimerType(Qt.PreciseTimer)
        self.frame_timer.timeout.connect(self.frame)
        self.frame_timer.start(round(1000 / fps))
        self.measure_timer = QTimer(self)
        self.measure_timer.timeout.connect(self.update_measurements)
        self.measure_timer.start(250)
        self.command_timer = QTimer(self)
        self.command_timer.timeout.connect(self.send_test_signal)
        self.command_timer.start(1000)

    # -- building the controls ----------------------------------------------
    def build_buttons(self):
        self.run_button = QPushButton("Stop")
        self.run_button.setToolTip("Freeze or restart the picture (space bar)")
        self.run_button.clicked.connect(self.toggle_running)
        self.single_button = QPushButton("Single")
        self.single_button.setToolTip("Wait for the next trigger, draw it, and stop")
        self.single_button.clicked.connect(self.arm_single)
        self.view_button = QPushButton("Show spectrum")
        self.view_button.setToolTip("Switch between the oscilloscope and the spectrum analyser (F2)")
        self.view_button.clicked.connect(self.toggle_view)

        grid = QGridLayout()
        grid.addWidget(self.run_button, 0, 0)
        grid.addWidget(self.single_button, 0, 1)
        grid.addWidget(self.view_button, 1, 0, 1, 2)       # across both columns
        return grid

    def build_show_row(self):
        """One tick box per channel, to show or hide its trace in both views."""
        row = QHBoxLayout()
        for channel in self.channels:
            box = QCheckBox(f"Channel {channel.name}")
            box.setChecked(channel.enabled)
            # "c=channel" makes each control remember its own channel. Without
            # it, both channels' controls would end up changing channel B.
            box.toggled.connect(lambda on, c=channel: self.on_channel(c, enabled=on))
            row.addWidget(color_chip(channel.color))
            row.addWidget(box)
            row.addStretch()
        return row

    def build_horizontal(self):
        self.time_box = step_box(TIME_STEPS_MS, self.time_div_ms, "ms")
        self.time_box.currentIndexChanged.connect(self.on_time_div)
        group = QGroupBox("Horizontal")
        form = QFormLayout(group)
        form.addRow("Time/div", self.time_box)
        return group

    def build_vertical(self):
        group = QGroupBox("Vertical")
        grid = QGridLayout(group)
        grid.setColumnStretch(1, 1)
        grid.setColumnStretch(2, 1)
        grid.addWidget(muted_label("Scale"), 0, 1)
        grid.addWidget(muted_label("Position"), 0, 2)
        for row, channel in enumerate(self.channels, start=1):
            scale = step_box(SCALE_STEPS, channel.scale, "/div")
            position = QDoubleSpinBox()
            position.setRange(-V_DIVS, V_DIVS)
            position.setSingleStep(0.5)
            position.setDecimals(1)
            position.setSuffix(" div")
            position.setValue(channel.position)
            scale.currentIndexChanged.connect(lambda i, c=channel, box=scale: self.on_channel(c, scale=box.itemData(i)))
            position.valueChanged.connect(lambda v, c=channel: self.on_channel(c, position=v))

            name = QHBoxLayout()
            name.addWidget(color_chip(channel.color))
            name.addWidget(QLabel(channel.name))
            grid.addLayout(name, row, 0)
            grid.addWidget(scale, row, 1)
            grid.addWidget(position, row, 2)
        return group

    def build_trigger(self):
        mode = choice_box(["Auto", "Normal"])
        mode.currentIndexChanged.connect(lambda i: self.on_trigger(auto=(i == 0)))
        source = choice_box(CHANNEL_NAMES)
        source.currentIndexChanged.connect(lambda i: self.on_trigger(source=i))
        edge = choice_box(["Rising", "Falling"])
        edge.currentIndexChanged.connect(lambda i: self.on_trigger(rising=(i == 0)))
        level = QDoubleSpinBox()
        level.setRange(-4.0, 4.0)
        level.setSingleStep(0.01)
        level.setDecimals(3)
        level.setValue(self.trig_level)
        level.valueChanged.connect(lambda v: self.on_trigger(level=v))

        # Two controls to a row, to keep the panel short.
        group = QGroupBox("Trigger")
        grid = QGridLayout(group)
        grid.setColumnStretch(1, 1)
        grid.setColumnStretch(3, 1)
        for place, (title, widget) in enumerate([("Mode", mode), ("Source", source),
                                                 ("Edge", edge), ("Level", level)]):
            grid.addWidget(muted_label(title), place // 2, 2 * (place % 2))
            grid.addWidget(widget, place // 2, 2 * (place % 2) + 1)
        return group

    def build_display(self):
        mode = choice_box(["Spectrum", "Response (B / A)"])
        mode.setToolTip("Spectrum: how strong each channel is at each frequency.\n"
                        "Response: how much louder or quieter B is than A at each frequency,\n"
                        "which is the frequency response of whatever sits between the two taps.\n"
                        "It needs a broadband input: use the white or pink noise test signal.")
        mode.currentIndexChanged.connect(lambda i: self.on_display(response_mode=(i == 1)))
        axis = choice_box(["Log", "Linear"])
        axis.setToolTip("Log spaces the octaves evenly, the way pitch is heard.\n"
                        "Linear spaces the harmonics of a tone evenly, which makes aliasing easy to spot.")
        axis.currentIndexChanged.connect(lambda i: self.on_display(log_axis=(i == 0)))

        self.top_box = QSpinBox()
        self.top_box.setRange(-60, 40)
        self.top_box.setSingleStep(10)
        self.top_box.setSuffix(" dB")
        self.top_box.setValue(self.spectrum_top)
        self.top_box.valueChanged.connect(lambda v: self.on_display(spectrum_top=v))
        self.range_box = step_box([60, 90, 120, 150], self.spectrum_range, "dB")
        self.range_box.currentIndexChanged.connect(
            lambda i: self.on_display(spectrum_range=self.range_box.itemData(i)))
        self.span_box = QComboBox()
        for span in (6, 12, 24, 48):
            self.span_box.addItem(f"±{span} dB", span)       # the plus-or-minus sign
        self.span_box.setCurrentIndex(2)
        self.span_box.currentIndexChanged.connect(
            lambda i: self.on_display(response_span=self.span_box.itemData(i)))
        self.span_box.setEnabled(False)

        group = QGroupBox("Display")
        form = QFormLayout(group)
        form.addRow("Show", mode)
        form.addRow("Frequency axis", axis)
        form.addRow("Top", self.top_box)
        form.addRow("Range", self.range_box)
        form.addRow("Response range", self.span_box)
        return group

    def build_analysis(self):
        size = step_box(FFT_SIZES, self.analyzer.size, "")
        size.setToolTip("How many samples each analysis covers.\n"
                        "More gives finer frequency detail, but reacts more slowly.")
        size.currentIndexChanged.connect(lambda i: self.on_analysis(size=size.itemData(i)))
        window = choice_box(list(WINDOWS))
        window.setToolTip("Hann: a good general choice.\n"
                          "Blackman-Harris: fatter peaks, but shows detail 90 dB below them (harmonics, aliasing).\n"
                          "Flat top: the most accurate peak levels.\n"
                          "Rectangular: no window. Only for signals that fit the block exactly.")
        window.currentIndexChanged.connect(lambda i: self.on_analysis(window_name=window.itemText(i)))
        averaging = QComboBox()
        for blocks in AVERAGING_STEPS:
            averaging.addItem("Off" if blocks == 1 else str(blocks), blocks)
        averaging.setCurrentIndex(AVERAGING_STEPS.index(self.analyzer.averaging))
        averaging.setToolTip("How many analyses are averaged together. More steadies a noisy picture.")
        averaging.currentIndexChanged.connect(lambda i: self.on_analysis(averaging=averaging.itemData(i)))
        hold = QCheckBox("Peak hold")
        hold.setToolTip("Also draw, faintly, the highest level each frequency has reached.\n"
                        "Untick and tick again to start afresh.")
        hold.toggled.connect(lambda on: self.on_analysis(hold=on))

        group = QGroupBox("Analysis")
        form = QFormLayout(group)
        form.addRow("FFT size", size)
        form.addRow("Window", window)
        form.addRow("Averaging", averaging)
        form.addRow(hold)
        return group

    def build_measurements(self, rows, tooltip):
        """A table with a column per channel. Returns the group box, and a
        dict of the QLabels that show the values: (channel number, key) -> QLabel."""
        group = QGroupBox("Measurements")
        group.setToolTip(tooltip)
        grid = QGridLayout(group)
        grid.setColumnStretch(1, 1)
        grid.setColumnStretch(2, 1)
        for column, channel in enumerate(self.channels, start=1):
            # A coloured bar above each column says which channel it is.
            head = QLabel(channel.name)
            head.setAlignment(Qt.AlignRight)
            head.setStyleSheet(f"border-bottom: 2px solid {channel.color.name()}; font-weight: bold;")
            grid.addWidget(head, 0, column)

        labels = {}
        for row, (title, key) in enumerate(rows, start=1):
            grid.addWidget(muted_label(title), row, 0)
            for number in range(len(self.channels)):
                value = QLabel("--")
                value.setAlignment(Qt.AlignRight)
                value.setMinimumWidth(78)       # wide enough that the layout doesn't shift as the numbers change
                grid.addWidget(value, row, number + 1)
                labels[number, key] = value
        return group, labels

    def build_test_signal(self):
        self.signal_box = choice_box([title for title, _word in TEST_SIGNALS])
        self.signal_box.currentIndexChanged.connect(self.send_test_signal)
        self.signal_freq = QDoubleSpinBox()
        self.signal_freq.setRange(20, 20000)
        self.signal_freq.setDecimals(0)
        self.signal_freq.setSingleStep(10)
        self.signal_freq.setSuffix(" Hz")
        self.signal_freq.setValue(1000)
        self.signal_freq.valueChanged.connect(self.send_test_signal)
        self.signal_level = QDoubleSpinBox()
        self.signal_level.setRange(-80, 0)
        self.signal_level.setDecimals(0)
        self.signal_level.setSuffix(" dB")
        self.signal_level.setValue(-20)
        self.signal_level.valueChanged.connect(self.send_test_signal)

        group = QGroupBox("Test signal")
        group.setToolTip("Plays a test signal in the effect program, in place of its input.\n"
                         "This needs scope_test_signal() in the audio path (see scope_tap.h).\n"
                         "It comes out of the speakers too, so start quiet.")
        grid = QGridLayout(group)
        grid.addWidget(self.signal_box, 0, 0, 1, 2)
        grid.addWidget(self.signal_freq, 1, 0)
        grid.addWidget(self.signal_level, 1, 1)
        self.send_test_signal()         # sets which of the controls apply (nothing is sent yet)
        return group

    # -- the controls' actions ----------------------------------------------
    def window_samples(self):
        """How many samples the scope's screen is wide (usually not a whole number)."""
        return H_DIVS * self.time_div_ms / 1000 * self.sample_rate

    def on_time_div(self, index):
        self.time_div_ms = TIME_STEPS_MS[index]
        self.screen.update()

    def step_time_div(self, step):
        index = self.time_box.currentIndex() + step
        self.time_box.setCurrentIndex(min(max(index, 0), len(TIME_STEPS_MS) - 1))

    def on_channel(self, channel, **changes):
        for name, value in changes.items():
            setattr(channel, name, value)
        self.restart_search()           # the trigger's hysteresis depends on the source's scale
        self.screens.currentWidget().update()

    def on_trigger(self, auto=None, source=None, rising=None, level=None):
        if auto is not None:
            self.trig_auto = auto
        if source is not None:
            self.trig_source = source
        if rising is not None:
            self.trig_rising = rising
        if level is not None:
            self.trig_level = level
        self.restart_search()
        self.screen.update()

    def restart_search(self):
        """Forget where the trigger search had got to. Needed whenever what
        counts as a trigger changes."""
        self.searched_to = None
        self.search_state = 0

    def on_display(self, **changes):
        """A control in the spectrum's Display group changed."""
        for name, value in changes.items():
            setattr(self, name, value)
        if "response_mode" in changes:
            # One block's response is meaningless (see SpectrumAnalyzer.response),
            # so the response view always averages at least a few.
            self.analyzer.configure(min_averaging=RESPONSE_MIN_AVERAGING if self.response_mode else 1)
            self.top_box.setEnabled(not self.response_mode)
            self.range_box.setEnabled(not self.response_mode)
            self.span_box.setEnabled(self.response_mode)
            self.analyzer.update(self.live_channels())
        self.spectrum_screen.update()

    def on_analysis(self, **changes):
        """A control in the spectrum's Analysis group changed."""
        self.analyzer.configure(**changes)
        # Re-analyse the recent samples straight away with the new settings.
        # (This is also what makes the controls work while stopped.)
        self.analyzer.update(self.live_channels())
        self.spectrum_screen.update()

    def toggle_view(self):
        self.set_view(not self.spectrum_view)

    def set_view(self, spectrum):
        """Switch between the oscilloscope (False) and the spectrum analyser (True)."""
        self.spectrum_view = spectrum
        self.screens.setCurrentIndex(1 if spectrum else 0)
        self.pages.setCurrentIndex(1 if spectrum else 0)
        self.view_button.setText("Show oscilloscope" if spectrum else "Show spectrum")
        self.single_button.setEnabled(not spectrum)     # a Single capture is a scope thing
        self.setWindowTitle("Spectrum analyser" if spectrum else "Oscilloscope")
        if spectrum:
            # The analyser isn't kept up to date while it is hidden. Start it
            # again from the newest samples.
            self.analyzer.reset()
            self.analyzer.update(self.live_channels())
        self.update_measurements()

    def view_state(self):
        """What the spectrum's screen shows at its top right."""
        if not self.running:
            return "Stopped"
        return "Running" if self.live_channels() else "No signal"

    def toggle_running(self):
        self.set_running(not self.running)

    def arm_single(self):
        self.set_running(True)
        self.single = True

    def set_running(self, running):
        if not running and self.running:
            self.frozen = self.live_channels()      # remember which channels the frozen picture has
        self.single = False
        self.running = running
        self.run_button.setText("Stop" if running else "Run")
        if running:
            # Samples that arrived while stopped were thrown away (see
            # receive()), so what is held is stale. Start with a clean slate.
            for channel in self.channels:
                channel.clear()
            self.centre = None
            self.restart_search()
            self.analyzer.reset()
            self.last_trigger = time.monotonic()
        else:
            self.state = "Stopped"
        self.screens.currentWidget().update()

    def live_channels(self):
        """The channels that are connected: those that have sent something in
        the last half second. While stopped, nothing is being received, so it
        is the ones that were connected at the moment of stopping."""
        if not self.running:
            return self.frozen
        now = time.monotonic()
        return [c for c in self.channels if c.has_data and now - c.last_packet < SIGNAL_TIMEOUT_S]

    # -- the test signal ----------------------------------------------------
    def send_test_signal(self, *_ignored):
        """Tell the effect program what test signal to play (see THE COMMANDS
        in scope_tap.h). Sent whenever a control changes, and once a second
        besides: the effect program switches the signal off if it stops
        hearing this, so that closing this panel can't leave a tone playing."""
        word = TEST_SIGNALS[self.signal_box.currentIndex()][1]
        self.signal_freq.setEnabled(word == "sine")     # noise has no frequency to set
        self.send_command(f"gen {word} {self.signal_freq.value():.0f} {self.signal_level.value():.0f}")

    def send_command(self, command):
        if self.effect is None:
            return                      # nothing has been heard from the effect program yet
        try:
            self.sock.sendto(command.encode("ascii"), self.effect)
        except OSError:
            pass                        # nothing to do if the send fails; the next one retries

    def closeEvent(self, event):
        # Don't leave the test signal playing. The timers are stopped first, so
        # that the once-a-second repeat can't switch it back on behind us.
        for timer in (self.frame_timer, self.measure_timer, self.command_timer):
            timer.stop()
        self.send_command("gen off 0 0")
        super().closeEvent(event)

    # -- every frame --------------------------------------------------------
    def frame(self):
        self.receive()
        if self.running:
            self.acquire()
        if self.spectrum_view:
            self.analyzer.update(self.live_channels())
        self.screens.currentWidget().update()

    def receive(self):
        """Take every packet that has arrived and file its samples under its
        channel. Anything that isn't a scope packet is ignored."""
        now = time.monotonic()
        while True:
            try:
                size, sender = self.sock.recvfrom_into(self.packet)
            except (BlockingIOError, InterruptedError):
                return                  # nothing more waiting
            except OSError:
                return                  # e.g. a reset on Windows; try again next frame
            if size < HEADER.size:
                continue
            magic, number, count, rate, _dropped, index = HEADER.unpack_from(self.packet)
            if (magic != MAGIC or number >= len(self.channels) or rate == 0
                    or count == 0 or size != HEADER.size + 4 * count):
                continue

            # Commands go back to wherever the samples come from. If that has
            # changed, the effect program was restarted (and its test signal
            # with it), so tell it the settings straight away.
            if sender != self.effect:
                self.effect = sender
                self.send_test_signal()
            if not self.running:
                continue                # stopped: the picture is frozen, so new samples are thrown away

            if rate != self.sample_rate:
                self.sample_rate = rate
                self.restart_search()
                self.analyzer.reset()
            channel = self.channels[number]
            # "<f4" = little-endian 32-bit floats, read straight out of the packet.
            samples = np.frombuffer(self.packet, dtype="<f4", count=count, offset=HEADER.size)
            channel.write(index, samples)
            channel.last_packet = now

    def acquire(self):
        """Decide what the scope's screen shows: move the centre of the
        picture to the newest trigger, if there is one."""
        now = time.monotonic()
        live = self.live_channels()
        if not live:
            self.state = "No signal"    # the last picture stays up
            return

        # The newest sample that every connected channel has. The picture can't
        # go past this without one trace being shorter than the other.
        newest = min(channel.end for channel in live)
        half = self.window_samples() / 2

        # A trigger can only be used once the samples to its right, out to the
        # edge of the screen, have arrived.
        trigger = None
        source = self.channels[self.trig_source]
        if source in live:
            trigger = self.find_trigger(source, newest - math.ceil(half))

        # With no trigger for a while, Auto gives up waiting and shows the
        # newest samples anyway. "A while" grows with the timebase, so a slow
        # signal on a slow timebase still gets the chance to trigger.
        timed_out = now - self.last_trigger > max(0.3, 2 * half / self.sample_rate)

        if trigger is not None:
            self.centre = trigger
            self.last_trigger = now
            self.state = "Trig'd"
            if self.single:
                self.set_running(False)
        elif self.single:
            self.state = "Armed"
        elif not timed_out:
            pass                        # hold the picture, and what the state says
        elif self.trig_auto:
            self.centre = newest - half
            self.state = "Auto"
        else:
            self.state = "Waiting"

    def find_trigger(self, source, limit):
        """Search the samples that have arrived since the last search, up to
        index limit, for trigger crossings. Returns one (a fractional sample
        index), or None."""
        limit = min(limit, source.end)
        start = self.searched_to
        if start is None or not source.start < start <= source.end:
            # Starting again: look back over one screen (and at least 50 ms).
            span = int(max(self.window_samples(), 0.05 * self.sample_rate))
            start = max(limit - span, source.start + 1)
            self.searched_to = start
            self.search_state = 0
        if limit <= start:
            return None                 # nothing new to search yet

        # Read one sample earlier than the new ones: a crossing between the
        # last sample of the previous search and the first of this one needs
        # both. search_state is that earlier sample's state.
        first, x = source.read(start - 1, limit)
        level = self.trig_level
        if not self.trig_rising:
            x = -x                      # a falling edge is a rising edge, upside down
            level = -level
        hysteresis = TRIGGER_HYSTERESIS_DIVS * source.scale
        positions, self.search_state = rising_crossings(x, level, hysteresis, self.search_state)
        self.searched_to = limit

        if len(positions) == 0:
            return None
        # Normally the newest crossing is the one to show. A Single capture
        # wants the first: the start of whatever it was waiting for.
        return first + float(positions[0] if self.single else positions[-1])

    # -- measurements -------------------------------------------------------
    def measure(self, channel):
        """The scope's measurements of a channel: its levels over the part
        that is on the screen, and its frequency. Returns a dict of text,
        keyed like SCOPE_MEASUREMENTS, or None."""
        if self.centre is None:
            return None
        window = self.window_samples()
        left_edge = self.centre - window / 2
        _first, y = channel.read(math.ceil(left_edge), math.floor(left_edge + window) + 1)
        if len(y) < 2:
            return None

        y = y.astype(np.float64)
        high = float(y.max())
        low = float(y.min())
        rms = math.sqrt(float(np.mean(y * y)))
        result = {"max": f"{high:+.4f}", "min": f"{low:+.4f}", "pp": f"{high - low:.4f}",
                  "rms": f"{rms:.4f}", "rms_db": format_db(rms), "mean": f"{float(y.mean()):+.4f}",
                  "freq": "--"}

        # Frequency: time the rising crossings of the signal's midpoint. One
        # cycle on the screen isn't enough to time, so this looks back over at
        # least 100 ms, ending at the right-hand edge of the screen.
        right_edge = math.floor(left_edge + window) + 1
        span = math.ceil(max(window, 0.1 * self.sample_rate))
        _first, y = channel.read(right_edge - span, right_edge)
        y = y.astype(np.float64)
        high = float(y.max())
        low = float(y.min())
        if high - low > 1e-6:
            crossings, _state = rising_crossings(y, (high + low) / 2, 0.1 * (high - low))
            if len(crossings) >= 2:
                period = (crossings[-1] - crossings[0]) / (len(crossings) - 1)
                result["freq"] = format_frequency(self.sample_rate / period)
        return result

    def update_measurements(self):
        """Fill in the table that goes with the screen that is showing."""
        if self.spectrum_view:
            rows, labels = SPECTRUM_MEASUREMENTS, self.spectrum_labels
        else:
            rows, labels = SCOPE_MEASUREMENTS, self.scope_labels
        for number, channel in enumerate(self.channels):
            result = None
            if channel.enabled and self.spectrum_view:
                result = self.analyzer.measure(number, self.sample_rate)
            elif channel.enabled:
                result = self.measure(channel)
            for _title, key in rows:
                labels[number, key].setText(result[key] if result else "--")

        # The status line: where we are listening, and anything that has gone wrong.
        parts = [f"Listening on {self.address}"]
        for channel in self.channels:
            if channel.missing:
                parts.append(f"{channel.name}: {channel.missing} samples missing")
            if channel.bad:
                parts.append(f"{channel.name}: {channel.bad} NaN/inf samples")
        self.status.setText("     ".join(parts))


# ---------------------------------------------------------------------------
# Starting up
# ---------------------------------------------------------------------------
def use_dark_theme(app):
    """Dark window colours, so the controls match the screen and the trace
    colours read the same everywhere."""
    app.setStyle("Fusion")
    palette = QPalette()
    palette.setColor(QPalette.Window, QColor("#242423"))
    palette.setColor(QPalette.WindowText, QColor(TEXT_COLOR))
    palette.setColor(QPalette.Base, QColor("#131312"))       # the inside of tick boxes and number boxes
    palette.setColor(QPalette.AlternateBase, QColor("#242423"))
    palette.setColor(QPalette.Text, QColor(TEXT_COLOR))
    palette.setColor(QPalette.Button, QColor("#333331"))
    palette.setColor(QPalette.ButtonText, QColor(TEXT_COLOR))
    palette.setColor(QPalette.ToolTipBase, QColor("#333331"))
    palette.setColor(QPalette.ToolTipText, QColor(TEXT_COLOR))
    palette.setColor(QPalette.Highlight, QColor("#3987e5"))
    palette.setColor(QPalette.HighlightedText, QColor("#ffffff"))
    palette.setColor(QPalette.PlaceholderText, QColor(MUTED_COLOR))
    # Greyed-out controls (the ones that don't apply to the current view).
    for role in (QPalette.WindowText, QPalette.Text, QPalette.ButtonText):
        palette.setColor(QPalette.Disabled, role, QColor("#6b6a65"))
    app.setPalette(palette)


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--host", default="127.0.0.1",
                   help="the address to listen on (default: 127.0.0.1)")
    p.add_argument("--port", type=int, default=9000,
                   help="the UDP port the effect program sends to (default: 9000)")
    p.add_argument("--fps", type=float, default=40,
                   help="how many times a second the screen is redrawn (default: 40)")
    p.add_argument("--spectrum", action="store_true",
                   help="start in the spectrum view instead of the oscilloscope")
    args, qt_args = p.parse_known_args()

    app = QApplication([sys.argv[0]] + qt_args)
    use_dark_theme(app)
    try:
        scope = Oscilloscope(args.host, args.port, min(max(args.fps, 1), 120))
    except OSError as e:
        message = (f"Could not listen on udp://{args.host}:{args.port}.\n"
                   f"Is another oscilloscope already running?\n\n{e}")
        print(message, file=sys.stderr)
        QMessageBox.critical(None, "Oscilloscope", message)
        sys.exit(1)
    if args.spectrum:
        scope.set_view(True)
    scope.resize(1180, 700)
    scope.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
