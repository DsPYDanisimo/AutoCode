"""
calibration_editor.py — Редактор калибровочных параметров ЭБУ

Правильный workflow:
  1. Считать калибровку с ЭБУ  →  CalibrationData
  2. Открыть редактор параметра →  editor (графики до / после)
  3. Настроить значения         →  CalibrationData.modified обновляется
  4. Записать в ЭБУ             →  модифицированный файл отправляется
"""

import copy
import logging
from typing import Optional

from PyQt5.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QWidget,
    QPushButton, QLabel, QSlider, QSpinBox,
    QGroupBox, QSizePolicy, QMessageBox,
)
from PyQt5.QtCore import Qt, pyqtSignal, QRect, QPointF
from PyQt5.QtGui import (
    QPainter, QPen, QBrush, QColor, QFont, QPainterPath, QLinearGradient,
    QFontMetrics, QPolygonF,
)

logger = logging.getLogger(__name__)

# ── Цвета ─────────────────────────────────────────────────────────────────────
_C_ORIG = '#4fc3f7'
_C_MOD  = '#ff7043'
_C_BG   = '#1a1a2e'
_C_PLOT = '#25253a'
_C_GRID = '#35354a'
_C_TEXT = '#cccccc'

# ── Данные калибровки по умолчанию ────────────────────────────────────────────
_RPM_13 = [1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000, 5500, 6000, 6500, 7000]
_RPM_11 = [1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000, 5500, 6000]
_LOADS  = [20, 40, 60, 80, 100]


def _mk_defaults() -> dict:
    return {
        "ignition_timing": {
            "rpm":    _RPM_13[:],
            "values": [5.0, 8.0, 12.0, 16.0, 20.0, 24.0, 26.0, 27.0, 26.5, 25.0, 23.0, 20.0, 17.0],
            "unit":   "°BTDC",
        },
        "fuel_map": {
            "rpm":    _RPM_11[:],
            "load":   _LOADS[:],
            "values": [
                [2.1, 2.3, 2.5, 2.7, 2.8, 2.9, 3.0, 3.1, 3.2, 3.3, 3.4],
                [3.5, 3.8, 4.1, 4.4, 4.6, 4.8, 5.0, 5.1, 5.2, 5.3, 5.4],
                [5.2, 5.6, 6.0, 6.3, 6.6, 6.9, 7.2, 7.4, 7.5, 7.6, 7.7],
                [7.0, 7.5, 8.0, 8.5, 8.9, 9.2, 9.5, 9.7, 9.8, 9.9, 10.0],
                [9.0, 9.5, 10.0, 10.5, 11.0, 11.5, 12.0, 12.3, 12.5, 12.6, 12.7],
            ],
            "unit":   "мс",
        },
        "boost_control": {
            "rpm":    _RPM_11[:],
            "values": [0.0, 0.1, 0.3, 0.6, 0.9, 1.1, 1.2, 1.2, 1.15, 1.1, 1.05],
            "unit":   "бар",
        },
        "rev_limit": {
            "value":      6500,
            "soft_limit": 6300,
            "unit":       "об/мин",
        },
    }


# ── Модель данных ──────────────────────────────────────────────────────────────
class CalibrationData:
    """Хранит оригинальные и изменённые калибровочные карты."""

    def __init__(self, source_filename: str = ""):
        self.source_filename = source_filename
        self.original = _mk_defaults()
        self.modified  = copy.deepcopy(self.original)

    @property
    def is_dirty(self) -> bool:
        return self.modified != self.original

    def apply(self, param: str, data: dict):
        self.modified[param] = copy.deepcopy(data)

    def reset(self, param: str):
        self.modified[param] = copy.deepcopy(self.original[param])

    def reset_all(self):
        self.modified = copy.deepcopy(self.original)


