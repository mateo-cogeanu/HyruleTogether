#!/usr/bin/env python3
"""Qt implementation of the Hyrule Together launcher with true alpha compositing."""

from __future__ import annotations

import json
import os
import shlex
import shutil
import signal
import socket
import sys
import time
import xml.etree.ElementTree as ET
from pathlib import Path

try:
    from PySide6.QtCore import QEasingCurve, QProcess, QProcessEnvironment, QPropertyAnimation, QRect, QSize, Qt, QVariantAnimation
    from PySide6.QtGui import QColor, QFont, QIcon, QImage, QPainter, QPixmap
    from PySide6.QtWidgets import (
        QApplication, QCheckBox, QComboBox, QDialog, QDialogButtonBox, QFileDialog, QFormLayout,
        QFrame, QGraphicsOpacityEffect, QGridLayout, QHBoxLayout, QLabel, QLineEdit,
        QMainWindow, QMenu, QMessageBox, QProgressBar, QPushButton, QScrollArea,
        QSizePolicy, QStackedWidget, QVBoxLayout, QWidget,
    )
except ImportError as error:
    raise SystemExit("The GUI requires PySide6. Run ./scripts/setup-cross-platform.sh") from error

import milkbar_launcher as backend


ROOT = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parents[1]))
WPF = ROOT / "WPF .NET 6" / "Breath of the Wild Multiplayer"
IMAGES = WPF / "Images"
BACKGROUNDS = WPF / "Backgrounds"
APP_ICON = next((path for path in (ROOT / "icon.png", Path(__file__).with_name("icon.png")) if path.exists()), None)
WIDTH, HEIGHT = 1188, 639
BOTW_TITLE_SUFFIXES = {"101c9300", "101c9400", "101c9500"}  # Japan, USA, Europe


def installed_title_id(meta_path: Path, kind: str) -> str:
    root = ET.parse(meta_path).getroot()
    title_id = next((str(node.text).strip().lower() for node in root.iter()
                     if node.tag.rsplit("}", 1)[-1] == "title_id" and node.text), "")
    version_text = next((str(node.text).strip() for node in root.iter()
                         if node.tag.rsplit("}", 1)[-1] == "title_version" and node.text), "0")
    try:
        title_version = int(version_text, 0)
    except ValueError:
        title_version = 0
    if len(title_id) != 16 or title_id[8:] not in BOTW_TITLE_SUFFIXES:
        raise ValueError(f"This is not a BOTW {kind} title (found {title_id or 'no title ID'}).")
    if kind == "update":
        if title_id.startswith("0005000e"):
            return title_id
        if title_id.startswith("00050000") and title_version > 0:
            return "0005000e" + title_id[8:]
        raise ValueError(f"This is not a BOTW update (title version {title_version}).")
    if kind == "dlc" and title_id.startswith("0005000c"):
        return title_id
    raise ValueError(f"This is not BOTW DLC (found {title_id}).")


def app_font(size: int, bold: bool = False, italic: bool = False) -> QFont:
    result = QFont("Avenir Next" if sys.platform == "darwin" else "DejaVu Sans", size)
    result.setBold(bold)
    result.setItalic(italic)
    return result


def chroma_pixmap(path: Path, width: int, height: int) -> QPixmap:
    image = QImage(str(path)).convertToFormat(QImage.Format.Format_RGBA8888)
    for y in range(image.height()):
        for x in range(image.width()):
            color = image.pixelColor(x, y)
            if color.green() > 140 and color.green() > color.red() * 1.65 and color.green() > color.blue() * 1.45:
                color.setAlpha(0)
                image.setPixelColor(x, y, color)
    return QPixmap.fromImage(image).scaled(width, height, Qt.AspectRatioMode.KeepAspectRatio,
                                           Qt.TransformationMode.SmoothTransformation)


class GlowButton(QPushButton):
    def __init__(self, text: str = "", parent: QWidget | None = None, prominent: bool = False) -> None:
        super().__init__(text, parent)
        self.prominent = prominent
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setFont(app_font(13 if prominent else 11, bold=True, italic=True))
        self.animation = QVariantAnimation(self)
        self.animation.setDuration(170)
        self.animation.valueChanged.connect(self._set_color)
        self._set_color(QColor(2, 12, 16, 92))

    def _set_color(self, color: QColor) -> None:
        border = "rgba(171,239,247,225)" if self.prominent else "rgba(180,206,211,175)"
        self.setStyleSheet(f"""
            QPushButton {{ background: rgba({color.red()},{color.green()},{color.blue()},{color.alpha()});
              color: white; border: 2px solid {border}; border-radius: 3px; padding: 10px 14px; }}
            QPushButton:disabled {{ color: rgba(210,215,218,90); border-color: rgba(150,160,165,80); }}
        """)

    def enterEvent(self, event) -> None:
        self.animation.stop()
        self.animation.setStartValue(QColor(2, 12, 16, 92))
        self.animation.setEndValue(QColor(18, 86, 103, 155))
        self.animation.start()
        super().enterEvent(event)

    def leaveEvent(self, event) -> None:
        self.animation.stop()
        self.animation.setStartValue(QColor(18, 86, 103, 155))
        self.animation.setEndValue(QColor(2, 12, 16, 92))
        self.animation.start()
        super().leaveEvent(event)


