"""
ecu_backup_tab.py — Вкладка резервных копий ЭБУ

Поддерживает:
  • Импорт файлов Full dump и Calibration
  • Автоматическое определение типа файла (Full / Calibration)
  • Сохранение в .../data/ecu_backups/ с JSON-метаданными
  • Таблицу существующих бэкапов с фильтрацией
  • Просмотр деталей и удаление записей

Теория (из документации чип-тюнинга):
  Calibration (частичный):  64 KB – 4 MB   — карты параметров без кода ЭБУ
  Full dump  (полный):       2 MB – 16 MB+  — Flash + EEPROM + программный код
"""

import os
import json
import shutil
import hashlib
import logging
from dataclasses import dataclass, field, asdict
from datetime import datetime
from typing import Optional

from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QSplitter,
    QPushButton, QLabel, QTableWidget, QTableWidgetItem,
    QHeaderView, QGroupBox, QFileDialog, QMessageBox,
    QLineEdit, QComboBox, QTextEdit, QProgressBar,
    QAbstractItemView, QFrame, QFormLayout, QSizePolicy,
    QDialog, QRadioButton, QButtonGroup, QScrollArea,
)
from PyQt5.QtCore import Qt, QThread, pyqtSignal, QSortFilterProxyModel, QTimer
from PyQt5.QtGui import QColor, QFont

logger = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Папка для бэкапов (рядом с front/, внутри data/)
# ---------------------------------------------------------------------------
BACKUP_DIR = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "data", "ecu_backups"
)
os.makedirs(BACKUP_DIR, exist_ok=True)


# ---------------------------------------------------------------------------
# Константы определения типа файла
# ---------------------------------------------------------------------------
# Границы размеров (байт)
CALIBRATION_SIZE_MIN = 64 * 1024          #  64 KB
CALIBRATION_SIZE_MAX = 4 * 1024 * 1024   #   4 MB
FULL_SIZE_MIN        = 2 * 1024 * 1024   #   2 MB
FULL_SIZE_MAX        = 32 * 1024 * 1024  #  32 MB

# Цвета типов для таблицы
COLOR_FULL  = QColor("#1e3a5f")   # тёмно-синий
COLOR_CAL   = QColor("#1e4d2b")   # тёмно-зелёный
COLOR_UNKN  = QColor("#4d3010")   # тёмно-оранжевый


# ---------------------------------------------------------------------------
# Анализатор файлов
# ---------------------------------------------------------------------------
class BackupFileAnalyzer:
    """Определяет тип файла прошивки по размеру и hex-сигнатурам."""

    # Известные сигнатуры заголовков Full-дампов
    FULL_SIGNATURES = [
        b"\xFF\xFF\xFF\xFF",   # стёртая flash (Intel/Motorola)
        b"\x00\x00\x00\x00",   # нулевой EEPROM-старт
        b"\xA5\x5A",           # Bosch checksum marker
    ]

    @staticmethod
    def detect_type(filepath: str) -> str:
        """Возвращает 'Full', 'Calibration' или 'Unknown'."""
        try:
            size = os.path.getsize(filepath)
        except OSError:
            return "Unknown"

        if size >= FULL_SIZE_MIN:
            return "Full"
        if CALIBRATION_SIZE_MIN <= size <= CALIBRATION_SIZE_MAX:
            return "Calibration"
        return "Unknown"

    @staticmethod
    def get_file_info(filepath: str) -> dict:
        """Возвращает полную информацию о файле."""
        info = {
            "size_bytes": 0,
            "size_human": "—",
            "file_type": "Unknown",
            "md5": "—",
            "hex_preview": "—",
            "sections": [],
        }
        try:
            size = os.path.getsize(filepath)
            info["size_bytes"] = size
            info["size_human"] = BackupFileAnalyzer._human_size(size)
            info["file_type"] = BackupFileAnalyzer.detect_type(filepath)

            with open(filepath, "rb") as f:
                header = f.read(64)

            info["hex_preview"] = " ".join(f"{b:02X}" for b in header[:32])
            info["sections"] = BackupFileAnalyzer._detect_sections(header, size)

            # MD5 по первым 256 KB (быстро)
            md5 = hashlib.md5()
            with open(filepath, "rb") as f:
                chunk = f.read(256 * 1024)
                md5.update(chunk)
            info["md5"] = md5.hexdigest()

        except Exception as e:
            logger.warning(f"Ошибка анализа файла {filepath}: {e}")

        return info

    @staticmethod
    def _human_size(size: int) -> str:
        for unit in ("Б", "КБ", "МБ", "ГБ"):
            if size < 1024:
                return f"{size:.1f} {unit}"
            size /= 1024
        return f"{size:.1f} ГБ"

    @staticmethod
    def _detect_sections(header: bytes, total_size: int) -> list:
        sections = []
        if total_size >= FULL_SIZE_MIN:
            sections.append("Flash")
            sections.append("EEPROM (предположительно)")
            if total_size > 4 * 1024 * 1024:
                sections.append("Software Code")
        else:
            sections.append("Calibration Maps")
        return sections


