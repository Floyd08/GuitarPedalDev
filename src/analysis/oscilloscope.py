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
                    dB. A sine wave that peaks at 1.0 reads 0 dB. With a sine
                    test signal it can also mark the tone's harmonics (solid
                    ticks) and where its harmonics above half the sample rate
                    fold back to (hollow triangles and dashed lines): anything
                    at a triangle is aliasing.
                    Response draws how much louder or quieter B is than A at
                    each frequency: the frequency response of whatever sits
                    between the two taps (a tone stack, a cabinet filter).
                    Feed it noise from the test signal, so there is something
                    to measure at every frequency. Where the curve turns grey
                    the measurement can't be trusted: A has nothing there, or B
                    isn't just a filtered copy of A (distortion does that, and
                    so does a long delay or reverb).
                    Spectrogram draws one channel's spectrum over time, with
                    the level as brightness. Feed it the Sweep test signal: the
                    sweep and its harmonics are lines climbing to the right,
                    and aliases are lines that hit the top and come back DOWN.
    FFT size        how many samples each analysis covers. More gives finer
                    frequency detail, but reacts more slowly.
    Window          how each block of samples is faded in and out before it is
                    analysed (the tool tip says when to use which).
    Averaging       steadies a noisy picture.
    Peak hold       also draws, faintly, the highest level seen so far.
    Measurements    the test tone in each channel: its level, its distortion
                    (THD), its aliases and everything else (noise, other
                    unwanted tones), each relative to the tone. Use the Sine
                    test signal, at a frequency the sample rate isn't a whole
                    number of times (1234 Hz, not 1000 Hz), or the aliases
                    land on the harmonics and can't be told apart.
    Mouse           moving over the screen reads off the frequency and each
                    curve's level under the pointer.

Either program can be started first.