class BackgroundRoot(QWidget):
    def __init__(self, app: "MilkBarWindow") -> None:
        super().__init__()
        self.app = app
        self.pixmap = QPixmap()

    def set_background(self, path: Path) -> None:
        self.pixmap = QPixmap(str(path))
        self.update()

    def paintEvent(self, event) -> None:
        painter = QPainter(self)
        if not self.pixmap.isNull():
            scaled = self.pixmap.scaled(self.size(), Qt.AspectRatioMode.KeepAspectRatioByExpanding,
                                        Qt.TransformationMode.SmoothTransformation)
            x = (scaled.width() - self.width()) // 2
            y = (scaled.height() - self.height()) // 2
            painter.drawPixmap(self.rect(), scaled, QRect(x, y, self.width(), self.height()))
        super().paintEvent(event)


class ServerDialog(QDialog):
    def __init__(self, parent: QWidget, server: dict | None = None, direct: bool = False) -> None:
        super().__init__(parent)
        self.setWindowTitle("Direct Connection" if direct else ("Edit Server" if server else "Add Server"))
        self.setMinimumWidth(520)
        self.setStyleSheet("QDialog { background: #11191d; color: white; } QLabel { color: white; }")
        values = server or {}
        form = QFormLayout(self)
        self.fields: dict[str, QLineEdit] = {}
        defaults = {
            "name": "Direct Connection" if direct else "", "host": "", "port": "5050",
            "password": "", "description": "",
        }
        labels = {"name": "Server name", "host": "Server IP or hostname", "port": "Port",
                  "password": "Password", "description": "Description"}
        for key in defaults:
            field = QLineEdit(str(values.get(key, defaults[key])))
            field.setStyleSheet("background: rgba(0,0,0,145); color: white; border: 1px solid #718087; padding: 8px;")
            self.fields[key] = field
            form.addRow(labels[key], field)
        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
        buttons.accepted.connect(self.validate)
        buttons.rejected.connect(self.reject)
        form.addRow(buttons)

    def validate(self) -> None:
        try:
            port = int(self.fields["port"].text())
            if not self.fields["host"].text().strip() or not 1 <= port <= 65535:
                raise ValueError
        except ValueError:
            QMessageBox.warning(self, "Invalid server", "Enter a hostname and a port from 1 to 65535.")
            return
        self.accept()

    def value(self) -> dict:
        result = {key: field.text().strip() for key, field in self.fields.items()}
        result["port"] = int(result["port"])
        result["mode"] = "Normal"
        return result


class HostServerDialog(QDialog):
    def __init__(self, parent: QWidget, values: dict | None = None) -> None:
        super().__init__(parent)
        self.setWindowTitle("Host a Hyrule Together Server")
        self.setMinimumWidth(540)
        self.setStyleSheet("QDialog { background: #11191d; color: white; } QLabel, QCheckBox { color: white; }")
        values = values or {}
        form = QFormLayout(self)
        defaults = {"name": "My Hyrule", "bind": "localhost", "port": "5050", "password": "",
                    "description": "Explore Hyrule with Friends!"}
        labels = {"name": "Server name", "bind": "Listen address", "port": "Port",
                  "password": "Password", "description": "Description"}
        self.fields: dict[str, QLineEdit] = {}
        for key, default in defaults.items():
            field = QLineEdit(str(values.get(key, default)))
            field.setStyleSheet("background: rgba(0,0,0,145); color: white; border: 1px solid #718087; padding: 8px;")
            self.fields[key] = field
            form.addRow(labels[key], field)
        self.enemy_sync = QCheckBox("Synchronize enemies")
        self.quest_sync = QCheckBox("Synchronize quests and world progress")
        self.enemy_sync.setChecked(bool(values.get("enemy_sync", False)))
        self.quest_sync.setChecked(bool(values.get("quest_sync", False)))
        form.addRow("Gameplay", self.enemy_sync)
        form.addRow("", self.quest_sync)
        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
        buttons.accepted.connect(self.validate)
        buttons.rejected.connect(self.reject)
        form.addRow(buttons)

    def validate(self) -> None:
        try:
            port = int(self.fields["port"].text())
            if not self.fields["name"].text().strip() or not self.fields["bind"].text().strip() or not 1 <= port <= 65535:
                raise ValueError
        except ValueError:
            QMessageBox.warning(self, "Invalid server", "Enter a server name, listen address, and port from 1 to 65535.")
            return
        self.accept()

    def value(self) -> dict:
        result = {key: field.text().strip() for key, field in self.fields.items()}
        result["port"] = int(result["port"])
        result["enemy_sync"] = self.enemy_sync.isChecked()
        result["quest_sync"] = self.quest_sync.isChecked()
        return result