# ---------------------------------------------------------------------------
# Метаданные резервной копии
# ---------------------------------------------------------------------------
@dataclass
class BackupMetadata:
    filename: str            # имя .bin файла (без пути)
    file_type: str           # 'Full' | 'Calibration' | 'Unknown'
    size_bytes: int
    size_human: str
    ecu_type: str
    vin: str
    notes: str
    created_at: str          # ISO-строка
    md5: str
    sections: list = field(default_factory=list)

    def meta_path(self) -> str:
        base = os.path.splitext(self.filename)[0]
        return os.path.join(BACKUP_DIR, base + ".json")

    def bin_path(self) -> str:
        return os.path.join(BACKUP_DIR, self.filename)

    def save(self):
        with open(self.meta_path(), "w", encoding="utf-8") as f:
            json.dump(asdict(self), f, ensure_ascii=False, indent=2)

    @classmethod
    def load(cls, json_path: str) -> "BackupMetadata":
        with open(json_path, "r", encoding="utf-8") as f:
            data = json.load(f)
        return cls(**data)


# ---------------------------------------------------------------------------
# Фоновый поток копирования файла
# ---------------------------------------------------------------------------
class CopyWorker(QThread):
    progress = pyqtSignal(int)          # 0..100
    finished = pyqtSignal(str)          # путь назначения
    error    = pyqtSignal(str)          # сообщение об ошибке

    def __init__(self, src: str, dst: str):
        super().__init__()
        self.src = src
        self.dst = dst

    def run(self):
        try:
            total = os.path.getsize(self.src)
            copied = 0
            chunk = 1024 * 1024  # 1 МБ
            with open(self.src, "rb") as fsrc, open(self.dst, "wb") as fdst:
                while True:
                    buf = fsrc.read(chunk)
                    if not buf:
                        break
                    fdst.write(buf)
                    copied += len(buf)
                    pct = int(copied * 100 / total) if total else 100
                    self.progress.emit(pct)
            self.finished.emit(self.dst)
        except Exception as e:
            self.error.emit(str(e))