Requires: PySide6, numpy
Run:      python oscilloscope.py [--host 127.0.0.1] [--port 9000] [--fps 40] [--spectrum]
"""
import argparse
import functools
import math
from collections import deque
import socket
import struct
import sys
import time

import numpy as np
from PySide6.QtCore import QLineF, QPointF, QRectF, Qt, QTimer
from PySide6.QtGui import (QColor, QImage, QKeySequence, QLinearGradient,
                           QPainter, QPalette, QPen, QPolygonF, QShortcut,
                           qRgb)
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

# The harmonics the analyser looks for, from the 2nd up to this one. Those
# below half the sample rate are real harmonics. Those above it can't exist in
# the output, so wherever they would fold back to is where aliasing shows up.
MAX_HARMONIC = 50

# The measurements (level, THD, aliases, other) don't use the window chosen for
# the display. They use their own, a Kaiser window, which keeps a tone's
# leakage below about -150 dB everywhere outside its lobe. Every ordinary window
# leaks more than that somewhere (Hann close to the tone, Blackman-Harris far
# from it), and the leakage would be counted as distortion. MEASURE_SPREAD is
# how many points either side of a tone's peak its lobe covers.
MEASURE_KAISER_BETA = 20
MEASURE_SPREAD = 8

# The spectrogram: one column (one FFT) every 1/SPECTROGRAM_RATE seconds, and
# SPECTROGRAM_COLUMNS of them across the screen (25.6 seconds). Each column is
# boiled down to SPECTROGRAM_ROWS points from the bottom of the frequency axis
# to the top.
SPECTROGRAM_RATE = 40
SPECTROGRAM_COLUMNS = 1024
SPECTROGRAM_ROWS = 400
MAX_COLUMNS_PER_FRAME = 300

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
        self.measure_window = None      # the Kaiser window for the measurements, made when first needed
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
        # the channels' power spectra, the cross-spectrum of A and B, a list of
        # the channels' power spectra through the measurement window). The
        # averages below are the mean of these.
        self.blocks = deque(maxlen=max(self.averaging, self.min_averaging))
        self.power = [None] * len(self.channels)    # each channel's averaged power spectrum
        self.measured = [None] * len(self.channels) # the same, through the measurement window (see MEASURE_KAISER_BETA)
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
        if self.measure_window is None or len(self.measure_window) != n:
            self.measure_window = np.kaiser(n, MEASURE_KAISER_BETA)
        measure_scale = 2 / self.measure_window.sum()

        added = False
        while self.position + n <= end:
            powers = [None] * len(self.channels)
            measured = [None] * len(self.channels)
            spectra = [None] * len(self.channels)
            for channel in live:
                number = self.channels.index(channel)
                _first, x = channel.read(self.position, self.position + n)
                kaiser = np.fft.rfft(x * self.measure_window) * measure_scale
                measured[number] = (kaiser.real ** 2 + kaiser.imag ** 2).astype(np.float32)
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

            self.blocks.append((powers, cross, measured))     # the oldest block drops off the other end
            self.position += hop
            added = True

        if added:
            # A plain average of the blocks that are held. (Not a running
            # "leaky" average: with this, when a tone stops it is gone from the
            # picture after that many blocks, instead of fading for seconds.)
            for number in range(len(self.channels)):
                held = [block[0][number] for block in self.blocks if block[0][number] is not None]
                self.power[number] = np.mean(held, axis=0, dtype=np.float64) if held else None
                held = [block[2][number] for block in self.blocks if block[2][number] is not None]
                self.measured[number] = np.mean(held, axis=0, dtype=np.float64) if held else None
            held = [block[1] for block in self.blocks if block[1] is not None]
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

    # -- finding and measuring a tone ---------------------------------------
    # A windowed tone doesn't land on one point of the spectrum. It is spread
    # over a few points either side (how many depends on the window). So a
    # tone is measured over that whole spread, its "lobe": its power is the
    # sum of the points, and its frequency is their centre of gravity. That
    # reads true wherever the tone falls between two points.
    def spread(self):
        """How many points either side of a tone's peak its lobe covers, in the
        spectra the measurements use."""
        return MEASURE_SPREAD

    def lobe(self, power, place):
        """The first and last point of the lobe around a place (a fractional
        point number). Never includes point 0 (DC) or the last point."""
        spread = self.spread()
        centre = int(round(place))
        return max(centre - spread, 1), min(centre + spread, len(power) - 2)

    def stands_clear(self, power, k):
        """Whether the point k is the top of a tone. A tone stands well clear of
        the spectrum either side of it. Noise doesn't: its highest point is only
        a little above its neighbours, and "measuring" that would give numbers
        that mean nothing. So compare the top with the typical level just
        outside the tone's lobe: it must be at least 15 dB clear."""
        spread = self.spread()
        beside = np.concatenate((power[max(k - spread - 10, 1):max(k - spread - 1, 1)],
                                 power[k + spread + 2:k + spread + 11]))
        return len(beside) > 0 and power[k] >= 1e-16 and power[k] >= 30 * float(np.median(beside))

    def centre_of(self, power, k):
        """The centre of gravity of the lobe around point k, as a fractional point number."""
        first, last = self.lobe(power, k)
        points = power[first:last + 1].astype(np.float64)
        return float((np.arange(first, last + 1) * points).sum()) / max(float(points.sum()), 1e-300)

    def find_tone(self, number, sample_rate):
        """The strongest tone in one channel, as a fractional point number, or
        None if the channel has no clear tone."""
        power = self.measured[number]
        if power is None:
            return None
        lowest = max(1, math.ceil(20 * self.size / sample_rate))     # ignore anything below 20 Hz (and DC)
        k = lowest + int(np.argmax(power[lowest:len(power) - 1]))
        if not self.stands_clear(power, k):
            return None
        return self.centre_of(power, k)

    def reference_tone(self, sample_rate):
        """The test tone that harmonics and aliases are worked out from. It is
        channel A's tone if it has one (A is the clean input), and otherwise
        B's. Returns a fractional point number, or None."""
        for number in range(len(self.channels)):
            centre = self.find_tone(number, sample_rate)
            if centre is not None:
                return centre
        return None

    def harmonic_positions(self, centre):
        """Where each harmonic of a tone (the 2nd to the MAX_HARMONIC-th) shows
        up in the spectrum. Returns a list of dicts:
            n       which harmonic
            true    where it really is (a fractional point number). This can
                    be far above the top of the spectrum
            place   where it shows up. For a harmonic below half the sample
                    rate that is where it really is. For one above, it is
                    where it folds back to (aliases), as if reflected back and
                    forth between 0 and half the sample rate
            folded  True if it is above half the sample rate, so anything at
                    its place is aliasing
            hidden  True if it folds onto the tone or a real harmonic, where
                    it can't be told apart from them. This happens when the
                    sample rate is a whole number of times the tone's frequency.
                    (Also if it folds onto 0 Hz or half the sample rate.)
        Everything is in points of the spectrum (multiply by
        sample_rate / size for Hz)."""
        size = self.size
        half = size / 2
        positions = []
        for n in range(2, MAX_HARMONIC + 1):
            true = n * centre
            place = true
            if true >= half:
                place = true % size
                if place > half:
                    place = size - place
            positions.append({"n": n, "true": true, "place": place, "folded": true >= half})

        # The places an alias can't be seen at: on the tone, on a real harmonic,
        # or on 0 Hz or half the sample rate (the two ends, which the
        # measurements leave out).
        unseen = np.array([0.0, half, centre] + [p["place"] for p in positions if not p["folded"]])
        spread = self.spread()
        for p in positions:
            p["hidden"] = p["folded"] and float(np.abs(unseen - p["place"]).min()) < spread
        return positions

    def measure(self, number, sample_rate, reference):
        """Measure the test tone in one channel. reference is the tone's place
        (from reference_tone()). Returns a dict of text keyed like
        SPECTRUM_MEASUREMENTS, or None.

        Every point of the spectrum is given to one of four groups, and each
        group's power is compared with the tone's:
            the tone        the lobe around the tone
            harmonics       the lobes around its real harmonics       -> THD
            aliases         the lobes where its harmonics above half
                            the sample rate fold back to              -> Aliases
            everything else noise, hum, and any other unwanted tones
                            (for example from images left by poor
                            oversampling)                             -> Other
        """
        power = self.measured[number]
        if power is None or reference is None:
            return None
        bins = len(power)

        # Find the tone's peak in this channel, near the reference.
        first, last = self.lobe(power, reference)
        k = first + int(np.argmax(power[first:last + 1]))
        if not self.stands_clear(power, k):
            return None
        centre = self.centre_of(power, k)
        positions = self.harmonic_positions(centre)

        def lobes(places):
            mask = np.zeros(bins, dtype=bool)
            for place in places:
                first, last = self.lobe(power, place)
                mask[first:last + 1] = True
            return mask

        tone_mask = lobes([centre])
        harmonic_mask = lobes([p["place"] for p in positions if not p["folded"]]) & ~tone_mask
        alias_mask = lobes([p["place"] for p in positions if p["folded"] and not p["hidden"]]) & ~(tone_mask | harmonic_mask)
        other_mask = ~(tone_mask | harmonic_mask | alias_mask)
        # Not DC (an offset), nor its lobe, nor anything below 20 Hz.
        other_mask[:max(self.spread() + 1, math.ceil(20 * self.size / sample_rate))] = False
        other_mask[bins - 1] = False

        power = power.astype(np.float64)
        tone = float(power[tone_mask].sum())

        # The sum counts the tone's power several times over (once per point
        # it is spread across). The window's "noise bandwidth" is how many.
        window = self.measure_window
        bandwidth = self.size * float((window ** 2).sum()) / float(window.sum()) ** 2

        def dbc(part):
            """A power as dB relative to the tone ("dBc", dB relative to the carrier)."""
            db = 10 * math.log10(max(part, 1e-300) / tone)
            return f"{db:.1f} dBc" if db > -140 else "< -140 dBc"

        result = {"freq": format_frequency(centre * sample_rate / self.size),
                  "level": f"{10 * math.log10(tone / bandwidth):+.1f} dB",
                  "thd": "--", "alias": "--", "other": dbc(float(power[other_mask].sum()))}

        # THD: only means something when the input is a single sine.
        if any(not p["folded"] for p in positions):
            percent = 100 * math.sqrt(float(power[harmonic_mask].sum()) / tone)
            result["thd"] = f"{percent:.3f} %" if percent >= 0.001 else "< 0.001 %"

        folded = [p for p in positions if p["folded"]]
        if folded and all(p["hidden"] for p in folded):
            result["alias"] = "hidden"          # every alias lands on a real harmonic (see harmonic_positions)
        elif folded:
            result["alias"] = dbc(float(power[alias_mask].sum()))
        return result