# ── Чистый QPainter-рендерер графиков (без внешних зависимостей) ──────────────
class _Chart(QWidget):
    """Универсальный виджет графика на чистом QPainter."""

    _MODE_NONE    = 0
    _MODE_LINES   = 1
    _MODE_HEATMAP = 2
    _MODE_BARS    = 3

    # минимальный размер виджета для рисования
    _MIN_W = 120
    _MIN_H = 80

    def __init__(self, *a, **kw):
        super().__init__()
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self.setMinimumSize(self._MIN_W, self._MIN_H)
        self._mode = self._MODE_NONE
        self._data: dict = {}

    # ── public API ────────────────────────────────────────────────────────
    def plot_lines(self, title, xlabel, ylabel, x, y_orig, y_mod):
        self._mode = self._MODE_LINES
        self._data = dict(title=title, xlabel=xlabel, ylabel=ylabel,
                          x=list(x), y_orig=list(y_orig), y_mod=list(y_mod))
        self.update()

    def plot_heatmaps(self, title, rpm, loads, orig_vals, mod_vals):
        self._mode = self._MODE_HEATMAP
        self._data = dict(title=title, rpm=list(rpm), loads=list(loads),
                          orig=orig_vals, mod=mod_vals)
        self.update()

    def plot_rev_limit(self, title, orig_hard, orig_soft, mod_hard, mod_soft):
        self._mode = self._MODE_BARS
        self._data = dict(title=title,
                          orig_hard=orig_hard, orig_soft=orig_soft,
                          mod_hard=mod_hard,   mod_soft=mod_soft)
        self.update()

    # ── QPainter entry point ──────────────────────────────────────────────
    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        p.fillRect(self.rect(), QColor(_C_BG))
        try:
            if self._mode == self._MODE_LINES:
                self._draw_lines(p)
            elif self._mode == self._MODE_HEATMAP:
                self._draw_heatmap(p)
            elif self._mode == self._MODE_BARS:
                self._draw_bars(p)
        except Exception as e:
            # Никогда не роняем paintEvent — выводим ошибку на экран
            p.setPen(QColor('#ff5555'))
            p.drawText(self.rect(), Qt.AlignCenter, f"Ошибка графика:\n{e}")
        finally:
            p.end()

    # ── helpers ───────────────────────────────────────────────────────────
    @staticmethod
    def _lerp(a, b, t): return a + (b - a) * t

    @staticmethod
    def _plasma(t):
        """t ∈ [0,1] → QColor (plasma colormap approximation)."""
        r = int(_Chart._lerp(13,  240, min(1.0, t * 2.0)))
        g = int(_Chart._lerp(8,   249, abs(t - 0.5) * 2))
        b = int(_Chart._lerp(135, 33,  t))
        return QColor(max(0, min(255, r)), max(0, min(255, g)), max(0, min(255, b)))

    def _plot_rect(self, pad_l=52, pad_r=16, pad_t=30, pad_b=36):
        """Возвращает область графика; гарантирует ненулевые размеры."""
        w = max(1, self.width()  - pad_l - pad_r)
        h = max(1, self.height() - pad_t - pad_b)
        return QRect(pad_l, pad_t, w, h)

    def _draw_grid(self, p, rc, y_min, y_max, nx=5, ny=5):
        """Рисует сетку. nx=0 → нет вертикальных линий, ny=0 → нет горизонтальных."""
        p.setPen(QPen(QColor(_C_GRID), 1, Qt.DashLine))
        if ny > 0:
            for i in range(ny + 1):
                ty = rc.top() + rc.height() * i / ny
                p.drawLine(rc.left(), int(ty), rc.right(), int(ty))
        if nx > 0:
            for i in range(nx + 1):
                tx = rc.left() + rc.width() * i / nx
                p.drawLine(int(tx), rc.top(), int(tx), rc.bottom())

    def _draw_y_labels(self, p, rc, y_min, y_max, ny=5):
        """Метки по оси Y слева от rc."""
        if ny <= 0:
            return
        p.setFont(QFont('Arial', 8))
        p.setPen(QColor(_C_TEXT))
        for i in range(ny + 1):
            val = y_max - (y_max - y_min) * i / ny
            ty  = rc.top() + rc.height() * i / ny
            p.drawText(QRect(0, int(ty) - 10, rc.left() - 4, 20),
                       Qt.AlignRight | Qt.AlignVCenter, f"{val:.1f}")

    def _draw_x_labels(self, p, rc, x_vals, xlabel):
        """Метки по оси X под rc + подпись оси."""
        p.setFont(QFont('Arial', 8))
        p.setPen(QColor(_C_TEXT))
        n = len(x_vals)
        for i, xv in enumerate(x_vals):
            tx = rc.left() + rc.width() * i / (n - 1) if n > 1 else rc.center().x()
            p.drawText(QRect(int(tx) - 25, rc.bottom() + 2, 50, 16),
                       Qt.AlignCenter, str(int(xv)))
        if xlabel:
            p.setPen(QColor('#888888'))
            p.drawText(QRect(rc.left(), self.height() - 18, rc.width(), 16),
                       Qt.AlignCenter, xlabel)

    def _draw_title(self, p, rc, title):
        p.setPen(QColor(_C_TEXT))
        p.setFont(QFont('Arial', 10, QFont.Bold))
        p.drawText(QRect(rc.left(), 4, rc.width(), 24), Qt.AlignCenter, title)

    # ── line chart ────────────────────────────────────────────────────────
    def _draw_lines(self, p):
        d = self._data
        x, y_orig, y_mod = d['x'], d['y_orig'], d['y_mod']
        if not x or self.width() < self._MIN_W or self.height() < self._MIN_H:
            return
        rc = self._plot_rect(pad_l=58, pad_r=16, pad_t=30, pad_b=36)
        y_all = y_orig + y_mod
        y_min, y_max = min(y_all), max(y_all)
        if y_max == y_min:
            y_min -= 1.0; y_max += 1.0

        def _px(xi, yi):
            tx = (rc.left() + (xi - x[0]) / (x[-1] - x[0]) * rc.width()
                  if x[-1] != x[0] else rc.center().x())
            ty = rc.bottom() - (yi - y_min) / (y_max - y_min) * rc.height()
            return QPointF(tx, ty)

        p.fillRect(rc, QColor(_C_PLOT))
        self._draw_grid(p, rc, y_min, y_max, nx=len(x) - 1 if len(x) > 1 else 5, ny=5)
        self._draw_title(p, rc, d['title'])

        # Fill between lines
        if len(x) > 1:
            path = QPainterPath()
            path.moveTo(_px(x[0], y_orig[0]))
            for xi, yi in zip(x, y_orig):
                path.lineTo(_px(xi, yi))
            for xi, yi in zip(reversed(x), reversed(y_mod)):
                path.lineTo(_px(xi, yi))
            path.closeSubpath()
            p.fillPath(path, QColor(255, 112, 67, 30))

        # Original series
        pts_o = [_px(xi, yi) for xi, yi in zip(x, y_orig)]
        p.setPen(QPen(QColor(_C_ORIG), 2))
        for i in range(len(pts_o) - 1):
            p.drawLine(pts_o[i], pts_o[i + 1])
        p.setBrush(QBrush(QColor(_C_ORIG)))
        p.setPen(Qt.NoPen)
        for pt in pts_o:
            p.drawEllipse(pt, 4, 4)

        # Value labels on original points
        p.setFont(QFont('Arial', 7))
        p.setPen(QColor(_C_ORIG))
        for xi, yi, pt in zip(x, y_orig, pts_o):
            p.drawText(QRect(int(pt.x()) - 18, int(pt.y()) - 16, 36, 12),
                       Qt.AlignCenter, f"{yi:.1f}")

        # Modified series
        pts_m = [_px(xi, yi) for xi, yi in zip(x, y_mod)]
        p.setPen(QPen(QColor(_C_MOD), 2, Qt.DashLine))
        p.setBrush(Qt.NoBrush)
        for i in range(len(pts_m) - 1):
            p.drawLine(pts_m[i], pts_m[i + 1])
        p.setPen(QPen(QColor(_C_MOD), 1))
        p.setBrush(QBrush(QColor(_C_MOD)))
        for pt in pts_m:
            p.drawRect(QRect(int(pt.x()) - 3, int(pt.y()) - 3, 6, 6))

        self._draw_y_labels(p, rc, y_min, y_max, ny=5)
        self._draw_x_labels(p, rc, x, d['xlabel'])

        # Y-axis unit label (rotated)
        ylabel = d.get('ylabel', '')
        if ylabel:
            p.save()
            p.translate(12, rc.top() + rc.height() // 2)
            p.rotate(-90)
            p.setFont(QFont('Arial', 8))
            p.setPen(QColor('#888888'))
            fm = p.fontMetrics()
            tw = fm.horizontalAdvance(ylabel)
            p.drawText(-tw // 2, 0, ylabel)
            p.restore()

        # Legend
        p.setBrush(Qt.NoBrush)
        p.setFont(QFont('Arial', 8))
        lx, ly = rc.right() - 130, rc.top() + 6
        p.setPen(QPen(QColor(_C_ORIG), 2))
        p.drawLine(lx, ly + 6, lx + 18, ly + 6)
        p.setPen(QColor(_C_TEXT))
        p.drawText(lx + 22, ly + 10, 'Оригинал')
        p.setPen(QPen(QColor(_C_MOD), 2, Qt.DashLine))
        p.drawLine(lx, ly + 22, lx + 18, ly + 22)
        p.setPen(QColor(_C_TEXT))
        p.drawText(lx + 22, ly + 26, 'Изменено')

    # ── heatmap ───────────────────────────────────────────────────────────
    def _draw_heatmap(self, p):
        d = self._data
        rpm, loads = d['rpm'], d['loads']
        orig, mod  = d['orig'], d['mod']
        if not rpm or not loads or self.width() < self._MIN_W:
            return
        all_vals = [v for row in orig + mod for v in row]
        if not all_vals:
            return
        vmin, vmax = min(all_vals), max(all_vals)
        vrange = vmax - vmin if vmax != vmin else 1.0

        # Layout constants
        PAD_L = 42   # Y-axis labels (нагрузка %)
        PAD_R = 6
        PAD_T = 52   # title (22px) + panel label (18px) + gap (12px)
        PAD_B = 28   # X-axis labels (обороты)
        GAP   = 10   # gap between panels

        W = self.width()
        H = self.height()
        half_w = max(1, (W - PAD_L - PAD_R - GAP) // 2)
        panel_h = max(1, H - PAD_T - PAD_B)
        rows, cols = len(loads), len(rpm)

        # Overall title
        p.setPen(QColor(_C_TEXT))
        p.setFont(QFont('Arial', 10, QFont.Bold))
        p.drawText(QRect(PAD_L, 2, W - PAD_L - PAD_R, 20), Qt.AlignCenter, d['title'])

        for side_idx, (data, label) in enumerate([(orig, 'Оригинал'), (mod, 'Изменено')]):
            px_left = PAD_L + side_idx * (half_w + GAP)
            rc = QRect(px_left, PAD_T, half_w, panel_h)
            p.fillRect(rc, QColor(_C_PLOT))

            # Panel label (above panel, below title)
            lbl_color = QColor(_C_ORIG) if side_idx == 0 else QColor(_C_MOD)
            p.setPen(lbl_color)
            p.setFont(QFont('Arial', 9, QFont.Bold))
            p.drawText(QRect(rc.left(), 24, rc.width(), 18), Qt.AlignCenter, label)

            if cols > 0 and rows > 0:
                cw = rc.width()  / cols
                ch = rc.height() / rows
                for ri, row in enumerate(reversed(data)):
                    for ci, val in enumerate(row):
                        t = (val - vmin) / vrange
                        color = self._plasma(t)
                        cell = QRect(int(rc.left() + ci * cw),
                                     int(rc.top()  + ri * ch),
                                     max(1, int(cw)), max(1, int(ch)))
                        p.fillRect(cell, color)
                        if cw > 22 and ch > 11:
                            # Adaptive text color: dark on bright cells, white on dark
                            r, g, b = color.red(), color.green(), color.blue()
                            lum = 0.299 * r + 0.587 * g + 0.114 * b
                            txt_c = QColor(20, 20, 20, 230) if lum > 130 else QColor(255, 255, 255, 230)
                            p.setPen(txt_c)
                            p.setFont(QFont('Arial', 7, QFont.Bold))
                            p.drawText(cell, Qt.AlignCenter, f"{val:.1f}")

            p.setPen(QPen(QColor(_C_GRID), 1))
            p.drawRect(rc)

            # X-axis: RPM labels (bottom of each panel)
            p.setFont(QFont('Arial', 7))
            p.setPen(QColor(_C_TEXT))
            step = max(1, cols // 6)
            for ci in range(0, cols, step):
                tx = rc.left() + (ci + 0.5) / cols * rc.width()
                p.drawText(QRect(int(tx) - 22, rc.bottom() + 2, 44, 13),
                           Qt.AlignCenter, str(rpm[ci]))

            # Y-axis: Load labels (only on left panel)
            if side_idx == 0 and rows > 0:
                p.setFont(QFont('Arial', 7))
                p.setPen(QColor(_C_TEXT))
                for ri, load_val in enumerate(reversed(loads)):
                    ty = rc.top() + (ri + 0.5) / rows * rc.height()
                    p.drawText(QRect(0, int(ty) - 7, PAD_L - 3, 14),
                               Qt.AlignRight | Qt.AlignVCenter, f"{load_val}%")

        # Axis captions
        p.setPen(QColor('#777777'))
        p.setFont(QFont('Arial', 7))
        p.drawText(QRect(PAD_L, H - 14, W - PAD_L - PAD_R, 13),
                   Qt.AlignCenter, "Обороты двигателя (об/мин) →")
        # Rotated Y-axis caption
        p.save()
        p.translate(10, PAD_T + panel_h // 2)
        p.rotate(-90)
        fm = p.fontMetrics()
        lbl_y = "← Нагрузка (%)"
        tw = fm.horizontalAdvance(lbl_y)
        p.drawText(-tw // 2, 0, lbl_y)
        p.restore()

    # ── bar chart (rev limit) ─────────────────────────────────────────────
    def _draw_bars(self, p):
        d = self._data
        orig_h = d['orig_hard']
        orig_s = d['orig_soft']
        mod_h  = d['mod_hard']
        mod_s  = d['mod_soft']
        if self.width() < self._MIN_W or self.height() < self._MIN_H:
            return

        rc = self._plot_rect(pad_l=64, pad_r=20, pad_t=34, pad_b=44)
        p.fillRect(rc, QColor(_C_PLOT))

        all_v = [orig_h, orig_s, mod_h, mod_s]
        y_max = max(all_v) + 400
        y_min = max(0, min(all_v) - 400)
        y_rng = y_max - y_min  # всегда > 0

        def _bar(x_frac, w_frac, val, color, line1, line2=''):
            bh  = int((val - y_min) / y_rng * rc.height())
            bw  = max(2, int(rc.width() * w_frac))
            bx  = int(rc.left() + x_frac * rc.width() - bw / 2)
            by  = rc.bottom() - bh
            p.fillRect(QRect(bx, by, bw, bh), QColor(color))
            # Значение над столбцом
            p.setPen(QColor(color))
            p.setFont(QFont('Arial', 8, QFont.Bold))
            p.drawText(QRect(bx - 10, by - 18, bw + 20, 16),
                       Qt.AlignCenter, str(val))
            # Подпись под столбцом
            p.setPen(QColor('#aaaaaa'))
            p.setFont(QFont('Arial', 7))
            p.drawText(QRect(bx - 10, rc.bottom() + 4,  bw + 20, 14),
                       Qt.AlignCenter, line1)
            if line2:
                p.drawText(QRect(bx - 10, rc.bottom() + 18, bw + 20, 14),
                           Qt.AlignCenter, line2)

        _bar(0.18, 0.10, orig_s, _C_ORIG, 'Мягкий',   'ориг.')
        _bar(0.36, 0.10, mod_s,  _C_MOD,  'Мягкий',   'изм.')
        _bar(0.64, 0.10, orig_h, _C_ORIG, 'Жёсткий',  'ориг.')
        _bar(0.82, 0.10, mod_h,  _C_MOD,  'Жёсткий',  'изм.')

        # Grid (только горизонтальные линии, nx=0)
        self._draw_grid(p, rc, y_min, y_max, nx=0, ny=5)
        self._draw_y_labels(p, rc, y_min, y_max, ny=5)
        self._draw_title(p, rc, d['title'])

        p.setPen(QPen(QColor(_C_GRID), 1))
        p.drawRect(rc)

        # Y-axis unit label
        p.save()
        p.translate(10, rc.top() + rc.height() // 2)
        p.rotate(-90)
        p.setFont(QFont('Arial', 8))
        p.setPen(QColor('#888888'))
        fm = p.fontMetrics()
        lbl_u = "Обороты (об/мин)"
        tw = fm.horizontalAdvance(lbl_u)
        p.drawText(-tw // 2, 0, lbl_u)
        p.restore()

        # Legend
        p.setFont(QFont('Arial', 8))
        lx, ly = rc.right() - 110, rc.top() + 6
        p.fillRect(QRect(lx, ly + 2, 14, 10), QColor(_C_ORIG))
        p.setPen(QColor(_C_TEXT))
        p.drawText(lx + 18, ly + 11, 'Оригинал')
        p.fillRect(QRect(lx, ly + 18, 14, 10), QColor(_C_MOD))
        p.drawText(lx + 18, ly + 27, 'Изменено')


# ── Вспомогательный слайдер с подписью ────────────────────────────────────────
_SLIDER_STYLE = """
    QSlider::groove:horizontal {
        height: 6px; background: #35354a; border-radius: 3px;
    }
    QSlider::handle:horizontal {
        width: 14px; height: 14px; margin: -4px 0;
        background: #4fc3f7; border-radius: 7px;
    }
    QSlider::sub-page:horizontal {
        background: #4fc3f7; border-radius: 3px;
    }
"""


def _make_slider_row(label_text: str, min_v: float, max_v: float,
                     default_v: float, step: float, unit: str = '') -> tuple:
    """Возвращает (widget, slider, value_label, get_value_fn)."""
    container = QWidget()
    row = QHBoxLayout(container)
    row.setContentsMargins(0, 0, 0, 0)

    lbl = QLabel(label_text)
    lbl.setFixedWidth(240)
    lbl.setStyleSheet("color: #ccc;")

    int_min = round(min_v / step)
    int_max = round(max_v / step)
    int_def = round(default_v / step)

    slider = QSlider(Qt.Horizontal)
    slider.setRange(int_min, int_max)
    slider.setValue(int_def)
    slider.setStyleSheet(_SLIDER_STYLE)

    val_lbl = QLabel()
    val_lbl.setFixedWidth(90)
    val_lbl.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
    val_lbl.setStyleSheet("color: #4fc3f7; font-weight: bold;")

    def _fmt(v_int):
        real = v_int * step
        sign = '+' if real > 0 else ''
        return f"{sign}{real:.2f} {unit}".strip()

    def _update(v_int):
        val_lbl.setText(_fmt(v_int))

    _update(int_def)
    slider.valueChanged.connect(_update)

    def get_value() -> float:
        return slider.value() * step

    row.addWidget(lbl)
    row.addWidget(slider, 1)
    row.addWidget(val_lbl)
    return container, slider, val_lbl, get_value


# ── Базовый редактор ───────────────────────────────────────────────────────────
class _BaseEditor(QDialog):
    changes_applied = pyqtSignal(str, dict)   # (param_key, modified_data)

    def __init__(self, cal_data: CalibrationData, param_key: str,
                 window_title: str, parent=None):
        super().__init__(parent)
        self.cal_data   = cal_data
        self.param_key  = param_key
        # _base = состояние на момент открытия; слайдеры — дельта поверх него
        self._base      = copy.deepcopy(cal_data.modified[param_key])
        self._working   = copy.deepcopy(self._base)

        self.setWindowTitle(window_title)
        self.setMinimumSize(880, 560)
        self.setModal(True)

        root = QVBoxLayout(self)
        root.setSpacing(8)

        # Источник
        src = cal_data.source_filename or "новый файл"
        src_lbl = QLabel(f"Активная калибровка:  <b>{src}</b>")
        src_lbl.setTextFormat(Qt.RichText)
        src_lbl.setStyleSheet("color: #888; font-size: 10px; padding: 2px 0;")
        root.addWidget(src_lbl)

        # График
        self.chart = _Chart()
        root.addWidget(self.chart, 1)

        # Блок управления
        ctrl_group = QGroupBox("Настройка")
        ctrl_layout = QVBoxLayout(ctrl_group)
        self._fill_controls(ctrl_layout)
        root.addWidget(ctrl_group)

        # Кнопки
        btn_row = QHBoxLayout()
        self.btn_apply = QPushButton("✅  Применить изменения")
        self.btn_apply.setStyleSheet(
            "background:#2e7d32; color:white; font-weight:bold; padding:6px 18px;"
        )
        self.btn_apply.clicked.connect(self._apply)
        btn_reset = QPushButton("↩  Сбросить к оригиналу")
        btn_reset.clicked.connect(self._reset)
        btn_cancel = QPushButton("Закрыть")
        btn_cancel.clicked.connect(self.reject)
        btn_row.addWidget(self.btn_apply)
        btn_row.addWidget(btn_reset)
        btn_row.addStretch()
        btn_row.addWidget(btn_cancel)
        root.addLayout(btn_row)

        # Первый рендер откладываем — диалог открывается мгновенно
        from PyQt5.QtCore import QTimer
        QTimer.singleShot(0, self._refresh_chart)

    # -- subclass interface ---------------------------------------------------

    def _fill_controls(self, layout: QVBoxLayout):
        raise NotImplementedError

    def _refresh_chart(self):
        raise NotImplementedError

    def _sync_controls_to_original(self):
        """Ставит слайдеры в «нулевое» положение (без изменений)."""
        raise NotImplementedError

    # -- slots ----------------------------------------------------------------

    def _apply(self):
        self.cal_data.apply(self.param_key, self._working)
        self.changes_applied.emit(self.param_key, self._working)
        self.accept()

    def _reset(self):
        self._base    = copy.deepcopy(self.cal_data.original[self.param_key])
        self._working = copy.deepcopy(self._base)
        self._sync_controls_to_original()
        self._refresh_chart()


# ── Редактор: угол зажигания ───────────────────────────────────────────────────
class IgnitionTimingEditor(_BaseEditor):
    def __init__(self, cal_data: CalibrationData, parent=None):
        self._get_offset = lambda: 0.0
        self._get_trim   = lambda: 0.0
        super().__init__(cal_data, "ignition_timing",
                         "⏱️  Угол опережения зажигания", parent)

    def _fill_controls(self, layout):
        w1, s1, _, self._get_offset = _make_slider_row(
            "Смещение по всему диапазону:", -10.0, 10.0, 0.0, 0.5, "°"
        )
        s1.valueChanged.connect(lambda _: self._on_change())
        layout.addWidget(w1)

        w2, s2, _, self._get_trim = _make_slider_row(
            "Корр. высоких оборотов (≥ 4000 об/мин):", -5.0, 5.0, 0.0, 0.25, "°"
        )
        s2.valueChanged.connect(lambda _: self._on_change())
        layout.addWidget(w2)

        # сохраняем ссылки для сброса
        self._s_offset = s1
        self._s_trim   = s2

    def _on_change(self):
        offset = self._get_offset()
        trim   = self._get_trim()
        orig   = self.cal_data.original["ignition_timing"]
        vals   = [
            round(v + offset + (trim if r >= 4000 else 0), 2)
            for r, v in zip(orig["rpm"], orig["values"])
        ]
        self._working = {"rpm": orig["rpm"][:], "values": vals, "unit": orig["unit"]}
        self._refresh_chart()

    def _sync_controls_to_original(self):
        self._s_offset.setValue(0)
        self._s_trim.setValue(0)

    def _refresh_chart(self):
        orig = self.cal_data.original["ignition_timing"]
        self.chart.plot_lines(
            "Угол опережения зажигания",
            "Обороты, об/мин", f"Угол, {orig['unit']}",
            orig["rpm"], orig["values"], self._working["values"],
        )


# ── Редактор: топливная карта ──────────────────────────────────────────────────
class FuelMapEditor(_BaseEditor):
    def __init__(self, cal_data: CalibrationData, parent=None):
        self._get_richness = lambda: 0.0
        self._get_lowrpm   = lambda: 0.0
        super().__init__(cal_data, "fuel_map",
                         "⛽  Топливная карта", parent)

    def _fill_controls(self, layout):
        w1, s1, _, self._get_richness = _make_slider_row(
            "Общее обогащение / обеднение:", -20.0, 20.0, 0.0, 1.0, "%"
        )
        s1.valueChanged.connect(lambda _: self._on_change())
        layout.addWidget(w1)

        w2, s2, _, self._get_lowrpm = _make_slider_row(
            "Корр. малых оборотов (< 2500 об/мин):", -10.0, 10.0, 0.0, 0.5, "%"
        )
        s2.valueChanged.connect(lambda _: self._on_change())
        layout.addWidget(w2)

        self._s_richness = s1
        self._s_lowrpm   = s2

    def _on_change(self):
        g_pct    = self._get_richness()
        low_pct  = self._get_lowrpm()
        orig     = self.cal_data.original["fuel_map"]
        rpm      = orig["rpm"]
        new_vals = []
        for row in orig["values"]:
            new_row = []
            for col_i, v in enumerate(row):
                scale = 1.0 + g_pct / 100.0
                if rpm[col_i] < 2500:
                    scale += low_pct / 100.0
                new_row.append(round(v * scale, 3))
            new_vals.append(new_row)
        self._working = {
            "rpm": rpm[:], "load": orig["load"][:],
            "values": new_vals, "unit": orig["unit"],
        }
        self._refresh_chart()

    def _sync_controls_to_original(self):
        self._s_richness.setValue(0)
        self._s_lowrpm.setValue(0)

    def _refresh_chart(self):
        orig = self.cal_data.original["fuel_map"]
        self.chart.plot_heatmaps(
            "Топливная карта  (время впрыска, мс)",
            orig["rpm"], orig["load"],
            orig["values"], self._working["values"],
        )


# ── Редактор: управление турбиной ──────────────────────────────────────────────
class BoostControlEditor(_BaseEditor):
    def __init__(self, cal_data: CalibrationData, parent=None):
        self._get_boost = lambda: 0.0
        super().__init__(cal_data, "boost_control",
                         "💨  Управление турбиной", parent)

    def _fill_controls(self, layout):
        w1, s1, _, self._get_boost = _make_slider_row(
            "Максимальное давление наддува:", -0.30, 0.50, 0.0, 0.05, "бар"
        )
        s1.valueChanged.connect(lambda _: self._on_change())
        layout.addWidget(w1)

        w2, s2, _, self._get_ramp = _make_slider_row(
            "Смещение точки нарастания (RPM):", -500.0, 500.0, 0.0, 50.0, "об/мин"
        )
        s2.valueChanged.connect(lambda _: self._on_change())
        layout.addWidget(w2)

        self._s_boost = s1
        self._s_ramp  = s2

    @staticmethod
    def _interp(x_list: list, y_list: list, x: float) -> float:
        """Линейная интерполяция/экстраполяция по крайним точкам."""
        if x <= x_list[0]:
            return float(y_list[0])
        if x >= x_list[-1]:
            return float(y_list[-1])
        for i in range(len(x_list) - 1):
            if x_list[i] <= x <= x_list[i + 1]:
                t = (x - x_list[i]) / (x_list[i + 1] - x_list[i])
                return y_list[i] + t * (y_list[i + 1] - y_list[i])
        return float(y_list[-1])

    def _on_change(self):
        boost_add  = self._get_boost()
        ramp_shift = self._get_ramp()    # смещение нарастания, об/мин
        orig       = self.cal_data.original["boost_control"]
        rpm        = orig["rpm"]
        orig_vals  = orig["values"]
        max_orig   = max(orig_vals) or 1.0

        new_vals = []
        for r in rpm:
            # Смещаем точку поиска на -ramp_shift: положительное значение
            # слайдера сдвигает нарастание вправо (позже), отрицательное — влево
            shifted_r = r - ramp_shift
            v = self._interp(rpm, orig_vals, shifted_r)
            # Масштабируем добавку давления пропорционально исходному значению
            v_new = v + boost_add * (v / max_orig)
            new_vals.append(round(max(0.0, v_new), 3))

        self._working = {"rpm": rpm[:], "values": new_vals, "unit": orig["unit"]}
        self._refresh_chart()

    def _sync_controls_to_original(self):
        self._s_boost.setValue(0)
        self._s_ramp.setValue(0)

    def _refresh_chart(self):
        orig = self.cal_data.original["boost_control"]
        self.chart.plot_lines(
            "Управление турбиной",
            "Обороты, об/мин", f"Давление, {orig['unit']}",
            orig["rpm"], orig["values"], self._working["values"],
        )


# ── Редактор: ограничитель оборотов ───────────────────────────────────────────
class RevLimitEditor(_BaseEditor):
    def __init__(self, cal_data: CalibrationData, parent=None):
        self._hard_spin = None
        self._soft_spin = None
        super().__init__(cal_data, "rev_limit",
                         "⏫  Ограничитель оборотов", parent)

    def _fill_controls(self, layout):
        row = QHBoxLayout()

        row.addWidget(QLabel("Жёсткий лимит (отсечка):"))
        self._hard_spin = QSpinBox()
        self._hard_spin.setRange(3000, 9000)
        self._hard_spin.setSingleStep(100)
        self._hard_spin.setValue(self._base["value"])
        self._hard_spin.setSuffix(" об/мин")
        self._hard_spin.setStyleSheet(
            "color:white; background:#35354a; padding:4px; min-width:110px;"
        )
        self._hard_spin.valueChanged.connect(self._on_change)
        row.addWidget(self._hard_spin)

        row.addSpacing(24)
        row.addWidget(QLabel("Мягкий лимит (начало ограничения):"))
        self._soft_spin = QSpinBox()
        self._soft_spin.setRange(3000, 9000)
        self._soft_spin.setSingleStep(100)
        self._soft_spin.setValue(self._base["soft_limit"])
        self._soft_spin.setSuffix(" об/мин")
        self._soft_spin.setStyleSheet(
            "color:white; background:#35354a; padding:4px; min-width:110px;"
        )
        self._soft_spin.valueChanged.connect(self._on_change)
        row.addWidget(self._soft_spin)
        row.addStretch()
        layout.addLayout(row)

    def _on_change(self):
        self._working = {
            "value":      self._hard_spin.value(),
            "soft_limit": self._soft_spin.value(),
            "unit":       "об/мин",
        }
        self._refresh_chart()

    def _sync_controls_to_original(self):
        orig = self.cal_data.original["rev_limit"]
        self._hard_spin.setValue(orig["value"])
        self._soft_spin.setValue(orig["soft_limit"])

    def _refresh_chart(self):
        orig = self.cal_data.original["rev_limit"]
        self.chart.plot_rev_limit(
            "Ограничитель оборотов",
            orig["value"], orig["soft_limit"],
            self._working["value"], self._working["soft_limit"],
        )


# ── Фабричная функция ──────────────────────────────────────────────────────────
_EDITORS = {
    "ignition_timing": IgnitionTimingEditor,
    "fuel_map":        FuelMapEditor,
    "boost_control":   BoostControlEditor,
    "rev_limit":       RevLimitEditor,
}


def open_parameter_editor(param_key: str,
                          cal_data: CalibrationData,
                          parent=None) -> Optional[_BaseEditor]:
    cls = _EDITORS.get(param_key)
    return cls(cal_data, parent) if cls else None