# ---------------------------------------------------------------------------
# Диалог создания / импорта бэкапа
# ---------------------------------------------------------------------------
class BackupImportWidget(QGroupBox):
    """Панель выбора файла и заполнения метаданных перед сохранением."""

    backup_saved = pyqtSignal(BackupMetadata)  # сигнал после успешного сохранения

    def __init__(self, parent=None):
        super().__init__("📥 Импорт / Создание резервной копии", parent)
        self._src_path: Optional[str] = None
        self._copy_worker: Optional[CopyWorker] = None
        self._build_ui()

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setSpacing(6)

        # ── Выбор файла ──────────────────────────────────────────────────
        file_row = QHBoxLayout()
        self.path_edit = QLineEdit()
        self.path_edit.setPlaceholderText("Путь к файлу прошивки (.bin / .hex / .ori / .mod)…")
        self.path_edit.setReadOnly(True)
        btn_browse = QPushButton("📂 Обзор")
        btn_browse.setFixedWidth(90)
        btn_browse.clicked.connect(self._browse_file)
        file_row.addWidget(QLabel("Файл:"))
        file_row.addWidget(self.path_edit, 1)
        file_row.addWidget(btn_browse)
        layout.addLayout(file_row)

        # ── Автоопределение ───────────────────────────────────────────────
        self.detect_label = QLabel("Тип файла: —    Размер: —")
        self.detect_label.setStyleSheet("color: #aaa; font-style: italic;")
        layout.addWidget(self.detect_label)

        # ── Метаданные ────────────────────────────────────────────────────
        meta_form = QFormLayout()
        meta_form.setLabelAlignment(Qt.AlignRight)

        self.type_combo = QComboBox()
        self.type_combo.addItems(["Full", "Calibration", "Unknown"])
        meta_form.addRow("Тип прошивки:", self.type_combo)

        self.ecu_edit = QLineEdit()
        self.ecu_edit.setPlaceholderText("напр. Bosch EDC17C46")
        meta_form.addRow("Тип ЭБУ:", self.ecu_edit)

        self.vin_edit = QLineEdit()
        self.vin_edit.setPlaceholderText("17 символов VIN (необязательно)")
        self.vin_edit.setMaxLength(17)
        meta_form.addRow("VIN:", self.vin_edit)

        self.notes_edit = QLineEdit()
        self.notes_edit.setPlaceholderText("комментарий к бэкапу")
        meta_form.addRow("Примечание:", self.notes_edit)

        layout.addLayout(meta_form)

        # ── Прогресс + кнопка ────────────────────────────────────────────
        self.progress_bar = QProgressBar()
        self.progress_bar.setVisible(False)
        layout.addWidget(self.progress_bar)

        btn_row = QHBoxLayout()
        self.btn_save = QPushButton("💾 Сохранить в ecu_backups")
        self.btn_save.setEnabled(False)
        self.btn_save.setFixedHeight(32)
        self.btn_save.clicked.connect(self._save_backup)
        btn_row.addStretch()
        btn_row.addWidget(self.btn_save)
        layout.addLayout(btn_row)

    # ── handlers ─────────────────────────────────────────────────────────

    def _browse_file(self):
        path, _ = QFileDialog.getOpenFileName(
            self,
            "Выбрать файл прошивки ЭБУ",
            "",
            "Файлы прошивок (*.bin *.hex *.ori *.mod *.dump *.fw);;Все файлы (*.*)"
        )
        if not path:
            return
        self._src_path = path
        self.path_edit.setText(path)
        self._analyze_file(path)

    def _analyze_file(self, path: str):
        info = BackupFileAnalyzer.get_file_info(path)
        ftype = info["file_type"]
        size  = info["size_human"]

        color = {"Full": "#4fc3f7", "Calibration": "#81c784"}.get(ftype, "#ffb74d")
        self.detect_label.setText(
            f'Тип файла: <span style="color:{color};font-weight:bold">{ftype}</span>'
            f'   Размер: <b>{size}</b>'
            f'   MD5: <span style="font-family:monospace">{info["md5"][:12]}…</span>'
        )
        self.detect_label.setTextFormat(Qt.RichText)

        idx = {"Full": 0, "Calibration": 1}.get(ftype, 2)
        self.type_combo.setCurrentIndex(idx)
        self.btn_save.setEnabled(True)

    def _save_backup(self):
        if not self._src_path:
            return

        ftype    = self.type_combo.currentText()
        ecu_type = self.ecu_edit.text().strip() or "unknown_ecu"
        vin      = self.vin_edit.text().strip().upper() or "NOVIN"
        notes    = self.notes_edit.text().strip()

        ts       = datetime.now().strftime("%Y%m%d_%H%M%S")
        ext      = os.path.splitext(self._src_path)[1] or ".bin"
        # безопасное имя файла
        safe_ecu = "".join(c if c.isalnum() or c in "-_" else "_" for c in ecu_type)
        filename = f"{safe_ecu}_{vin}_{ftype.lower()}_{ts}{ext}"
        dst_path = os.path.join(BACKUP_DIR, filename)

        info = BackupFileAnalyzer.get_file_info(self._src_path)
        meta = BackupMetadata(
            filename   = filename,
            file_type  = ftype,
            size_bytes = info["size_bytes"],
            size_human = info["size_human"],
            ecu_type   = ecu_type,
            vin        = vin,
            notes      = notes,
            created_at = datetime.now().isoformat(),
            md5        = info["md5"],
            sections   = info["sections"],
        )

        # Копирование в фоне
        self.btn_save.setEnabled(False)
        self.progress_bar.setValue(0)
        self.progress_bar.setVisible(True)

        self._copy_worker = CopyWorker(self._src_path, dst_path)
        self._copy_worker.progress.connect(self.progress_bar.setValue)
        self._copy_worker.finished.connect(lambda _: self._on_copy_done(meta))
        self._copy_worker.error.connect(self._on_copy_error)
        self._copy_worker.start()

    def _on_copy_done(self, meta: BackupMetadata):
        meta.save()
        self.progress_bar.setVisible(False)
        self.btn_save.setEnabled(True)
        logger.info(f"Бэкап сохранён: {meta.filename}")
        self.backup_saved.emit(meta)
        # сброс формы
        self._src_path = None
        self.path_edit.clear()
        self.detect_label.setText("Тип файла: —    Размер: —")
        self.detect_label.setStyleSheet("color: #aaa; font-style: italic;")
        self.ecu_edit.clear()
        self.vin_edit.clear()
        self.notes_edit.clear()

    def _on_copy_error(self, msg: str):
        self.progress_bar.setVisible(False)
        self.btn_save.setEnabled(True)
        logger.error(f"Ошибка копирования: {msg}")
        QMessageBox.critical(self, "Ошибка", f"Не удалось сохранить файл:\n{msg}")