class Spectrogram:
    """The spectrogram's picture: the recent spectra of one channel, side by
    side, oldest on the left. Each column is one FFT, taken every
    1/SPECTROGRAM_RATE seconds, and each holds the level (in dB) at
    SPECTROGRAM_ROWS frequencies, top to bottom.

    It uses the analyser's FFT size and window, but not its averaging: each
    column is one FFT on its own, so that changes over time stay sharp."""

    def __init__(self, channels):
        self.channels = channels
        self.number = 1                 # which channel it shows. B, the output, by default
        self.key = None                 # the settings the picture was made with
        self.image = None               # the QImage last drawn, kept until something changes
        self.reset()

    def reset(self):
        """Clear the picture."""
        self.db = np.full((SPECTROGRAM_ROWS, SPECTROGRAM_COLUMNS), -400.0, dtype=np.float32)
        self.next = 0                   # the column that will be written next (the ring wraps round)
        self.position = None            # the sample index where the next column's FFT starts
        self.changed = True             # the picture needs redrawing

    def ordered(self):
        """The columns in time order, oldest first."""
        return np.concatenate((self.db[:, self.next:], self.db[:, :self.next]), axis=1)

    def setup(self, size, window, log_axis, sample_rate):
        """Work out which points of each FFT go into which row of the picture."""
        self.window = window
        self.scale = (2 / window.sum()) ** 2        # so a sine that peaks at 1.0 reads 0 dB, as in the spectrum
        self.hop = max(1, round(sample_rate / SPECTROGRAM_RATE))
        low, high = (20.0 if log_axis else 0.0), sample_rate / 2
        fractions = np.arange(SPECTROGRAM_ROWS + 1) / SPECTROGRAM_ROWS     # bottom edge of each row, then the top of the last
        if log_axis:
            edges = low * (high / low) ** fractions
        else:
            edges = low + fractions * (high - low)
        points = edges * size / sample_rate
        bins = size // 2 + 1
        # Row r takes the highest of the points from starts[r] up to (not
        # including) starts[r + 1]. A row narrower than one point (at the
        # bottom of a log axis) takes the point it sits on.
        self.starts = np.clip(np.round(points[:-1]).astype(np.int64), 0, bins - 1)
        self.end = min(bins, int(round(points[-1])) + 1)

    def update(self, panel):
        """Add a column for every whole FFT that has arrived since the last call."""
        analyzer = panel.analyzer
        key = (self.number, analyzer.size, analyzer.window_name, panel.log_axis, panel.sample_rate)
        if key != self.key:
            self.key = key
            self.reset()
            self.setup(analyzer.size, make_window(analyzer.window_name, analyzer.size),
                       panel.log_axis, panel.sample_rate)

        channel = self.channels[self.number]
        if channel not in panel.live_channels():
            return
        n = analyzer.size
        if self.position is None or not channel.start <= self.position <= channel.end:
            # Starting again: begin far enough back to fill the screen with
            # whatever samples are held.
            self.position = max(channel.start, channel.end - n - (SPECTROGRAM_COLUMNS - 1) * self.hop)
        columns = (channel.end - n - self.position) // self.hop + 1
        if columns <= 0:
            return
        columns = min(columns, MAX_COLUMNS_PER_FRAME)       # any more wait for the next frame

        # All the new FFTs at once: a 2D array with one block of samples per row.
        first, x = channel.read(self.position, self.position + (columns - 1) * self.hop + n)
        blocks = np.lib.stride_tricks.sliding_window_view(x, n)[::self.hop][:columns]
        spectra = np.fft.rfft(blocks * self.window, axis=1)
        power = (spectra.real ** 2 + spectra.imag ** 2) * self.scale
        rows = np.maximum.reduceat(power[:, :self.end], self.starts, axis=1)
        db = 10 * np.log10(np.maximum(rows, 1e-30)).astype(np.float32)

        # Into the ring, one column per FFT, with the highest frequency at the top.
        places = (self.next + np.arange(columns)) % SPECTROGRAM_COLUMNS
        self.db[:, places] = db[:, ::-1].T
        self.next = (self.next + columns) % SPECTROGRAM_COLUMNS
        self.position += columns * self.hop
        self.changed = True

    def picture(self, top, bottom, color):
        """The picture as a QImage, one pixel per row and column. Levels from
        bottom (dB) up to top are shaded from the screen's colour, through the
        channel's colour, to nearly white."""
        key = (top, bottom, QColor(color).name())
        if self.image is not None and not self.changed and key == self.image_key:
            return self.image
        shade = np.clip((self.ordered() - bottom) * (255 / (top - bottom)), 0, 255)
        # Kept on the object: the QImage reads straight from this memory.
        self.pixels = np.ascontiguousarray(shade.astype(np.uint8))
        image = QImage(self.pixels.data, SPECTROGRAM_COLUMNS, SPECTROGRAM_ROWS,
                       SPECTROGRAM_COLUMNS, QImage.Format_Indexed8)
        image.setColorTable(color_ramp(QColor(color).name()))
        # Converted to full colour once here, because Qt draws (and stretches)
        # a full-colour picture much faster than one that uses a colour table.
        self.image = image.convertToFormat(QImage.Format_RGB32)
        self.image_key = key
        self.changed = False
        return image

    def level_at(self, column, row):
        """The level (dB) at a column (0 = oldest) and row (0 = top), or None."""
        if 0 <= column < SPECTROGRAM_COLUMNS and 0 <= row < SPECTROGRAM_ROWS:
            value = float(self.ordered()[row, column])
            return value if value > -300 else None
        return None