class MilkBarWindow(QMainWindow):
    page_names = ("Settings", "Lobby Browser", "Model Selection")

    def __init__(self) -> None:
        super().__init__()
        self.config = backend.load_config()
        self.config.setdefault("servers", [])
        self.config.setdefault("selected_model", "Link")
        self.config.setdefault("background", "mainWindowBackground.png")
        self.page_index = max(0, min(2, int(os.environ.get("MILKBAR_GUI_PAGE", "1"))))
        self.selected_server = 0
        self.process: QProcess | None = None
        self.server_process: QProcess | None = None
        self.server_pid: int | None = None
        self.graphics_process: QProcess | None = None
        self.setWindowTitle("Hyrule Together")
        if APP_ICON:
            self.setWindowIcon(QIcon(str(APP_ICON)))
        self.setFixedSize(WIDTH, HEIGHT)

        self.root = BackgroundRoot(self)
        self.setCentralWidget(self.root)
        self.root.set_background(self.background_path())
        outer = QVBoxLayout(self.root)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        self.header = QFrame(objectName="header")
        self.header.setFixedHeight(78)
        header_layout = QHBoxLayout(self.header)
        header_layout.setContentsMargins(260, 5, 260, 3)
        self.previous = QPushButton("" if self.page_index == 0 else f"◀\n{self.page_names[self.page_index - 1]}")
        self.previous.clicked.connect(lambda: self.change_page(-1))
        self.previous.setObjectName("nav")
        self.title = QLabel(self.page_names[self.page_index])
        self.title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.title.setFont(app_font(28, bold=True, italic=True))
        self.title.setStyleSheet("color: white; background: transparent;")
        self.next = QPushButton("" if self.page_index == 2 else f"▶\n{self.page_names[self.page_index + 1]}")
        self.next.clicked.connect(lambda: self.change_page(1))
        self.next.setObjectName("nav")
        header_layout.addWidget(self.previous, 1)
        header_layout.addWidget(self.title, 2)
        header_layout.addWidget(self.next, 1)
        outer.addWidget(self.header)

        self.stack = QStackedWidget()
        self.stack.setStyleSheet("background: transparent;")
        self.settings_page = self.make_settings()
        self.lobby_page = self.make_lobby()
        self.models_page = self.make_models()
        for page in (self.settings_page, self.lobby_page, self.models_page):
            self.stack.addWidget(page)
        self.stack.setCurrentIndex(self.page_index)
        self.previous.setEnabled(self.page_index > 0)
        self.next.setEnabled(self.page_index < 2)
        outer.addWidget(self.stack, 1)

        self.footer = QLabel("Hyrule Together  •  Bundled Cemu Runtime   ", objectName="footer")
        self.footer.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        self.footer.setFixedHeight(54)
        self.footer.setFont(app_font(11, italic=True))
        outer.addWidget(self.footer)
        self.apply_style()

        self.loading = QFrame(self.root, objectName="loading")
        loading_layout = QVBoxLayout(self.loading)
        loading_layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.loading_message = QLabel("Starting Cemu…")
        self.loading_message.setFont(app_font(25, bold=True))
        self.loading_message.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.busy = QProgressBar()
        self.busy.setRange(0, 0)
        self.busy.setFixedWidth(420)
        loading_layout.addWidget(self.loading_message)
        loading_layout.addWidget(self.busy)
        self.loading.hide()

    def apply_style(self) -> None:
        self.setStyleSheet("""
            QWidget { color: white; background: transparent; }
            QFrame#header, QLabel#footer { background: rgba(5,12,16,118); }
            QPushButton#nav { background: rgba(0,0,0,42); color: white; border: 0; padding: 5px;
                              font-size: 14px; font-style: italic; }
            QPushButton#nav:hover { background: rgba(31,103,119,105); }
            QFrame#panel { background: rgba(0,0,0,102); border: 2px solid rgba(198,222,225,150); border-radius: 4px; }
            QLabel#sectionTitle { background: rgba(0,0,0,78); padding: 8px; }
            QLineEdit, QComboBox { background: rgba(0,0,0,115); color: white; border: 1px solid rgba(190,213,217,175);
                                  border-radius: 2px; padding: 8px; selection-background-color: #168de2; }
            QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: 0; }
            QScrollBar:vertical { width: 7px; background: rgba(0,0,0,30); }
            QScrollBar::handle:vertical { background: rgba(184,232,238,145); border-radius: 3px; }
            QFrame#loading { background: rgba(0,0,0,178); }
            QProgressBar { border: 1px solid rgba(180,230,236,180); background: rgba(0,0,0,100); height: 9px; }
            QProgressBar::chunk { background: #63d9e8; }
        """)

    def background_path(self) -> Path:
        name = str(self.config.get("background", "mainWindowBackground.png"))
        return BACKGROUNDS / name if name != "mainWindowBackground.png" and (BACKGROUNDS / name).exists() else IMAGES / "mainWindowBackground.png"

    def section_title(self, text: str, size: int = 24) -> QLabel:
        label = QLabel(text, objectName="sectionTitle")
        label.setFont(app_font(size, bold=True, italic=True))
        return label

    def transparent_panel(self) -> QFrame:
        return QFrame(objectName="panel")

    def make_settings(self) -> QWidget:
        page = QWidget()
        layout = QHBoxLayout(page)
        layout.setContentsMargins(12, 10, 12, 10)
        layout.setSpacing(12)
        logo_panel = self.transparent_panel()
        logo_layout = QVBoxLayout(logo_panel)
        logo = QLabel()
        logo.setPixmap(QPixmap(str(IMAGES / "ColoredLogo.png")).scaled(450, 360, Qt.AspectRatioMode.KeepAspectRatio,
                                                                        Qt.TransformationMode.SmoothTransformation))
        logo.setAlignment(Qt.AlignmentFlag.AlignCenter)
        edition = QLabel("CROSS-PLATFORM EDITION")
        edition.setFont(app_font(13, bold=True, italic=True))
        edition.setStyleSheet("color: #82e3ef; background: transparent;")
        edition.setAlignment(Qt.AlignmentFlag.AlignCenter)
        logo_layout.addStretch()
        logo_layout.addWidget(logo)
        logo_layout.addWidget(edition)
        logo_layout.addStretch()
        layout.addWidget(logo_panel, 5)

        form_panel = self.transparent_panel()
        form = QGridLayout(form_panel)
        form.setContentsMargins(20, 12, 20, 12)
        self.setting_fields: dict[str, QLineEdit] = {}
        rows = (("player_name", "PLAYER NAME"),
                ("game_rpx", "BREATH OF THE WILD — U-KING.RPX"))
        for row, (key, label_text) in enumerate(rows):
            label = QLabel(label_text)
            label.setFont(app_font(9, bold=True))
            field = QLineEdit(str(self.config.get(key, "")))
            self.setting_fields[key] = field
            form.addWidget(label, row * 2, 0, 1, 2)
            form.addWidget(field, row * 2 + 1, 0)
            if key == "game_rpx":
                browse = GlowButton("BROWSE")
                browse.clicked.connect(lambda _checked=False, k=key: self.browse(k))
                form.addWidget(browse, row * 2 + 1, 1)

        content_actions = QHBoxLayout()
        update = GlowButton("INSTALL BOTW UPDATE")
        update.clicked.connect(lambda: self.install_title_content("update"))
        dlc = GlowButton("INSTALL BOTW DLC")
        dlc.clicked.connect(lambda: self.install_title_content("dlc"))
        content_actions.addWidget(update)
        content_actions.addWidget(dlc)
        form.addLayout(content_actions, 4, 0, 1, 2)

        graphics = GlowButton("MANAGE CEMU GRAPHIC PACKS")
        graphics.clicked.connect(self.manage_graphic_packs)
        form.addWidget(graphics, 5, 0, 1, 2)

        form.addWidget(QLabel("BACKGROUND"), 6, 0, 1, 2)
        self.backgrounds = QComboBox()
        names = ["mainWindowBackground.png"] + sorted(item.name for item in BACKGROUNDS.iterdir() if item.suffix.lower() in (".png", ".jpg"))
        self.backgrounds.addItems(names)
        self.backgrounds.setCurrentText(str(self.config.get("background", names[0])))
        self.backgrounds.currentTextChanged.connect(self.preview_background)
        form.addWidget(self.backgrounds, 7, 0, 1, 2)
        actions = QHBoxLayout()
        save = GlowButton("◆  SAVE SETTINGS  ◆", prominent=True)
        save.clicked.connect(self.save_settings)
        doctor = GlowButton("◆  RUN SETUP CHECK  ◆", prominent=True)
        doctor.clicked.connect(self.run_doctor)
        actions.addWidget(save)
        actions.addWidget(doctor)
        form.addLayout(actions, 8, 0, 1, 2)
        form.setRowStretch(9, 1)
        layout.addWidget(form_panel, 7)
        return page

    def manage_graphic_packs(self) -> None:
        if backend.cemu_session_active():
            QMessageBox.information(self, "Cemu is running",
                                    "Close the game or the existing graphic-pack manager before changing packs.")
            return
        try:
            cemu = backend.normalize_cemu(str(self.config["cemu"]))
            config_dir = backend.prepare_cemu_config()
            title_id = "00050000" + backend._botw_suffix(self.config)
        except (KeyError, OSError, RuntimeError, ValueError) as error:
            QMessageBox.critical(self, "Graphic packs", str(error))
            return
        self.graphics_process = QProcess(self)
        self.graphics_process.setProgram(str(cemu))
        self.graphics_process.setArguments(["--mlc", str(backend.data_directory() / "cemu" / "mlc01"),
                                            "--graphic-packs", title_id])
        environment = QProcessEnvironment.systemEnvironment()
        environment.insert("MILKBAR_CEMU_DATA_DIR", str(config_dir))
        self.graphics_process.setProcessEnvironment(environment)
        self.graphics_process.finished.connect(self.graphic_packs_closed)
        self.graphics_process.start()
        if not self.graphics_process.waitForStarted(5000):
            QMessageBox.critical(self, "Graphic packs", self.graphics_process.errorString())
            return
        backend.set_cemu_session(int(self.graphics_process.processId()))

    def graphic_packs_closed(self) -> None:
        backend.clear_cemu_session()
        QMessageBox.information(self, "Graphic packs saved",
                                "Your changes will be applied on the next clean game launch.")

    def make_lobby(self) -> QWidget:
        page = QWidget()
        layout = QHBoxLayout(page)
        layout.setContentsMargins(10, 8, 10, 8)
        layout.setSpacing(10)
        left = QWidget()
        left_layout = QVBoxLayout(left)
        toolbar = QHBoxLayout()
        for text, icon, action in (("Add Server", "AddServer_Icon.png", self.add_server),
                                   ("Direct IP", "DirectConnection_Icon.png", self.direct_server),
                                   ("Refresh", "Refresh_Icon.png", self.refresh_server)):
            button = QPushButton(text)
            button.setIcon(QIcon(str(IMAGES / icon)))
            button.setIconSize(QPixmap(str(IMAGES / icon)).size().scaled(42, 42, Qt.AspectRatioMode.KeepAspectRatio))
            button.setFont(app_font(11, bold=True, italic=True))
            button.setStyleSheet("QPushButton { background: rgba(0,0,0,55); border: 0; padding: 8px; } QPushButton:hover { background: rgba(27,105,122,105); }")
            button.clicked.connect(action)
            toolbar.addWidget(button)
        self.host_button = QPushButton("Host Server")
        self.host_button.setIcon(QIcon(str(IMAGES / "Server_Icon.png")))
        self.host_button.setFont(app_font(11, bold=True, italic=True))
        self.host_button.setStyleSheet("QPushButton { background: rgba(0,0,0,55); border: 0; padding: 8px; } QPushButton:hover { background: rgba(27,105,122,105); }")
        self.host_button.clicked.connect(self.toggle_host_server)
        toolbar.addWidget(self.host_button)
        left_layout.addLayout(toolbar)
        self.server_scroll = QScrollArea()
        self.server_scroll.setWidgetResizable(True)
        self.server_holder = QWidget()
        self.server_layout = QVBoxLayout(self.server_holder)
        self.server_layout.setAlignment(Qt.AlignmentFlag.AlignTop)
        self.server_scroll.setWidget(self.server_holder)
        left_layout.addWidget(self.server_scroll)
        layout.addWidget(left, 5)

        detail = self.transparent_panel()
        detail_layout = QVBoxLayout(detail)
        self.detail_name = self.section_title("Select a server", 28)
        self.detail_meta = QLabel("")
        self.detail_meta.setFont(app_font(13, italic=True))
        self.detail_meta.setStyleSheet("color: #62cce4; background: rgba(0,0,0,58); padding: 8px;")
        self.detail_description = QLabel("Add a server to begin your adventure.")
        self.detail_description.setWordWrap(True)
        self.detail_description.setAlignment(Qt.AlignmentFlag.AlignTop)
        self.detail_description.setFont(app_font(18, italic=True))
        self.detail_description.setStyleSheet("background: rgba(0,0,0,55); padding: 14px;")
        self.connect = GlowButton("◆  CONNECT  ◆", prominent=True)
        self.connect.clicked.connect(self.launch_selected)
        detail_layout.addWidget(self.detail_name)
        detail_layout.addWidget(self.detail_meta)
        detail_layout.addWidget(self.detail_description, 1)
        detail_layout.addWidget(self.connect)
        layout.addWidget(detail, 5)
        self.populate_servers()
        return page

    def clear_layout(self, layout) -> None:
        while layout.count():
            item = layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()

    def populate_servers(self) -> None:
        self.clear_layout(self.server_layout)
        servers = self.config.get("servers", [])
        if not servers:
            empty = QLabel("No servers yet\nChoose Add Server or Direct IP")
            empty.setAlignment(Qt.AlignmentFlag.AlignCenter)
            empty.setFont(app_font(16, italic=True))
            empty.setStyleSheet("background: rgba(0,0,0,60); padding: 80px;")
            self.server_layout.addWidget(empty)
            self.connect.setEnabled(False)
            return
        self.selected_server = min(self.selected_server, len(servers) - 1)
        for index, server in enumerate(servers):
            button = QPushButton(f"{server.get('name', 'Server')}\n{server.get('host')}:{server.get('port')}  •  {server.get('mode', 'Normal')}")
            button.setFont(app_font(15, bold=True, italic=True))
            selected = index == self.selected_server
            button.setStyleSheet(f"""QPushButton {{ text-align: left; padding: 14px; color: white;
              background: rgba(0,0,0,{78 if selected else 58}); border: 2px solid {'#d8fbff' if selected else 'rgba(180,210,215,135)'}; }}
              QPushButton:hover {{ background: rgba(20,112,132,110); }}""")
            button.clicked.connect(lambda _checked=False, i=index: self.select_server(i))
            button.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
            button.customContextMenuRequested.connect(lambda point, i=index, b=button: self.server_menu(b, point, i))
            self.server_layout.addWidget(button)
        self.show_server()

    def select_server(self, index: int) -> None:
        self.selected_server = index
        self.populate_servers()

    def show_server(self) -> None:
        servers = self.config.get("servers", [])
        if not servers:
            return
        server = servers[self.selected_server]
        self.detail_name.setText(server.get("name", "Server"))
        self.detail_meta.setText(f"{server.get('host')}:{server.get('port')}   •   Gamemode: {server.get('mode', 'Normal')}")
        self.detail_description.setText(server.get("description") or "A Breath of the Wild multiplayer server.")
        self.connect.setText(f"◆  CONNECT TO {server.get('name', 'SERVER').upper()}  ◆")
        self.connect.setEnabled(True)

    def make_models(self) -> QWidget:
        page = QWidget()
        layout = QHBoxLayout(page)
        layout.setContentsMargins(10, 8, 10, 8)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        holder = QWidget()
        grid = QGridLayout(holder)
        models = sorted(path.stem for path in (IMAGES / "Bust").glob("*.png"))
        self.model_buttons: dict[str, QPushButton] = {}
        for index, name in enumerate(models):
            button = QPushButton()
            button.setFixedSize(128, 128)
            button.setIcon(QIcon(chroma_pixmap(IMAGES / "Bust" / f"{name}.png", 108, 108)))
            button.setIconSize(QSize(108, 108))
            active = name == self.config.get("selected_model", "Link")
            button.setStyleSheet(f"QPushButton {{ background: rgba(0,0,0,65); border: 2px solid {'#e4fdff' if active else 'rgba(180,210,215,140)'}; }} QPushButton:hover {{ background: rgba(20,112,132,100); }}")
            button.clicked.connect(lambda _checked=False, n=name: self.select_model(n))
            self.model_buttons[name] = button
            grid.addWidget(button, index // 5, index % 5)
        scroll.setWidget(holder)
        layout.addWidget(scroll, 7)
        preview = self.transparent_panel()
        preview_layout = QVBoxLayout(preview)
        self.body = QLabel()
        self.body.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.model_name = self.section_title("Link", 22)
        self.model_name.setAlignment(Qt.AlignmentFlag.AlignCenter)
        preview_layout.addWidget(self.body, 1)
        preview_layout.addWidget(self.model_name)
        layout.addWidget(preview, 3)
        self.update_model_preview(str(self.config.get("selected_model", "Link")))
        return page

    def select_model(self, name: str) -> None:
        self.config["selected_model"] = name
        backend.save_config(self.config)
        for model, button in self.model_buttons.items():
            active = model == name
            button.setStyleSheet(f"QPushButton {{ background: rgba(0,0,0,65); border: 2px solid {'#e4fdff' if active else 'rgba(180,210,215,140)'}; }} QPushButton:hover {{ background: rgba(20,112,132,100); }}")
        self.update_model_preview(name)

    def update_model_preview(self, name: str) -> None:
        path = IMAGES / "Body" / f"{name}.png"
        if not path.exists():
            path = IMAGES / "Body" / "Link.png"
        self.body.setPixmap(chroma_pixmap(path, 340, 365))
        self.model_name.setText(f"{name.replace('_', ' ')}\nSelected multiplayer model")

    def change_page(self, delta: int) -> None:
        target = max(0, min(2, self.page_index + delta))
        if target == self.page_index:
            return
        self.page_index = target
        self.stack.setCurrentIndex(target)
        self.title.setText(self.page_names[target])
        self.previous.setText("" if target == 0 else f"◀\n{self.page_names[target - 1]}")
        self.next.setText("" if target == 2 else f"▶\n{self.page_names[target + 1]}")
        self.previous.setEnabled(target > 0)
        self.next.setEnabled(target < 2)
        effect = QGraphicsOpacityEffect(self.stack.currentWidget())
        self.stack.currentWidget().setGraphicsEffect(effect)
        animation = QPropertyAnimation(effect, b"opacity", self)
        animation.setDuration(320)
        animation.setStartValue(0.0)
        animation.setEndValue(1.0)
        animation.setEasingCurve(QEasingCurve.Type.OutCubic)
        animation.finished.connect(lambda: self.stack.currentWidget().setGraphicsEffect(None))
        self.page_animation = animation
        animation.start()

    def browse(self, key: str) -> None:
        title = {"game_rpx": "Select U-King.rpx"}[key]
        path, _ = QFileDialog.getOpenFileName(self, title)
        if path:
            self.setting_fields[key].setText(path)

    def preview_background(self, name: str) -> None:
        self.config["background"] = name
        self.root.set_background(self.background_path())

    def save_settings(self, quiet: bool = False) -> None:
        for key, field in self.setting_fields.items():
            self.config[key] = field.text().strip()
        self.config["background"] = self.backgrounds.currentText()
        backend.save_config(self.config)
        if not quiet:
            QMessageBox.information(self, "Hyrule Together", "Settings saved.")

    def run_doctor(self) -> None:
        self.save_settings(True)
        errors = backend.validation_errors(self.config)
        if errors:
            QMessageBox.critical(self, "Setup needs attention", "\n\n".join(errors))
        else:
            QMessageBox.information(self, "Setup complete", "Cemu, the native client, game, and server settings are ready.")

    def install_title_content(self, kind: str) -> None:
        selected = QFileDialog.getExistingDirectory(self, f"Select the BOTW {kind} folder")
        if not selected:
            return
        source = Path(selected)
        if source.name in ("code", "content", "meta"):
            source = source.parent
        required = [source / name for name in ("code", "content", "meta")]
        meta_path = source / "meta" / "meta.xml"
        if not all(path.is_dir() for path in required) or not meta_path.is_file():
            QMessageBox.warning(self, "Invalid title folder", "Select a decrypted title folder containing code, content, and meta/meta.xml.")
            return
        try:
            # Decrypted updates produced by Cemu or common Wii U dump tools
            # frequently retain the base title ID and identify themselves by
            # their non-zero title version. Cemu installs these under 0005000e.
            target_title_id = installed_title_id(meta_path, kind)
            target = (backend.data_directory() / "cemu" / "mlc01" / "usr" / "title"
                      / target_title_id[:8] / target_title_id[8:])
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.exists():
                backup = target.with_name(f"{target.name}.backup-{int(time.time())}")
                target.rename(backup)
            shutil.copytree(source, target)
        except (OSError, ET.ParseError, ValueError) as error:
            QMessageBox.critical(self, "Installation failed", str(error))
            return
        self.config[f"{kind}_installed"] = str(target)
        backend.save_config(self.config)
        message = f"BOTW {kind} installed into the bundled Cemu environment."
        if backend.bundled_mod_archive() and self.config.get("game_rpx"):
            QApplication.setOverrideCursor(Qt.CursorShape.WaitCursor)
            QApplication.processEvents()
            try:
                backend.install_bundled_mod(self.config, force=True)
                message += "\n\nThe Hyrule Together mod was merged and enabled automatically."
            except RuntimeError as error:
                message += f"\n\nThe title was installed, but the mod will be prepared after the v208 update is available:\n{error}"
            finally:
                QApplication.restoreOverrideCursor()
        QMessageBox.information(self, "Installed", message)

    def edit_server(self, index: int | None = None, direct: bool = False) -> None:
        current = self.config["servers"][index] if index is not None else None
        dialog = ServerDialog(self, current, direct)
        if dialog.exec() != QDialog.DialogCode.Accepted:
            return
        if index is None:
            self.config["servers"].append(dialog.value())
            self.selected_server = len(self.config["servers"]) - 1
        else:
            self.config["servers"][index] = dialog.value()
        backend.save_config(self.config)
        self.populate_servers()

    def add_server(self) -> None:
        self.edit_server()

    def direct_server(self) -> None:
        self.edit_server(direct=True)

    def server_menu(self, button: QWidget, point, index: int) -> None:
        menu = QMenu(self)
        menu.addAction("Edit server", lambda: self.edit_server(index))
        menu.addAction("Remove server", lambda: self.remove_server(index))
        menu.exec(button.mapToGlobal(point))

    def remove_server(self, index: int) -> None:
        name = self.config["servers"][index].get("name", "this server")
        if QMessageBox.question(self, "Remove server", f"Remove {name}?") == QMessageBox.StandardButton.Yes:
            self.config["servers"].pop(index)
            self.selected_server = max(0, self.selected_server - 1)
            backend.save_config(self.config)
            self.populate_servers()

    def refresh_server(self) -> None:
        servers = self.config.get("servers", [])
        if not servers:
            return
        server = servers[self.selected_server]
        try:
            with socket.create_connection((server["host"], int(server["port"])), timeout=1.5):
                self.detail_meta.setText("Online • ready to connect")
                self.detail_meta.setStyleSheet("color: #78ed91; background: rgba(0,0,0,58); padding: 8px;")
        except OSError:
            self.detail_meta.setText("Offline • could not reach server")
            self.detail_meta.setStyleSheet("color: #ff7f87; background: rgba(0,0,0,58); padding: 8px;")

    def server_executable(self) -> Path:
        runtime = backend.bundled_runtime_root()
        if runtime is not None:
            bundled = runtime / "server" / "MBL.DedicatedServer"
            if bundled.is_file():
                return bundled
        machine = "arm64" if os.uname().machine in ("arm64", "aarch64") else "x64"
        rid = ("osx" if sys.platform == "darwin" else "linux") + f"-{machine}"
        return ROOT / "Build" / "server" / rid / "MBL.DedicatedServer"

    def toggle_host_server(self) -> None:
        if self.host_server_running():
            self.stop_host_server()
            return

        dialog = HostServerDialog(self, self.config.get("host_server"))
        if dialog.exec() != QDialog.DialogCode.Accepted:
            return
        self.start_host_server(dialog.value())

    def start_host_server(self, values: dict) -> bool:
        executable = self.server_executable()
        if not executable.is_file():
            QMessageBox.critical(self, "Dedicated server is not built",
                                 "Run scripts/build-server.sh, then try hosting again.")
            return False
        self.config["host_server"] = values
        backend.save_config(self.config)

        server_dir = backend.data_directory() / "server"
        server_dir.mkdir(parents=True, exist_ok=True)
        config_path = server_dir / "ServerConfig.ini"
        enabled = str(bool(values["quest_sync"]))
        config_path.write_text(
            f"[Connection]\nIP={values['bind']}\nPort={values['port']}\nPassword={values['password']}\n\n"
            f"[ServerInformation]\nDescription={values['description']}\n\n[Gamemode]\nDefaultGamemode=True\n\n"
            f"[DefaultGamemode]\nName=Custom\nEnemySync={values['enemy_sync']}\nQuestSync={enabled}\n"
            f"KorokSync={enabled}\nTowerSync={enabled}\nShrineSync={enabled}\nLocationSync={enabled}\n"
            f"DungeonSync={enabled}\nSpecial=0\n", encoding="utf-8")

        pid_file = server_dir / "server.pid"
        pid_file.unlink(missing_ok=True)
        command_file = server_dir / "Start Hyrule Together Server.command"
        command_file.write_text(
            "#!/usr/bin/env bash\n"
            f"export MILKBAR_DATA_DIR={shlex.quote(str(backend.data_directory()))}\n"
            f"export MILKBAR_SERVER_LOG_DIR={shlex.quote(str(server_dir))}\n"
            f"export HYRULE_SERVER_PID_FILE={shlex.quote(str(pid_file))}\n"
            f"cd {shlex.quote(str(server_dir))}\n"
            f"exec {shlex.quote(str(executable))} --config {shlex.quote(str(config_path))}\n",
            encoding="utf-8")
        command_file.chmod(0o755)

        launched = False
        if sys.platform == "darwin":
            launched, _ = QProcess.startDetached("open", ["-a", "Terminal", str(command_file)], str(server_dir))
        else:
            terminals = (
                ("x-terminal-emulator", ["-e", str(command_file)]),
                ("gnome-terminal", ["--", str(command_file)]),
                ("konsole", ["-e", str(command_file)]),
                ("xfce4-terminal", ["--execute", str(command_file)]),
                ("xterm", ["-e", str(command_file)]),
            )
            for terminal, arguments in terminals:
                if shutil.which(terminal):
                    launched, _ = QProcess.startDetached(terminal, arguments, str(server_dir))
                    if launched:
                        break
        if not launched:
            QMessageBox.critical(self, "Hyrule Together Server",
                                 "No supported terminal application was found.")
            return False

        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline and not pid_file.exists():
            QApplication.processEvents()
            time.sleep(0.05)
        if not pid_file.exists():
            QMessageBox.critical(self, "Hyrule Together Server",
                                 "The interactive server terminal opened, but the server did not start.")
            return False
        self.server_pid = int(pid_file.read_text(encoding="utf-8").strip())
        self.host_button.setText("Stop Server")
        local_entry = {"name": values["name"], "host": "127.0.0.1", "port": values["port"],
                       "password": values["password"], "description": values["description"], "mode": "Hosted"}
        servers = self.config.setdefault("servers", [])
        match = next((index for index, item in enumerate(servers)
                      if item.get("host") == "127.0.0.1" and int(item.get("port", 0)) == values["port"]), None)
        if match is None:
            servers.append(local_entry)
            self.selected_server = len(servers) - 1
        else:
            servers[match] = local_entry
            self.selected_server = match
        backend.save_config(self.config)
        self.populate_servers()
        return True

    def host_server_running(self) -> bool:
        if not self.server_pid:
            return False
        try:
            os.kill(self.server_pid, 0)
            return True
        except (ProcessLookupError, PermissionError):
            self.server_pid = None
            return False

    def stop_host_server(self) -> None:
        if self.server_pid:
            try:
                os.kill(self.server_pid, signal.SIGINT)
            except ProcessLookupError:
                pass
        self.server_pid = None
        self.host_button.setText("Host Server")

    def server_output(self) -> None:
        if not self.server_process:
            return
        output = bytes(self.server_process.readAllStandardOutput()).decode(errors="replace").strip()
        if output:
            self.detail_meta.setText(output.splitlines()[-1][:100])

    def server_finished(self, code: int) -> None:
        self.host_button.setText("Host Server")
        if code and self.isVisible():
            QMessageBox.critical(self, "Hyrule Together Server", "The dedicated server stopped unexpectedly. See its LatestLog.txt.")

    def launch_selected(self) -> None:
        servers = self.config.get("servers", [])
        if not servers:
            return
        server = servers[self.selected_server]
        if (server.get("mode") == "Hosted" and server.get("host") in ("127.0.0.1", "localhost")
                and not self.host_server_running()):
            values = dict(self.config.get("host_server") or {})
            values.update(name=server.get("name", "My Hyrule"), port=int(server["port"]),
                          password=server.get("password", ""),
                          description=server.get("description", "Explore Hyrule with Friends!"))
            values.setdefault("bind", "localhost")
            values.setdefault("enemy_sync", True)
            values.setdefault("quest_sync", True)
            if not self.start_host_server(values):
                return
            deadline = time.monotonic() + 5.0
            while time.monotonic() < deadline:
                QApplication.processEvents()
                if not self.host_server_running():
                    QMessageBox.critical(self, "Hyrule Together Server",
                                         "The hosted server stopped before Cemu could connect.")
                    return
                try:
                    with socket.create_connection(("127.0.0.1", int(server["port"])), timeout=0.15):
                        break
                except OSError:
                    time.sleep(0.05)
            else:
                QMessageBox.critical(self, "Hyrule Together Server",
                                     "The hosted server did not begin listening within five seconds.")
                return
        self.config.update(server_name=server["name"], server_host=server["host"], server_port=server["port"],
                           server_password=server.get("password", ""))
        backend.save_config(self.config)
        errors = backend.validation_errors(self.config)
        if errors:
            QMessageBox.critical(self, "Setup needs attention", "\n\n".join(errors))
            self.change_page(-1)
            return
        self.loading.setGeometry(self.root.rect())
        self.loading.raise_()
        self.loading.show()
        self.launch_log = []
        self.process = QProcess(self)
        self.process.setProgram(sys.executable)
        self.process.setArguments([str(Path(__file__).with_name("milkbar_launcher.py")), "launch"])
        self.process.setProcessChannelMode(QProcess.ProcessChannelMode.MergedChannels)
        self.process.readyReadStandardOutput.connect(self.launch_output)
        self.process.finished.connect(self.launch_finished)
        self.process.start()

    def launch_output(self) -> None:
        if self.process:
            output = bytes(self.process.readAllStandardOutput()).decode(errors="replace").strip()
            if output:
                self.launch_log.extend(output.splitlines())
                self.loading_message.setText(output.splitlines()[-1][:90])

    def launch_finished(self, code: int) -> None:
        self.loading.hide()
        if code:
            detail = "\n".join(getattr(self, "launch_log", [])[-4:])
            QMessageBox.critical(self, "Hyrule Together",
                                 detail or "Cemu or the multiplayer client exited with an error.")

    def resizeEvent(self, event) -> None:
        if hasattr(self, "loading") and self.loading.isVisible():
            self.loading.setGeometry(self.root.rect())
        super().resizeEvent(event)

    def closeEvent(self, event) -> None:
        if self.host_server_running():
            self.stop_host_server()
        super().closeEvent(event)


def main() -> int:
    if sys.platform not in ("darwin", "linux"):
        print("The native Hyrule Together GUI supports macOS and Linux.", file=sys.stderr)
        return 1
    application = QApplication(sys.argv)
    application.setApplicationName("Hyrule Together")
    window = MilkBarWindow()
    window.show()
    return application.exec()


if __name__ == "__main__":
    raise SystemExit(main())