# ---------------------------------------------------------------------------
# Таблица существующих бэкапов
# ---------------------------------------------------------------------------
class BackupTableWidget(QWidget):
    """Таблица резервных копий с фильтрацией и деталями."""

    COLS = ["Файл", "Тип", "Размер", "ЭБУ", "VIN", "Дата", "Примечание"]

    def __init__(self, parent=None):
        super().__init__(parent)
        self._backups: list[BackupMetadata] = []
        self._build_ui()
        self.refresh()

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(4)

        # ── Фильтр + кнопки ──────────────────────────────────────────────
        toolbar = QHBoxLayout()

        self.filter_edit = QLineEdit()
        self.filter_edit.setPlaceholderText("🔍 Фильтр по VIN, ЭБУ или имени…")
        self.filter_edit.textChanged.connect(self._apply_filter)
        toolbar.addWidget(self.filter_edit, 1)

        self.type_filter = QComboBox()
        self.type_filter.addItems(["Все типы", "Full", "Calibration", "Unknown"])
        self.type_filter.currentTextChanged.connect(self._apply_filter)
        toolbar.addWidget(self.type_filter)

        btn_refresh = QPushButton("🔄")
        btn_refresh.setFixedWidth(32)
        btn_refresh.setToolTip("Обновить список")
        btn_refresh.clicked.connect(self.refresh)
        toolbar.addWidget(btn_refresh)

        btn_delete = QPushButton("🗑️ Удалить")
        btn_delete.clicked.connect(self._delete_selected)
        toolbar.addWidget(btn_delete)

        btn_open_dir = QPushButton("📁 Открыть папку")
        btn_open_dir.clicked.connect(self._open_dir)
        toolbar.addWidget(btn_open_dir)

        layout.addLayout(toolbar)

        # ── Таблица ───────────────────────────────────────────────────────
        self.table = QTableWidget(0, len(self.COLS))
        self.table.setHorizontalHeaderLabels(self.COLS)
        self.table.horizontalHeader().setSectionResizeMode(0, QHeaderView.Stretch)
        self.table.horizontalHeader().setSectionResizeMode(6, QHeaderView.Stretch)
        self.table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.table.setAlternatingRowColors(True)
        self.table.verticalHeader().setVisible(False)
        self.table.itemSelectionChanged.connect(self._show_details)
        layout.addWidget(self.table, 1)

        # ── Детали файла ──────────────────────────────────────────────────
        details_box = QGroupBox("📋 Детали выбранного файла")
        det_layout = QVBoxLayout(details_box)
        self.details_text = QTextEdit()
        self.details_text.setReadOnly(True)
        self.details_text.setMaximumHeight(130)
        self.details_text.setFont(QFont("Consolas", 9))
        det_layout.addWidget(self.details_text)
        layout.addWidget(details_box)

    # ── public ────────────────────────────────────────────────────────────

    def refresh(self):
        """Перечитывает папку ecu_backups и перестраивает таблицу."""
        self._backups.clear()
        if os.path.isdir(BACKUP_DIR):
            for fn in sorted(os.listdir(BACKUP_DIR)):
                if fn.endswith(".json"):
                    try:
                        meta = BackupMetadata.load(os.path.join(BACKUP_DIR, fn))
                        # проверяем, что .bin рядом
                        if os.path.isfile(meta.bin_path()):
                            self._backups.append(meta)
                    except Exception as e:
                        logger.warning(f"Не удалось загрузить метаданные {fn}: {e}")
        self._populate_table(self._backups)

    def add_backup(self, meta: BackupMetadata):
        """Добавляет только что сохранённый бэкап без полного перечтения."""
        self._backups.append(meta)
        self._populate_table(self._backups)

    # ── private ───────────────────────────────────────────────────────────

    def _populate_table(self, items: list):
        self.table.setRowCount(0)
        for meta in items:
            row = self.table.rowCount()
            self.table.insertRow(row)
            values = [
                meta.filename,
                meta.file_type,
                meta.size_human,
                meta.ecu_type,
                meta.vin,
                meta.created_at[:19].replace("T", " "),
                meta.notes,
            ]
            for col, val in enumerate(values):
                item = QTableWidgetItem(str(val))
                item.setData(Qt.UserRole, meta)   # храним объект в ячейке
                if col == 1:  # Тип — цветная метка
                    color = {"Full": COLOR_FULL, "Calibration": COLOR_CAL}.get(
                        meta.file_type, COLOR_UNKN
                    )
                    item.setBackground(color)
                    item.setForeground(QColor("white"))
                    item.setFont(QFont("Arial", 9, QFont.Bold))
                self.table.setItem(row, col, item)

    def _apply_filter(self):
        text   = self.filter_edit.text().lower()
        ftype  = self.type_filter.currentText()
        result = [
            m for m in self._backups
            if (ftype == "Все типы" or m.file_type == ftype)
            and (
                text in m.filename.lower()
                or text in m.vin.lower()
                or text in m.ecu_type.lower()
                or text in m.notes.lower()
            )
        ]
        self._populate_table(result)

    def _show_details(self):
        rows = self.table.selectedItems()
        if not rows:
            self.details_text.clear()
            return
        meta: BackupMetadata = rows[0].data(Qt.UserRole)
        if not meta:
            return

        exists = "✅ файл на диске" if os.path.isfile(meta.bin_path()) else "❌ файл не найден"
        sections_str = ", ".join(meta.sections) if meta.sections else "—"

        self.details_text.setHtml(f"""
<b>Файл:</b> {meta.filename}  ({exists})<br>
<b>Тип:</b> {meta.file_type} &nbsp;&nbsp; <b>Размер:</b> {meta.size_human}<br>
<b>ЭБУ:</b> {meta.ecu_type} &nbsp;&nbsp; <b>VIN:</b> {meta.vin}<br>
<b>Секции:</b> {sections_str}<br>
<b>MD5 (256 КБ):</b> <span style="font-family:Consolas">{meta.md5}</span><br>
<b>Создан:</b> {meta.created_at[:19].replace("T", " ")}
{f'<br><b>Примечание:</b> {meta.notes}' if meta.notes else ''}
        """)

    def _delete_selected(self):
        rows = self.table.selectedItems()
        if not rows:
            QMessageBox.information(self, "Удаление", "Выберите запись для удаления.")
            return
        meta: BackupMetadata = rows[0].data(Qt.UserRole)
        reply = QMessageBox.question(
            self, "Удаление бэкапа",
            f"Удалить файл и метаданные?\n\n{meta.filename}",
            QMessageBox.Yes | QMessageBox.No
        )
        if reply != QMessageBox.Yes:
            return
        try:
            if os.path.isfile(meta.bin_path()):
                os.remove(meta.bin_path())
            if os.path.isfile(meta.meta_path()):
                os.remove(meta.meta_path())
            logger.info(f"Бэкап удалён: {meta.filename}")
        except Exception as e:
            QMessageBox.critical(self, "Ошибка", f"Не удалось удалить файлы:\n{e}")
        self.refresh()

    def _open_dir(self):
        import subprocess
        subprocess.Popen(f'explorer "{BACKUP_DIR}"')