@functools.lru_cache(maxsize=8)
def color_ramp(color):
    """The spectrogram's 256 shades: the screen's colour (for silence),
    darkening into the channel's colour, then lightening to nearly white (for
    the loudest). One hue, so brighter always means louder."""
    color = QColor(color)
    stops = [(0.0, QColor(SCREEN_COLOR)), (0.4, QColor(color).darker(230)),
             (0.75, QColor(color)), (1.0, QColor(color).lighter(175))]
    table = []
    for i in range(256):
        where = i / 255
        for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
            if where <= p1:
                t = (where - p0) / (p1 - p0)
                table.append(qRgb(round(c0.red() + (c1.red() - c0.red()) * t),
                                  round(c0.green() + (c1.green() - c0.green()) * t),
                                  round(c0.blue() + (c1.blue() - c0.blue()) * t)))
                break
    return table


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

    def frequency_to_fraction(self, frequency):
        """How far along the frequency axis a frequency sits, from 0 (the low
        end) to 1 (the high end). Works on one frequency or an array of them."""
        low, high = self.frequency_range()
        if self.panel.log_axis:
            return np.log(np.maximum(frequency, 1e-3) / low) / math.log(high / low)
        return (frequency - low) / (high - low)

    def fraction_to_frequency(self, fraction):
        low, high = self.frequency_range()
        if self.panel.log_axis:
            return low * (high / low) ** fraction
        return low + fraction * (high - low)

    def frequency_to_x(self, grid, frequency):
        """In the spectrum, frequency runs across."""
        return grid.left() + self.frequency_to_fraction(frequency) * grid.width()

    def x_to_frequency(self, grid, x):
        return self.fraction_to_frequency((x - grid.left()) / grid.width())

    def frequency_to_y(self, grid, frequency):
        """In the spectrogram, frequency runs up."""
        return grid.bottom() - self.frequency_to_fraction(frequency) * grid.height()

    def frequency_lines(self):
        """The frequencies to draw grid lines at, each with whether it gets a
        label. On a log axis: 20, 30 ... 90, 100, 200 ... with a label on the
        1, 2 and 5 of each decade. On a linear axis: evenly spaced, all labelled."""
        low, high = self.frequency_range()
        if self.panel.log_axis:
            lines = [(digit * decade, digit in (1, 2, 5))
                     for decade in (10, 100, 1000, 10000) for digit in range(1, 10)]
        else:
            step = 1000 * max(1, round(high / 12000))
            lines = [(f, True) for f in range(step, int(high) + 1, step)]
        return [(f, labelled) for f, labelled in lines if low <= f <= high]

    @staticmethod
    def frequency_label(frequency):
        return f"{frequency / 1000:g}k" if frequency >= 1000 else f"{frequency:g}"

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

        if panel.spectrogram_mode:
            self.paint_spectrogram(painter, grid)
            return

        self.draw_grid(painter, grid)
        curves = self.curves(grid)

        painter.setRenderHint(QPainter.Antialiasing, True)
        painter.setClipRect(grid)
        for curve in curves:
            self.draw_curve(painter, grid, curve)
        painter.setClipping(False)

        markers = []
        if panel.show_markers and not panel.response_mode and curves:
            markers = self.markers(grid)
            self.draw_markers(painter, grid, markers)

        if not curves:
            painter.setPen(QColor(MUTED_COLOR))
            if not any(channel.has_data for channel in panel.channels):
                painter.drawText(grid, Qt.AlignCenter, panel.waiting_text)
            elif panel.response_mode and len(panel.analyzer.used) < 2:
                painter.drawText(grid, Qt.AlignCenter, "The response needs both channels connected")
            elif any(channel.enabled for channel in panel.channels):
                painter.drawText(grid, Qt.AlignCenter, "Not enough samples yet for this FFT size")

        self.draw_readouts(painter, grid, curves, markers)

    def draw_grid(self, painter, grid):
        painter.setRenderHint(QPainter.Antialiasing, False)
        metrics = painter.fontMetrics()

        for frequency, labelled in self.frequency_lines():
            x = round(float(self.frequency_to_x(grid, frequency)))
            painter.setPen(QPen(QColor(GRID_COLOR), 1))
            painter.drawLine(x, round(grid.top()), x, round(grid.bottom()))
            if labelled:
                label = self.frequency_label(frequency)
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

    # -- harmonic and alias markers -----------------------------------------
    def markers(self, grid):
        """Where the test tone's harmonics, and its aliases, are on the screen.
        A list of dicts: n (which harmonic, 1 = the tone itself), x, folded
        (True for an alias) and true (where the harmonic really is, in Hz).
        Aliases that land on a real harmonic are left out (see
        SpectrumAnalyzer.harmonic_positions)."""
        panel = self.panel
        analyzer = panel.analyzer
        centre = analyzer.reference_tone(panel.sample_rate)
        if centre is None:
            return []
        hz = panel.sample_rate / analyzer.size          # Hz per point
        found = [{"n": 1, "place": centre, "folded": False, "true": centre}]
        found += [p for p in analyzer.harmonic_positions(centre) if not p["hidden"]]
        markers = []
        for p in found:
            x = float(self.frequency_to_x(grid, p["place"] * hz))
            if grid.left() <= x <= grid.right():
                markers.append({"n": p["n"], "x": x, "folded": p["folded"], "true": p["true"] * hz})
        return markers

    def draw_markers(self, painter, grid, markers):
        """Real harmonics get a short solid tick down from the top of the grid.
        Aliases get a hollow triangle and a faint dashed line all the way down,
        so they are easy to pick out and to follow down to the trace. The shape,
        not a colour, is what tells the two apart. Numbers say which harmonic
        each is, wherever there is room for them."""
        metrics = painter.fontMetrics()
        labels = []                     # (left, right) of every number drawn so far, so they don't overlap

        def label(x, n, color):
            text = str(n)
            width = metrics.horizontalAdvance(text)
            left = x - width / 2
            if any(left < r + 3 and left + width > l - 3 for l, r in labels):
                return                  # no room
            labels.append((left, left + width))
            painter.setPen(QColor(color))
            painter.drawText(QPointF(left, grid.top() + 24), text)

        # The dashed lines are drawn all at once, without antialiasing: Qt is
        # slow at antialiased dashes, and these are straight up and down anyway.
        dashed = QPen(QColor(137, 135, 129, 120), 1, Qt.DashLine)
        dashed.setCosmetic(True)
        painter.setRenderHint(QPainter.Antialiasing, False)
        painter.setPen(dashed)
        painter.drawLines([QLineF(round(m["x"]) + 0.5, grid.top() + 10, round(m["x"]) + 0.5, grid.bottom())
                           for m in markers if m["folded"]])
        painter.setRenderHint(QPainter.Antialiasing, True)
        for m in markers:
            if m["folded"]:
                painter.setPen(QPen(QColor(TEXT_COLOR), 1.2))
                painter.setBrush(Qt.NoBrush)
                painter.drawPolygon(QPolygonF([QPointF(m["x"] - 4.5, grid.top() + 2), QPointF(m["x"] + 4.5, grid.top() + 2),
                                               QPointF(m["x"], grid.top() + 10)]))
            else:
                painter.setPen(QPen(QColor(LABEL_COLOR), 2))
                painter.drawLine(QPointF(m["x"], grid.top()), QPointF(m["x"], grid.top() + 10))
        # Numbers: the real harmonics first, so they win any tussle for room.
        for m in sorted(markers, key=lambda m: (m["folded"], m["n"])):
            label(m["x"], m["n"], MUTED_COLOR if m["folded"] else LABEL_COLOR)

    # -- the spectrogram ----------------------------------------------------
    def paint_spectrogram(self, painter, grid):
        """Frequency (up) against time (across, newest on the right), with the
        level shaded in the channel's colour: dark is quiet, bright is loud.
        A sweep is a line climbing to the right. Its harmonics climb faster
        above it. An alias climbs to the top, then comes back DOWN."""
        panel = self.panel
        spectrogram = panel.spectrogram
        channel = panel.channels[spectrogram.number]
        top, bottom = panel.spectrum_top, panel.spectrum_top - panel.spectrum_range
        metrics = painter.fontMetrics()
        text, swatch = strip_writer(painter)
        gap = metrics.horizontalAdvance("    ")

        painter.setRenderHint(QPainter.SmoothPixmapTransform, True)
        painter.drawImage(grid, spectrogram.picture(top, bottom, channel.color))

        # Frequency labels on the left, with a short tick into the picture
        # (full lines would fight with the picture).
        painter.setRenderHint(QPainter.Antialiasing, False)
        for frequency, labelled in self.frequency_lines():
            y = round(float(self.frequency_to_y(grid, frequency)))
            painter.setPen(QPen(QColor(AXIS_COLOR), 1))
            painter.drawLine(round(grid.left()), y, round(grid.left()) + (8 if labelled else 4), y)
            if labelled:
                label = self.frequency_label(frequency)
                painter.setPen(QColor(MUTED_COLOR))
                painter.drawText(QPointF(grid.left() - 6 - metrics.horizontalAdvance(label), y + 4), label)
        painter.drawText(QPointF(4, grid.top() - 10), "Hz")

        # Time labels along the bottom: seconds before now.
        span = SPECTROGRAM_COLUMNS / SPECTROGRAM_RATE
        for seconds in range(0, int(span) + 1, 5):
            x = round(grid.right() - seconds / span * grid.width())
            painter.setPen(QPen(QColor(AXIS_COLOR), 1))
            painter.drawLine(x, round(grid.bottom()) - 6, x, round(grid.bottom()))
            label = f"-{seconds} s" if seconds else "now"
            painter.setPen(QColor(MUTED_COLOR))
            painter.drawText(QPointF(x - metrics.horizontalAdvance(label) / 2, grid.bottom() + 15), label)
        painter.setPen(QPen(QColor(AXIS_COLOR), 1))
        painter.setBrush(Qt.NoBrush)
        painter.drawRect(grid)

        if not channel.has_data:
            painter.setPen(QColor(MUTED_COLOR))
            painter.drawText(grid, Qt.AlignCenter, panel.waiting_text)

        # Top strip: which channel, the settings, then the colour scale and the state.
        baseline = grid.top() - 10
        x = swatch(grid.left(), baseline, channel.color)
        x = text(x, baseline, f"Channel {channel.name}", TEXT_COLOR) + gap
        text(x, baseline, f"{panel.analyzer.size} points    {panel.analyzer.window_name}    "
                          f"one FFT every {1000 / SPECTROGRAM_RATE:g} ms", MUTED_COLOR)
        state = panel.view_state()
        x = grid.right() - metrics.horizontalAdvance(state)
        text(x, baseline, state, TEXT_COLOR)
        self.draw_color_scale(painter, x - gap, baseline, top, bottom, channel.color)

        # Bottom strip, right: the sample rate.
        baseline = grid.bottom() + 36
        rate = f"{panel.sample_rate / 1000:g} kHz"
        text(grid.right() - metrics.horizontalAdvance(rate), baseline, rate, MUTED_COLOR)

        # Under the mouse: crosshairs, and the frequency, time and level there.
        if self.pointer is None:
            return
        px, py = self.pointer.x(), self.pointer.y()
        painter.setPen(QPen(QColor(LABEL_COLOR), 1, Qt.DotLine))
        painter.drawLine(QPointF(px, grid.top()), QPointF(px, grid.bottom()))
        painter.drawLine(QPointF(grid.left(), py), QPointF(grid.right(), py))
        column = int((px - grid.left()) / grid.width() * SPECTROGRAM_COLUMNS)
        row = int((py - grid.top()) / grid.height() * SPECTROGRAM_ROWS)
        frequency = float(self.fraction_to_frequency((grid.bottom() - py) / grid.height()))
        x = text(grid.left(), baseline, "f  ", LABEL_COLOR)
        x = text(x, baseline, format_frequency(frequency), TEXT_COLOR) + gap
        x = text(x, baseline, "t  ", LABEL_COLOR)
        x = text(x, baseline, f"-{(SPECTROGRAM_COLUMNS - 1 - column) / SPECTROGRAM_RATE:.2f} s", TEXT_COLOR) + gap
        level = spectrogram.level_at(column, row)
        if level is not None:
            x = swatch(x, baseline, channel.color)
            text(x, baseline, f"{level:+.1f} dB", TEXT_COLOR)

    def draw_color_scale(self, painter, right, baseline, top, bottom, color):
        """The key to the spectrogram's shading, ending at x = right: the
        bottom level, a bar shaded like the picture, and the top level."""
        text, _swatch = strip_writer(painter)
        metrics = painter.fontMetrics()
        high = f"{top} dB"
        low = f"{bottom} dB"
        x = right - metrics.horizontalAdvance(high)
        text(x, baseline, high, LABEL_COLOR)
        bar = QRectF(x - 6 - 110, baseline - 9, 110, 10)
        gradient = QLinearGradient(bar.topLeft(), bar.topRight())
        table = color_ramp(QColor(color).name())
        for i in range(0, 256, 15):
            gradient.setColorAt(i / 255, QColor.fromRgb(table[i]))
        gradient.setColorAt(1.0, QColor.fromRgb(table[255]))
        painter.setPen(QPen(QColor(AXIS_COLOR), 1))
        painter.setBrush(gradient)
        painter.drawRect(bar)
        text(bar.left() - 6 - metrics.horizontalAdvance(low), baseline, low, LABEL_COLOR)

    def draw_readouts(self, painter, grid, curves, markers):
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

        # Bottom strip, right: the sample rate, and a key to the markers.
        baseline = grid.bottom() + 36
        rate = f"{panel.sample_rate / 1000:g} kHz"
        x = grid.right() - metrics.horizontalAdvance(rate)
        text(x, baseline, rate, MUTED_COLOR)
        if markers:
            x -= gap + metrics.horizontalAdvance("alias")
            text(x, baseline, "alias", LABEL_COLOR)
            painter.setPen(QPen(QColor(TEXT_COLOR), 1.2))
            painter.setBrush(Qt.NoBrush)
            painter.drawPolygon(QPolygonF([QPointF(x - 15, baseline - 9), QPointF(x - 6, baseline - 9),
                                           QPointF(x - 10.5, baseline - 1)]))
            x -= 15 + gap + metrics.horizontalAdvance("harmonic")
            text(x, baseline, "harmonic", LABEL_COLOR)
            painter.setPen(QPen(QColor(LABEL_COLOR), 2))
            painter.drawLine(QPointF(x - 8, baseline - 10), QPointF(x - 8, baseline))

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

        # Near a marker, say what it is.
        near = [m for m in markers if abs(m["x"] - px) <= 6]
        if near:
            m = min(near, key=lambda m: abs(m["x"] - px))
            if m["n"] == 1:
                name = "the test tone"
            elif m["folded"]:
                name = f"alias of harmonic {m['n']} (really at {format_frequency(m['true'])})"
            else:
                name = f"harmonic {m['n']}"
            text(x, baseline, name, TEXT_COLOR)


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
SPECTRUM_MEASUREMENTS = [("Peak", "freq"), ("Level", "level"), ("THD", "thd"),
                         ("Aliases", "alias"), ("Other", "other")]