# ---------------------------------------------------------------------------
# Главный виджет вкладки
# ---------------------------------------------------------------------------
class ECUBackupTab(QWidget):
    """
    Вкладка «Резервные копии / Прошивка».

    Встраивается в QTabWidget главного окна:

        from ecu_backup_tab import ECUBackupTab
        self.diagnostic_tabs.addTab(ECUBackupTab(self), "💾 Бэкапы")
    """

    log_event_requested = pyqtSignal(str)  # для вывода в общий лог главного окна

    def __init__(self, parent=None):
        super().__init__(parent)
        self._build_ui()

    def _build_ui(self):
        root = QVBoxLayout(self)
        root.setContentsMargins(6, 6, 6, 6)
        root.setSpacing(8)

        # ── Заголовок ──────────────────────────────────────────────────────
        header = QLabel(
            "💾 <b>Управление резервными копиями ЭБУ</b>"
            "<span style='color:#888; font-size:10px;'> — Full dump и Calibration</span>"
        )
        header.setTextFormat(Qt.RichText)
        root.addWidget(header)

        # ── Теоретическая справка ──────────────────────────────────────────
        tip = QLabel(
            "ℹ️  <b>Calibration</b> (64 КБ – 4 МБ): карты параметров, без кода ЭБУ и иммобилайзера.  "
            "<b>Full dump</b> (2 МБ – 16+ МБ): Flash + EEPROM + программный код + Immo."
        )
        tip.setTextFormat(Qt.RichText)
        tip.setWordWrap(True)
        tip.setStyleSheet("color: #aaa; background: #1a1a2e; padding: 4px; border-radius: 4px;")
        root.addWidget(tip)

        # ── Сплиттер: импорт (сверху) / список (снизу) ────────────────────
        splitter = QSplitter(Qt.Vertical)

        self.import_widget = BackupImportWidget()
        self.import_widget.backup_saved.connect(self._on_backup_saved)
        splitter.addWidget(self.import_widget)

        self.table_widget = BackupTableWidget()
        splitter.addWidget(self.table_widget)

        splitter.setSizes([260, 420])
        root.addWidget(splitter, 1)

        # ── Статусная строка ───────────────────────────────────────────────
        self.status_label = QLabel(f"Папка: {BACKUP_DIR}")
        self.status_label.setStyleSheet("color: #666; font-size: 10px;")
        root.addWidget(self.status_label)

    # ── slots ─────────────────────────────────────────────────────────────

    def _on_backup_saved(self, meta: BackupMetadata):
        self.table_widget.add_backup(meta)
        msg = f"Бэкап сохранён: {meta.filename} ({meta.file_type}, {meta.size_human})"
        self.status_label.setText(f"✅ {msg}")
        self.log_event_requested.emit(f"[Бэкап] {msg}")
        QMessageBox.information(
            self,
            "Бэкап создан",
            f"✅ Файл сохранён:\n{meta.filename}\n\nТип: {meta.file_type}\nРазмер: {meta.size_human}"
        )


# ---------------------------------------------------------------------------
# Воркер считывания прошивки с ЭБУ
# ---------------------------------------------------------------------------
class FirmwareReadWorker(QThread):
    """Симулирует считывание прошивки с ЭБУ и сохраняет .bin файл."""

    progress = pyqtSignal(int)    # 0..100
    status   = pyqtSignal(str)    # текстовый статус
    finished = pyqtSignal(str)    # путь к сохранённому файлу
    error    = pyqtSignal(str)    # сообщение об ошибке

    # Размеры генерируемого файла по типу
    READ_SIZES = {
        "Full":        4 * 1024 * 1024,   # 4 МБ
        "Calibration": 256 * 1024,        # 256 КБ
    }
    STEPS = [
        (5,  "[Демо] Инициализация соединения с ЭБУ…"),
        (15, "[Демо] Идентификация блока управления…"),
        (25, "[Демо] Авторизация сессии считывания…"),
        (40, "[Демо] Генерация случайных данных вместо Flash-памяти…"),
        (60, "[Демо] Генерация случайных данных вместо EEPROM…"),
        (75, "[Демо] Генерация случайных калибровочных карт…"),
        (90, "[Демо] Верификация контрольных сумм…"),
        (98, "[Демо] Сохранение файла…"),
    ]

    def __init__(self, dst_path: str, read_type: str):
        super().__init__()
        self.dst_path  = dst_path
        self.read_type = read_type

    def run(self):
        try:
            import time, random
            size = self.READ_SIZES.get(self.read_type, 4 * 1024 * 1024)

            for pct, msg in self.STEPS:
                self.status.emit(msg)
                self.progress.emit(pct)
                time.sleep(random.uniform(0.05, 0.12))

            # Генерируем псевдо-прошивку (реальная реализация читает через API)
            self.status.emit("Запись файла на диск…")
            chunk = 64 * 1024
            written = 0
            with open(self.dst_path, "wb") as f:
                while written < size:
                    to_write = min(chunk, size - written)
                    f.write(bytes([random.randint(0, 255) for _ in range(to_write)]))
                    written += to_write
                    self.progress.emit(98 + int(written * 2 / size))

            self.progress.emit(100)
            self.status.emit("Готово!")
            self.finished.emit(self.dst_path)
        except Exception as e:
            self.error.emit(str(e))