# The test signals: what the control shows, and the word sent to the effect
# program (see THE COMMANDS in scope_tap.h).
TEST_SIGNALS = [("Off", "off"), ("Sine", "sine"), ("Sweep", "sweep"),
                ("White noise", "white"), ("Pink noise", "pink")]
SWEEP_TIMES = [5, 10, 20]               # seconds per sweep (from 20 Hz to 20 kHz)


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
        self.response_mode = False      # True: draw B measured against A
        self.spectrogram_mode = False   # True: draw the spectrogram. (Both False: each channel's spectrum)
        self.show_markers = True        # mark the test tone's harmonics and aliases on the spectrum
        self.spectrogram = Spectrogram(self.channels)
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
            SPECTRUM_MEASUREMENTS,
            "The test tone (channel A's strongest tone) in each channel. These mean something\n"
            "only when the input is a single sine: use the Sine test signal.\n\n"
            "THD: the real harmonics (up to the 50th, below half the sample rate), compared with the tone.\n"
            "Aliases: the harmonics above half the sample rate, which can't exist in the output,\n"
            "    measured where they fold back to. In dB relative to the tone (dBc). Lower is better.\n"
            "    'hidden' means they all land on real harmonics: the sample rate is a whole number\n"
            "    of times the tone's frequency. Pick a tone like 1234 Hz instead.\n"
            "Other: everything else (noise, hum, other unwanted tones), in dBc.\n\n"
            "These use their own low-leakage window, whatever the Window setting.")
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
        mode = choice_box(["Spectrum", "Response (B / A)", "Spectrogram"])
        mode.setToolTip("Spectrum: how strong each channel is at each frequency.\n"
                        "Response: how much louder or quieter B is than A at each frequency,\n"
                        "which is the frequency response of whatever sits between the two taps.\n"
                        "It needs a broadband input: use the white or pink noise test signal.\n"
                        "Spectrogram: one channel's spectrum over time. With the Sweep test signal,\n"
                        "aliases show up as lines that go DOWN while the sweep goes up.")
        mode.currentIndexChanged.connect(self.set_display_mode)
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
        self.spectrogram_box = choice_box([f"Channel {name}" for name in CHANNEL_NAMES], self.spectrogram.number)
        self.spectrogram_box.currentIndexChanged.connect(self.on_spectrogram_channel)
        self.spectrogram_box.setEnabled(False)
        self.markers_box = QCheckBox("Mark harmonics and aliases")
        self.markers_box.setToolTip("With a sine test signal, mark where its harmonics are, and where the\n"
                                    "harmonics above half the sample rate fold back to (aliases).\n"
                                    "Point at a marker to see which harmonic it is.")
        self.markers_box.setChecked(self.show_markers)
        self.markers_box.toggled.connect(lambda on: self.on_display(show_markers=on))

        group = QGroupBox("Display")
        form = QFormLayout(group)
        form.addRow("Show", mode)
        form.addRow("Frequency axis", axis)
        form.addRow("Top", self.top_box)
        form.addRow("Range", self.range_box)
        form.addRow("Response range", self.span_box)
        form.addRow("Spectrogram of", self.spectrogram_box)
        form.addRow(self.markers_box)
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
        # Not 1000 Hz: 48000 is a whole number of times 1000, so all of a
        # 1 kHz tone's aliases land exactly on its harmonics and can't be seen.
        self.signal_freq.setValue(1234)
        self.signal_freq.valueChanged.connect(self.send_test_signal)
        self.sweep_time = QComboBox()
        for seconds in SWEEP_TIMES:
            self.sweep_time.addItem(f"{seconds} s sweep", seconds)
        self.sweep_time.setCurrentIndex(1)
        self.sweep_time.currentIndexChanged.connect(self.send_test_signal)
        # The frequency and the sweep time share one place: whichever applies is shown.
        self.signal_setting = QStackedWidget()
        self.signal_setting.addWidget(self.signal_freq)
        self.signal_setting.addWidget(self.sweep_time)
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
        grid.addWidget(self.signal_setting, 1, 0)
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

    def set_display_mode(self, index):
        """Choose what the spectrum's screen draws: 0 = each channel's
        spectrum, 1 = the response (B / A), 2 = the spectrogram."""
        self.response_mode = index == 1
        self.spectrogram_mode = index == 2
        # One block's response is meaningless (see SpectrumAnalyzer.response),
        # so the response view always averages at least a few.
        self.analyzer.configure(min_averaging=RESPONSE_MIN_AVERAGING if self.response_mode else 1)
        # Grey out the controls that don't apply. (Top and Range set the
        # spectrogram's shading.)
        self.top_box.setEnabled(not self.response_mode)
        self.range_box.setEnabled(not self.response_mode)
        self.span_box.setEnabled(self.response_mode)
        self.spectrogram_box.setEnabled(self.spectrogram_mode)
        self.markers_box.setEnabled(index == 0)
        self.refresh_spectrum()

    def on_spectrogram_channel(self, number):
        self.spectrogram.number = number
        self.refresh_spectrum()

    def on_display(self, **changes):
        """A control in the spectrum's Display group changed."""
        for name, value in changes.items():
            setattr(self, name, value)
        self.refresh_spectrum()

    def on_analysis(self, **changes):
        """A control in the spectrum's Analysis group changed."""
        self.analyzer.configure(**changes)
        self.refresh_spectrum()

    def refresh_spectrum(self):
        """Re-analyse the recent samples straight away, with whatever settings
        have just changed. (This is also what makes the controls work while
        stopped.)"""
        live = self.live_channels()
        self.analyzer.update(live)
        if self.spectrogram_mode:
            self.spectrogram.update(self)
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
            self.spectrogram.reset()
            self.refresh_spectrum()
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
            self.spectrogram.reset()
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
        self.signal_setting.setCurrentIndex(1 if word == "sweep" else 0)
        self.signal_freq.setEnabled(word == "sine")     # noise has no frequency to set
        self.send_command(f"gen {word} {self.signal_freq.value():.0f} {self.signal_level.value():.0f} "
                          f"{self.sweep_time.currentData()}")

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
            if self.spectrogram_mode:
                self.spectrogram.update(self)
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
        reference = self.analyzer.reference_tone(self.sample_rate) if self.spectrum_view else None
        for number, channel in enumerate(self.channels):
            result = None
            if channel.enabled and self.spectrum_view:
                result = self.analyzer.measure(number, self.sample_rate, reference)
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
    scope.resize(1180, 760)
    scope.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