# ---------------------------------------------------------------------------
# Диалог считывания прошивки с ЭБУ
# ---------------------------------------------------------------------------
class ECUReadBackupDialog(QDialog):
    """
    Открывается по кнопке «💾 Создать бэкап ЭБУ».
    Считывает прошивку непосредственно с подключённого ЭБУ
    и сохраняет её в папку ecu_backups.
    """

    log_event_requested = pyqtSignal(str)
    backup_created      = pyqtSignal(str)   # имя сохранённого .bin файла

    def __init__(self, api_client=None, parent=None):
        super().__init__(parent)
        self.api_client = api_client
        self._worker: FirmwareReadWorker | None = None
        self._ecu_info: dict = {}

        self.setWindowTitle("💾 Создание бэкапа ЭБУ")
        self.setMinimumSize(820, 640)
        self.setModal(True)
        self._build_ui()
        self._load_ecu_info()

    # ── Build UI ─────────────────────────────────────────────────────────

    def _build_ui(self):
        # Единый вертикальный layout диалога
        root = QVBoxLayout(self)
        root.setSpacing(0)
        root.setContentsMargins(0, 0, 0, 0)

        # ── Одна большая прокручиваемая область на весь диалог ────────────
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(scroll.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        scroll.setVerticalScrollBarPolicy(Qt.ScrollBarAsNeeded)

        page = QWidget()
        inner = QVBoxLayout(page)
        inner.setSpacing(10)
        inner.setContentsMargins(10, 10, 10, 10)

        # ── Демо-баннер ──────────────────────────────────────────────────
        demo_banner = QLabel(
            "⚠️ Демо-режим: реальное считывание прошивки с ЭБУ ещё не реализовано. "
            "Файл ниже будет заполнен случайными данными и НЕ пригоден для восстановления реального автомобиля."
        )
        demo_banner.setWordWrap(True)
        demo_banner.setStyleSheet(
            "background:#4a3300; color:#ffc107; padding:8px; border-radius:4px; font-weight:bold;"
        )
        inner.addWidget(demo_banner)

        # ── 1. Статус подключения ─────────────────────────────────────────
        conn_group = QGroupBox("Подключённый ЭБУ")
        conn_layout = QFormLayout(conn_group)
        conn_layout.setLabelAlignment(Qt.AlignRight)
        conn_layout.setVerticalSpacing(6)

        self.lbl_status   = QLabel("Проверяется…")
        self.lbl_ecu_type = QLabel("—")
        self.lbl_vin      = QLabel("—")
        self.lbl_protocol = QLabel("—")

        conn_layout.addRow("Статус:", self.lbl_status)
        conn_layout.addRow("Тип ЭБУ:", self.lbl_ecu_type)
        conn_layout.addRow("VIN:", self.lbl_vin)
        conn_layout.addRow("Протокол:", self.lbl_protocol)
        inner.addWidget(conn_group)

        # ── 2. Параметры считывания ────────────────────────────────────────
        read_group = QGroupBox("Параметры считывания")
        read_layout = QVBoxLayout(read_group)
        read_layout.setSpacing(8)

        type_row = QHBoxLayout()
        type_row.addWidget(QLabel("Тип считывания:"))
        self.btn_full = QRadioButton("Full dump  (Flash + EEPROM + код, 2–16 МБ)")
        self.btn_cal  = QRadioButton("Calibration  (только карты параметров, 64 КБ – 4 МБ)")
        self.btn_full.setChecked(True)
        type_row.addWidget(self.btn_full)
        type_row.addWidget(self.btn_cal)
        type_row.addStretch()
        read_layout.addLayout(type_row)

        notes_row = QHBoxLayout()
        notes_row.addWidget(QLabel("Примечание:"))
        self.notes_edit = QLineEdit()
        self.notes_edit.setPlaceholderText("необязательный комментарий к бэкапу")
        notes_row.addWidget(self.notes_edit)
        read_layout.addLayout(notes_row)
        inner.addWidget(read_group)

        # ── 3. Прогресс ───────────────────────────────────────────────────
        prog_group = QGroupBox("Прогресс считывания")
        prog_layout = QVBoxLayout(prog_group)
        prog_layout.setSpacing(6)

        self.progress_bar = QProgressBar()
        self.progress_bar.setValue(0)
        prog_layout.addWidget(self.progress_bar)

        self.lbl_progress_status = QLabel("Нажмите кнопку ниже для начала считывания")
        self.lbl_progress_status.setStyleSheet("color: #aaa;")
        self.lbl_progress_status.setWordWrap(True)
        prog_layout.addWidget(self.lbl_progress_status)
        inner.addWidget(prog_group)

        # ── 4. Кнопки ────────────────────────────────────────────────────
        btn_row = QHBoxLayout()
        self.btn_read = QPushButton("📥  Считать прошивку с ЭБУ")
        self.btn_read.setFixedHeight(40)
        self.btn_read.setStyleSheet(
            "background-color: #2e7d32; color: white; font-size: 14px; font-weight: bold;"
        )
        self.btn_read.clicked.connect(self._start_read)
        btn_close = QPushButton("Закрыть")
        btn_close.setFixedHeight(40)
        btn_close.setFixedWidth(100)
        btn_close.clicked.connect(self.close)
        btn_row.addWidget(self.btn_read, 1)
        btn_row.addWidget(btn_close)
        inner.addLayout(btn_row)

        # ── 5. Существующие бэкапы ────────────────────────────────────────
        existing_group = QGroupBox("Существующие бэкапы")
        existing_layout = QVBoxLayout(existing_group)
        existing_layout.setContentsMargins(6, 6, 6, 6)
        self.table_widget = BackupTableWidget()
        self.table_widget.setMinimumHeight(300)
        existing_layout.addWidget(self.table_widget)
        inner.addWidget(existing_group)

        scroll.setWidget(page)
        root.addWidget(scroll)

    # ── Logic ─────────────────────────────────────────────────────────────

    def _load_ecu_info(self):
        """Запрашивает информацию об ЭБУ через api_client."""
        if not self.api_client:
            self.lbl_status.setText("❌ API клиент не передан")
            self.lbl_status.setStyleSheet("color: #f44336;")
            self.btn_read.setEnabled(False)
            return

        connected = getattr(self.api_client, 'is_connected', False)
        if not connected:
            self.lbl_status.setText("❌ ЭБУ не подключён")
            self.lbl_status.setStyleSheet("color: #f44336;")
            self.btn_read.setEnabled(False)
            return

        self.lbl_status.setText("✅ Подключён")
        self.lbl_status.setStyleSheet("color: #66bb6a;")

        # Получаем данные ЭБУ
        try:
            info = self.api_client.get_ecu_info()
            self._ecu_info = info
            status = self.api_client.get_connection_status()
            self.lbl_ecu_type.setText(info.get("ecu_type", "—") or "—")
            self.lbl_vin.setText(info.get("vin", "—") or "—")
            self.lbl_protocol.setText(status.get("protocol", "—") or "—")
        except Exception:
            pass

    def _get_read_type(self) -> str:
        return "Full" if self.btn_full.isChecked() else "Calibration"

    def _start_read(self):
        if self.api_client and not getattr(self.api_client, 'is_connected', False):
            QMessageBox.warning(self, "Нет подключения",
                                "Подключите ЭБУ перед созданием бэкапа.")
            return

        read_type = self._get_read_type()
        ecu_type  = self._ecu_info.get("ecu_type", "unknown_ecu") or "unknown_ecu"
        vin       = self._ecu_info.get("vin", "NOVIN") or "NOVIN"
        notes     = self.notes_edit.text().strip()

        safe_ecu = "".join(c if c.isalnum() or c in "-_" else "_" for c in ecu_type)
        ts       = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"{safe_ecu}_{vin}_{read_type.lower()}_{ts}.bin"
        dst_path = os.path.join(BACKUP_DIR, filename)

        self.btn_read.setEnabled(False)
        self.progress_bar.setValue(0)

        self._worker = FirmwareReadWorker(dst_path, read_type)
        self._worker.progress.connect(self.progress_bar.setValue)
        self._worker.status.connect(self._on_status)
        self._worker.finished.connect(lambda p: self._on_finished(p, filename, read_type,
                                                                    ecu_type, vin, notes))
        self._worker.error.connect(self._on_error)
        self._worker.start()

    def _on_status(self, msg: str):
        self.lbl_progress_status.setText(msg)
        self.lbl_progress_status.setStyleSheet("color: #4fc3f7;")

    def _on_finished(self, path: str, filename: str, read_type: str,
                     ecu_type: str, vin: str, notes: str):
        info = BackupFileAnalyzer.get_file_info(path)
        meta = BackupMetadata(
            filename   = filename,
            file_type  = read_type,
            size_bytes = info["size_bytes"],
            size_human = info["size_human"],
            ecu_type   = ecu_type,
            vin        = vin,
            notes      = notes,
            created_at = datetime.now().isoformat(),
            md5        = info["md5"],
            sections   = info["sections"],
        )
        meta.save()
        self.table_widget.refresh()

        msg = f"Демо-файл сгенерирован и сохранён: {filename} ({read_type}, {info['size_human']})"
        self.log_event_requested.emit(f"[Бэкап][Демо] {msg}")
        self.lbl_progress_status.setText(f"✅ {msg} (демо-режим)")
        self.lbl_progress_status.setStyleSheet("color: #66bb6a;")
        self.btn_read.setEnabled(True)
        self.backup_created.emit(filename)

        QMessageBox.information(
            self, "Демо-бэкап создан",
            f"⚠️ Это демо-режим: файл заполнен случайными данными, а НЕ реальной прошивкой ЭБУ.\n\n"
            f"Файл: {filename}\nТип: {read_type}\nРазмер: {info['size_human']}\n\n"
            f"Реальное считывание прошивки с ЭБУ в этой версии программы не реализовано."
        )

    def _on_error(self, msg: str):
        self.lbl_progress_status.setText(f"❌ Ошибка: {msg}")
        self.lbl_progress_status.setStyleSheet("color: #f44336;")
        self.btn_read.setEnabled(True)
        QMessageBox.critical(self, "Ошибка считывания",
                             f"Не удалось считать прошивку с ЭБУ:\n{msg}")
