from __future__ import annotations

import struct
import sys
import time
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

import serial
from serial.tools import list_ports
from PySide6.QtCore import QObject, QThread, Signal, Slot
from PySide6.QtWidgets import (
    QApplication,
    QComboBox,
    QFileDialog,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QProgressBar,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)


class ProtocolError(Exception):
    """协议执行异常。"""


@dataclass
class ProtocolResponse:
    """协议响应帧解析结果。"""

    command: int
    status: int
    value0: int
    value1: int
    value2: int


@dataclass
class DeviceInfo:
    """统一的设备状态描述。"""

    mode: int
    mode_name: str
    active_slot: int
    switch_slot: int
    app1_valid: int
    app2_valid: int
    upgrade_flag: int


class ProtocolClient:
    """通用OTA1协议客户端。"""

    MAGIC = 0x3141544F
    MAGIC_BYTES = b"OTA1"
    RESPONSE_MASK = 0x8000
    HEADER_FORMAT = "<IHHII"
    RESPONSE_FORMAT = "<IIII"
    HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
    RESPONSE_SIZE = struct.calcsize(RESPONSE_FORMAT)

    def __init__(
        self,
        serial_port: serial.Serial,
        log_callback: Callable[[str], None] | None = None,
        max_payload_length: int = 256,
    ) -> None:
        """绑定串口对象与日志回调。"""
        self.serial_port = serial_port
        self.log_callback = log_callback
        self.max_payload_length = max_payload_length
        self.sequence = 1
        self._text_buffer = ""

    @staticmethod
    def calculate_crc32(data: bytes) -> int:
        """计算与固件端一致的CRC32值。"""
        return zlib.crc32(data) & 0xFFFFFFFF

    def emit_log(self, text: str) -> None:
        """输出一行日志。"""
        if self.log_callback is not None and text:
            self.log_callback(text)

    def consume_text_bytes(self, data: bytes) -> None:
        """从协议流中剥离设备文本日志。"""
        if not data:
            return

        self._text_buffer += data.decode("utf-8", errors="ignore")
        while "\n" in self._text_buffer:
            line, self._text_buffer = self._text_buffer.split("\n", 1)
            line = line.replace("\r", "").strip()
            if line:
                self.emit_log(f"[DEVICE] {line}")

    def flush_text_buffer(self) -> None:
        """输出残留的文本日志。"""
        remain = self._text_buffer.replace("\r", "").strip()
        self._text_buffer = ""
        if remain:
            self.emit_log(f"[DEVICE] {remain}")

    def send_frame(self, command: int, payload: bytes = b"") -> None:
        """发送一帧协议命令。"""
        if len(payload) > self.max_payload_length:
            raise ProtocolError(f"载荷长度超限: {len(payload)}")

        payload_crc32 = self.calculate_crc32(payload) if payload else 0
        header = struct.pack(
            self.HEADER_FORMAT,
            self.MAGIC,
            command,
            len(payload),
            self.sequence,
            payload_crc32,
        )
        self.serial_port.write(header + payload)
        self.serial_port.flush()
        self.sequence += 1

    def read_exact(self, size: int, timeout: float) -> bytes:
        """在超时时间内读取指定字节数。"""
        deadline = time.monotonic() + timeout
        buffer = bytearray()

        while len(buffer) < size:
            remain = deadline - time.monotonic()
            if remain <= 0:
                raise ProtocolError(f"串口读取超时，需要 {size} 字节，实际 {len(buffer)} 字节")

            self.serial_port.timeout = remain
            chunk = self.serial_port.read(size - len(buffer))
            if not chunk:
                continue
            buffer.extend(chunk)

        return bytes(buffer)

    def read_response_header(self, timeout: float) -> bytes:
        """跳过文本日志并找到响应帧头。"""
        deadline = time.monotonic() + timeout
        buffer = bytearray()
        keep_length = len(self.MAGIC_BYTES) - 1

        while True:
            remain = deadline - time.monotonic()
            if remain <= 0:
                raise ProtocolError("等待响应帧头超时")

            self.serial_port.timeout = remain
            chunk = self.serial_port.read(1)
            if not chunk:
                continue

            buffer.extend(chunk)
            magic_index = buffer.find(self.MAGIC_BYTES)
            if magic_index == -1:
                if len(buffer) > keep_length:
                    flush_length = len(buffer) - keep_length
                    self.consume_text_bytes(bytes(buffer[:flush_length]))
                    del buffer[:flush_length]
                continue

            if magic_index > 0:
                self.consume_text_bytes(bytes(buffer[:magic_index]))
                del buffer[:magic_index]

            if len(buffer) < self.HEADER_SIZE:
                continue

            header = bytes(buffer[: self.HEADER_SIZE])
            del buffer[: self.HEADER_SIZE]
            if buffer:
                self.consume_text_bytes(bytes(buffer))
                buffer.clear()
            return header

    def receive_response(self, expected_command: int, timeout: float = 2.0) -> ProtocolResponse:
        """接收并校验响应帧。"""
        header_data = self.read_response_header(timeout)
        magic, command, length, _sequence, payload_crc32 = struct.unpack(self.HEADER_FORMAT, header_data)
        if magic != self.MAGIC:
            raise ProtocolError(f"响应魔术字错误: 0x{magic:08X}")
        if command != (expected_command | self.RESPONSE_MASK):
            raise ProtocolError(f"响应命令不匹配: 0x{command:04X}")
        if length != self.RESPONSE_SIZE:
            raise ProtocolError(f"响应载荷长度错误: {length}")

        payload = self.read_exact(length, timeout)
        if self.calculate_crc32(payload) != payload_crc32:
            raise ProtocolError("响应CRC校验失败")

        self.flush_text_buffer()
        status, value0, value1, value2 = struct.unpack(self.RESPONSE_FORMAT, payload)
        return ProtocolResponse(command=command, status=status, value0=value0, value1=value1, value2=value2)


class BootOtaClient(ProtocolClient):
    """Bootloader OTA协议客户端。"""

    CMD_HELLO = 0x0001
    CMD_START = 0x0002
    CMD_DATA = 0x0003
    CMD_FINISH = 0x0004
    CMD_ABORT = 0x0005
    CMD_QUERY = 0x0006
    START_FORMAT = "<IIII"

    STATUS_TEXT = {
        0x00000000: "成功",
        0x00000001: "超时",
        0x00000002: "帧魔术字错误",
        0x00000003: "帧CRC错误",
        0x00000004: "状态错误",
        0x00000005: "槽位错误",
        0x00000006: "镜像大小错误",
        0x00000007: "Flash错误",
        0x00000008: "镜像校验错误",
        0x00000009: "参数错误",
    }

    @classmethod
    def status_to_text(cls, status: int) -> str:
        """将状态码转换为可读文本。"""
        return cls.STATUS_TEXT.get(status, f"未知错误(0x{status:08X})")

    def hello(self, timeout: float = 1.0) -> ProtocolResponse:
        """发送HELLO握手命令。"""
        self.send_frame(self.CMD_HELLO)
        return self.receive_response(self.CMD_HELLO, timeout)

    def query(self, timeout: float = 2.0) -> ProtocolResponse:
        """发送QUERY命令。"""
        self.send_frame(self.CMD_QUERY)
        return self.receive_response(self.CMD_QUERY, timeout)

    def start(self, slot: int, image_size: int, image_crc32: int, image_version: int) -> ProtocolResponse:
        """发送START命令。"""
        payload = struct.pack(self.START_FORMAT, slot, image_size, image_crc32, image_version)
        self.send_frame(self.CMD_START, payload)
        return self.receive_response(self.CMD_START, timeout=8.0)

    def send_data(self, chunk: bytes) -> ProtocolResponse:
        """发送DATA命令。"""
        self.send_frame(self.CMD_DATA, chunk)
        return self.receive_response(self.CMD_DATA, timeout=5.0)

    def finish(self) -> ProtocolResponse:
        """发送FINISH命令。"""
        self.send_frame(self.CMD_FINISH)
        return self.receive_response(self.CMD_FINISH, timeout=8.0)

    def abort(self) -> ProtocolResponse:
        """发送ABORT命令。"""
        self.send_frame(self.CMD_ABORT)
        return self.receive_response(self.CMD_ABORT, timeout=3.0)


class AppControlClient(ProtocolClient):
    """应用态控制协议客户端。"""

    CMD_HELLO = 0x0101
    CMD_QUERY = 0x0102
    CMD_ENTER_OTA = 0x0103
    CMD_REBOOT = 0x0104
    ENTER_OTA_FORMAT = "<IIII"

    STATUS_TEXT = {
        0x00000000: "成功",
        0x00000003: "CRC错误",
        0x00000004: "状态错误",
        0x00000005: "槽位错误",
        0x00000007: "Flash错误",
        0x00000009: "参数错误",
    }

    @classmethod
    def status_to_text(cls, status: int) -> str:
        """将状态码转换为可读文本。"""
        return cls.STATUS_TEXT.get(status, f"未知错误(0x{status:08X})")

    def __init__(self, serial_port: serial.Serial, log_callback: Callable[[str], None] | None = None) -> None:
        """初始化应用控制客户端。"""
        super().__init__(serial_port, log_callback, max_payload_length=32)

    def hello(self, timeout: float = 1.0) -> ProtocolResponse:
        """发送应用态HELLO命令。"""
        self.send_frame(self.CMD_HELLO)
        return self.receive_response(self.CMD_HELLO, timeout)

    def query(self, timeout: float = 1.5) -> ProtocolResponse:
        """发送应用态QUERY命令。"""
        self.send_frame(self.CMD_QUERY)
        return self.receive_response(self.CMD_QUERY, timeout)

    def enter_ota(self, slot: int, image_size: int, image_crc32: int, image_version: int) -> ProtocolResponse:
        """发送进入OTA命令。"""
        payload = struct.pack(self.ENTER_OTA_FORMAT, slot, image_size, image_crc32, image_version)
        self.send_frame(self.CMD_ENTER_OTA, payload)
        return self.receive_response(self.CMD_ENTER_OTA, timeout=3.0)

    def reboot(self) -> ProtocolResponse:
        """发送重启命令。"""
        self.send_frame(self.CMD_REBOOT)
        return self.receive_response(self.CMD_REBOOT, timeout=2.0)


class OtaWorker(QObject):
    """后台执行串口检测、切换和升级任务。"""

    MODE_BOOT = 1
    MODE_APP1 = 2
    MODE_APP2 = 3

    log = Signal(str)
    progress = Signal(int)
    finished = Signal(bool, str)
    info = Signal(dict)

    def __init__(
        self,
        serial_port: serial.Serial,
        bin_path: str,
        target_slot: int,
        image_version: int,
        handshake_window: float,
        action: str,
    ) -> None:
        """保存任务参数。"""
        super().__init__()
        self.serial_port = serial_port
        self.bin_path = Path(bin_path) if bin_path else None
        self.target_slot = target_slot
        self.image_version = image_version
        self.handshake_window = handshake_window
        self.action = action
        self._cancelled = False

    @staticmethod
    def parse_app_info(response: ProtocolResponse) -> DeviceInfo:
        """解析应用态HELLO或QUERY响应。"""
        mode = response.value0
        state_word = response.value1
        valid_word = response.value2
        active_slot = state_word & 0xFF
        switch_slot = (state_word >> 8) & 0xFF
        upgrade_flag = (state_word >> 16) & 0xFFFF
        app1_valid = valid_word & 0xFFFF
        app2_valid = (valid_word >> 16) & 0xFFFF
        return DeviceInfo(
            mode=mode,
            mode_name=mode_to_text(mode),
            active_slot=active_slot,
            switch_slot=switch_slot,
            app1_valid=app1_valid,
            app2_valid=app2_valid,
            upgrade_flag=upgrade_flag,
        )

    @staticmethod
    def parse_boot_hello(response: ProtocolResponse) -> DeviceInfo:
        """解析Bootloader HELLO响应。"""
        return DeviceInfo(
            mode=OtaWorker.MODE_BOOT,
            mode_name="BOOT",
            active_slot=response.value0,
            switch_slot=0,
            app1_valid=response.value1,
            app2_valid=response.value2,
            upgrade_flag=0,
        )

    @staticmethod
    def parse_boot_query(response: ProtocolResponse) -> DeviceInfo:
        """解析Bootloader QUERY响应。"""
        app1_info = response.value1
        app2_info = response.value2
        return DeviceInfo(
            mode=OtaWorker.MODE_BOOT,
            mode_name="BOOT",
            active_slot=(app1_info >> 16) & 0xFFFF,
            switch_slot=(app2_info >> 16) & 0xFFFF,
            app1_valid=app1_info & 0xFFFF,
            app2_valid=app2_info & 0xFFFF,
            upgrade_flag=response.value0,
        )

    def emit_device_info(self, device: DeviceInfo) -> None:
        """将统一设备状态发送到界面。"""
        self.info.emit(
            {
                "mode": device.mode_name,
                "active_slot": device.active_slot,
                "switch_slot": device.switch_slot,
                "app1_valid": device.app1_valid,
                "app2_valid": device.app2_valid,
                "upgrade_flag": device.upgrade_flag,
            }
        )

    def ensure_boot_status_ok(self, response: ProtocolResponse, stage: str) -> None:
        """检查Bootloader响应状态。"""
        if response.status != 0:
            raise ProtocolError(
                f"{stage}失败: {BootOtaClient.status_to_text(response.status)} | "
                f"value0=0x{response.value0:08X}, value1=0x{response.value1:08X}, value2=0x{response.value2:08X}"
            )

    def ensure_app_status_ok(self, response: ProtocolResponse, stage: str) -> None:
        """检查应用态响应状态。"""
        if response.status != 0:
            raise ProtocolError(
                f"{stage}失败: {AppControlClient.status_to_text(response.status)} | "
                f"value0=0x{response.value0:08X}, value1=0x{response.value1:08X}, value2=0x{response.value2:08X}"
            )

    def reset_serial_buffers(self) -> None:
        """清空串口缓存。"""
        self.serial_port.reset_input_buffer()
        self.serial_port.reset_output_buffer()

    def try_detect_device(self) -> tuple[str, DeviceInfo]:
        """检测当前设备处于APP还是BOOT。"""
        app_client = AppControlClient(self.serial_port, self.log.emit)
        boot_client = BootOtaClient(self.serial_port, self.log.emit)
        deadline = time.monotonic() + max(self.handshake_window, 1.5)
        last_error = "设备无响应"

        self.log.emit("检测设备当前模式")
        while time.monotonic() < deadline:
            try:
                self.reset_serial_buffers()
                response = app_client.hello(timeout=0.8)
                self.ensure_app_status_ok(response, "APP_HELLO")
                device = self.parse_app_info(response)
                self.log.emit(f"检测到应用态: {device.mode_name}")
                return "app", device
            except Exception as exc:
                last_error = str(exc)

            try:
                self.reset_serial_buffers()
                response = boot_client.hello(timeout=0.8)
                self.ensure_boot_status_ok(response, "BOOT_HELLO")
                device = self.parse_boot_hello(response)
                self.log.emit("检测到Bootloader")
                return "boot", device
            except Exception as exc:
                last_error = str(exc)

            time.sleep(0.15)

        raise ProtocolError(f"设备识别失败: {last_error}")

    def wait_for_bootloader(self) -> DeviceInfo:
        """等待设备切换进入Bootloader。"""
        boot_client = BootOtaClient(self.serial_port, self.log.emit)
        deadline = time.monotonic() + max(self.handshake_window, 3.0)
        last_error = "未进入Bootloader"

        self.log.emit("等待设备重启并进入Bootloader")
        while time.monotonic() < deadline:
            try:
                self.reset_serial_buffers()
                response = boot_client.hello(timeout=0.8)
                self.ensure_boot_status_ok(response, "BOOT_HELLO")
                device = self.parse_boot_hello(response)
                self.log.emit("Bootloader握手成功")
                return device
            except Exception as exc:
                last_error = str(exc)
                time.sleep(0.15)

        raise ProtocolError(f"等待Bootloader失败: {last_error}")

    @Slot()
    def run(self) -> None:
        """执行查询或升级任务。"""
        try:
            if not self.serial_port.is_open:
                raise ProtocolError("串口尚未连接")

            self.reset_serial_buffers()
            device_kind, device = self.try_detect_device()
            self.emit_device_info(device)

            if self.action == "query":
                self.run_query(device_kind)
                return

            self.run_upgrade(device_kind, device)
        except Exception as exc:
            self.finished.emit(False, str(exc))

    def run_query(self, device_kind: str) -> None:
        """执行查询任务。"""
        if device_kind == "app":
            client = AppControlClient(self.serial_port, self.log.emit)
            response = client.query(timeout=1.5)
            self.ensure_app_status_ok(response, "APP_QUERY")
            device = self.parse_app_info(response)
        else:
            client = BootOtaClient(self.serial_port, self.log.emit)
            response = client.query(timeout=2.0)
            self.ensure_boot_status_ok(response, "BOOT_QUERY")
            device = self.parse_boot_query(response)

        self.emit_device_info(device)
        self.finished.emit(True, "查询完成")

    def run_upgrade(self, device_kind: str, device: DeviceInfo) -> None:
        """执行升级任务。"""
        if self.bin_path is None or not self.bin_path.exists():
            raise ProtocolError("BIN文件不存在")

        image = self.bin_path.read_bytes()
        image_crc32 = ProtocolClient.calculate_crc32(image)
        self.log.emit(f"镜像大小: {len(image)} 字节")
        self.log.emit(f"镜像CRC32: 0x{image_crc32:08X}")

        if device.active_slot == self.target_slot:
            raise ProtocolError("目标槽位与当前活动槽位相同，拒绝覆盖当前运行分区")

        if device_kind == "app":
            app_client = AppControlClient(self.serial_port, self.log.emit)
            response = app_client.enter_ota(self.target_slot, len(image), image_crc32, self.image_version)
            self.ensure_app_status_ok(response, "APP_ENTER_OTA")
            self.log.emit("应用已接受升级请求，准备重启到Bootloader")
            time.sleep(0.3)
            boot_device = self.wait_for_bootloader()
            self.emit_device_info(boot_device)
        else:
            self.log.emit("当前已在Bootloader，直接进入升级流程")

        boot_client = BootOtaClient(self.serial_port, self.log.emit)
        start_response = boot_client.start(self.target_slot, len(image), image_crc32, self.image_version)
        self.ensure_boot_status_ok(start_response, "START")
        self.log.emit("开始发送数据包")

        sent = 0
        while sent < len(image):
            if self._cancelled:
                abort_response = boot_client.abort()
                self.ensure_boot_status_ok(abort_response, "ABORT")
                self.finished.emit(False, "用户中止升级")
                return

            chunk = image[sent : sent + boot_client.max_payload_length]
            data_response = boot_client.send_data(chunk)
            self.ensure_boot_status_ok(data_response, "DATA")
            sent = data_response.value0
            percent = int(sent * 100 / len(image))
            self.progress.emit(percent)
            self.log.emit(f"已发送 {sent}/{len(image)} 字节")

        finish_response = boot_client.finish()
        self.ensure_boot_status_ok(finish_response, "FINISH")
        self.progress.emit(100)
        self.finished.emit(True, "升级完成，设备将按metadata切换到新分区")

    def cancel(self) -> None:
        """请求中止升级任务。"""
        self._cancelled = True



def mode_to_text(mode: int) -> str:
    """将模式编码转换为文本。"""
    if mode == OtaWorker.MODE_BOOT:
        return "BOOT"
    if mode == OtaWorker.MODE_APP1:
        return "APP1"
    if mode == OtaWorker.MODE_APP2:
        return "APP2"
    return f"UNKNOWN({mode})"


class MainWindow(QMainWindow):
    """OTA串口升级图形界面。"""

    def __init__(self) -> None:
        """初始化界面与默认参数。"""
        super().__init__()
        self.worker_thread: QThread | None = None
        self.worker: OtaWorker | None = None
        self.serial_port: serial.Serial | None = None

        self.setWindowTitle("STM32 双Bank 串口OTA工具")
        self.resize(960, 720)
        self.build_ui()
        self.refresh_ports()
        self.update_connection_state(False)

    def build_ui(self) -> None:
        """创建主界面控件。"""
        central = QWidget()
        self.setCentralWidget(central)
        root_layout = QVBoxLayout(central)

        serial_group = QGroupBox("串口配置")
        serial_layout = QGridLayout(serial_group)
        self.port_combo = QComboBox()
        self.refresh_button = QPushButton("刷新串口")
        self.connect_button = QPushButton("连接串口")
        self.disconnect_button = QPushButton("关闭串口")
        self.refresh_button.clicked.connect(self.refresh_ports)
        self.connect_button.clicked.connect(self.connect_serial)
        self.disconnect_button.clicked.connect(self.disconnect_serial)
        self.baud_edit = QLineEdit("115200")
        self.handshake_edit = QLineEdit("5.0")
        self.connection_label = QLabel("未连接")
        serial_layout.addWidget(QLabel("串口"), 0, 0)
        serial_layout.addWidget(self.port_combo, 0, 1)
        serial_layout.addWidget(self.refresh_button, 0, 2)
        serial_layout.addWidget(self.connect_button, 0, 3)
        serial_layout.addWidget(self.disconnect_button, 0, 4)
        serial_layout.addWidget(QLabel("波特率"), 1, 0)
        serial_layout.addWidget(self.baud_edit, 1, 1)
        serial_layout.addWidget(QLabel("等待窗口(秒)"), 1, 2)
        serial_layout.addWidget(self.handshake_edit, 1, 3)
        serial_layout.addWidget(QLabel("连接状态"), 2, 0)
        serial_layout.addWidget(self.connection_label, 2, 1, 1, 4)
        root_layout.addWidget(serial_group)

        image_group = QGroupBox("镜像配置")
        image_layout = QGridLayout(image_group)
        self.file_edit = QLineEdit()
        self.browse_button = QPushButton("选择BIN")
        self.browse_button.clicked.connect(self.select_file)
        self.slot_combo = QComboBox()
        self.slot_combo.addItem("升级到 APP1 / BANK1", 1)
        self.slot_combo.addItem("升级到 APP2 / BANK2", 2)
        self.version_spin = QSpinBox()
        self.version_spin.setRange(0, 0x7FFFFFFF)
        self.version_spin.setValue(1)
        image_layout.addWidget(QLabel("BIN文件"), 0, 0)
        image_layout.addWidget(self.file_edit, 0, 1, 1, 3)
        image_layout.addWidget(self.browse_button, 0, 4)
        image_layout.addWidget(QLabel("目标槽位"), 1, 0)
        image_layout.addWidget(self.slot_combo, 1, 1)
        image_layout.addWidget(QLabel("版本号"), 1, 2)
        image_layout.addWidget(self.version_spin, 1, 3)
        root_layout.addWidget(image_group)

        action_layout = QHBoxLayout()
        self.query_button = QPushButton("查询状态")
        self.upgrade_button = QPushButton("开始升级")
        self.cancel_button = QPushButton("中止")
        self.clear_log_button = QPushButton("清空日志")
        self.query_button.clicked.connect(self.start_query)
        self.upgrade_button.clicked.connect(self.start_upgrade)
        self.cancel_button.clicked.connect(self.cancel_task)
        self.clear_log_button.clicked.connect(self.clear_log)
        action_layout.addWidget(self.query_button)
        action_layout.addWidget(self.upgrade_button)
        action_layout.addWidget(self.cancel_button)
        action_layout.addWidget(self.clear_log_button)
        root_layout.addLayout(action_layout)

        status_group = QGroupBox("设备状态")
        status_layout = QGridLayout(status_group)
        self.mode_label = QLabel("-")
        self.active_slot_label = QLabel("-")
        self.switch_slot_label = QLabel("-")
        self.app1_valid_label = QLabel("-")
        self.app2_valid_label = QLabel("-")
        self.upgrade_flag_label = QLabel("-")
        status_layout.addWidget(QLabel("当前模式"), 0, 0)
        status_layout.addWidget(self.mode_label, 0, 1)
        status_layout.addWidget(QLabel("当前活动槽位"), 0, 2)
        status_layout.addWidget(self.active_slot_label, 0, 3)
        status_layout.addWidget(QLabel("待切换槽位"), 1, 0)
        status_layout.addWidget(self.switch_slot_label, 1, 1)
        status_layout.addWidget(QLabel("APP1有效"), 1, 2)
        status_layout.addWidget(self.app1_valid_label, 1, 3)
        status_layout.addWidget(QLabel("APP2有效"), 2, 0)
        status_layout.addWidget(self.app2_valid_label, 2, 1)
        status_layout.addWidget(QLabel("升级标志"), 2, 2)
        status_layout.addWidget(self.upgrade_flag_label, 2, 3)
        root_layout.addWidget(status_group)

        self.progress_bar = QProgressBar()
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)
        root_layout.addWidget(self.progress_bar)

        self.log_edit = QPlainTextEdit()
        self.log_edit.setReadOnly(True)
        root_layout.addWidget(self.log_edit, 1)

    @Slot()
    def refresh_ports(self) -> None:
        """刷新系统串口列表。"""
        current_port = self.port_combo.currentData()
        self.port_combo.clear()
        for port in list_ports.comports():
            text = f"{port.device} | {port.description}"
            self.port_combo.addItem(text, port.device)
            if port.device == current_port:
                self.port_combo.setCurrentIndex(self.port_combo.count() - 1)

    def update_connection_state(self, connected: bool) -> None:
        """根据串口连接状态刷新界面按钮。"""
        task_running = self.worker is not None
        self.connection_label.setText("已连接" if connected else "未连接")
        self.port_combo.setEnabled((not connected) and (not task_running))
        self.refresh_button.setEnabled((not connected) and (not task_running))
        self.baud_edit.setEnabled((not connected) and (not task_running))
        self.connect_button.setEnabled((not connected) and (not task_running))
        self.disconnect_button.setEnabled(connected and (not task_running))
        self.query_button.setEnabled(connected and (not task_running))
        self.upgrade_button.setEnabled(connected and (not task_running))
        self.cancel_button.setEnabled(task_running)
        self.browse_button.setEnabled(not task_running)
        self.file_edit.setEnabled(not task_running)
        self.slot_combo.setEnabled(not task_running)
        self.version_spin.setEnabled(not task_running)
        self.handshake_edit.setEnabled(not task_running)
        self.clear_log_button.setEnabled(True)

    @Slot()
    def connect_serial(self) -> None:
        """打开选中的串口并保留连接。"""
        port_name = self.port_combo.currentData()
        if not port_name:
            QMessageBox.warning(self, "缺少串口", "请先选择串口。")
            return

        try:
            baudrate = int(self.baud_edit.text().strip())
        except ValueError:
            QMessageBox.warning(self, "参数错误", "波特率格式不正确。")
            return

        try:
            self.serial_port = serial.Serial(port_name, baudrate, timeout=0.2)
            self.serial_port.reset_input_buffer()
            self.serial_port.reset_output_buffer()
            self.append_log(f"串口已连接: {port_name} @ {baudrate}")
            self.update_connection_state(True)
        except Exception as exc:
            self.serial_port = None
            QMessageBox.critical(self, "连接失败", str(exc))

    @Slot()
    def disconnect_serial(self) -> None:
        """关闭当前串口连接。"""
        if self.worker is not None:
            QMessageBox.warning(self, "任务进行中", "请先等待当前任务结束。")
            return

        if self.serial_port is not None:
            try:
                port_name = self.serial_port.port
                self.serial_port.close()
                self.append_log(f"串口已关闭: {port_name}")
            except Exception as exc:
                self.append_log(f"关闭串口失败: {exc}")
            finally:
                self.serial_port = None
        self.update_connection_state(False)

    @Slot()
    def select_file(self) -> None:
        """选择待升级的BIN文件。"""
        file_path, _ = QFileDialog.getOpenFileName(self, "选择BIN文件", "", "BIN文件 (*.bin)")
        if file_path:
            self.file_edit.setText(file_path)

    @Slot()
    def clear_log(self) -> None:
        """清空日志窗口。"""
        self.log_edit.clear()

    @Slot()
    def start_query(self) -> None:
        """启动查询任务。"""
        self.start_worker("query")

    @Slot()
    def start_upgrade(self) -> None:
        """启动升级任务。"""
        if not self.file_edit.text().strip():
            QMessageBox.warning(self, "缺少文件", "请先选择BIN文件。")
            return
        self.start_worker("upgrade")

    def start_worker(self, action: str) -> None:
        """创建并启动后台线程。"""
        if self.serial_port is None or (not self.serial_port.is_open):
            QMessageBox.warning(self, "串口未连接", "请先连接串口。")
            return

        try:
            handshake_window = float(self.handshake_edit.text().strip())
        except ValueError:
            QMessageBox.warning(self, "参数错误", "等待窗口格式不正确。")
            return

        self.progress_bar.setValue(0)
        self.append_log("----------------------------------------")
        self.append_log(f"开始任务: {action}")

        self.worker_thread = QThread(self)
        self.worker = OtaWorker(
            serial_port=self.serial_port,
            bin_path=self.file_edit.text().strip(),
            target_slot=int(self.slot_combo.currentData()),
            image_version=int(self.version_spin.value()),
            handshake_window=handshake_window,
            action=action,
        )
        self.worker.moveToThread(self.worker_thread)
        self.worker_thread.started.connect(self.worker.run)
        self.worker.log.connect(self.append_log)
        self.worker.progress.connect(self.progress_bar.setValue)
        self.worker.info.connect(self.update_device_info)
        self.worker.finished.connect(self.on_task_finished)
        self.worker.finished.connect(self.worker_thread.quit)
        self.worker_thread.finished.connect(self.worker_thread.deleteLater)
        self.worker_thread.start()

        self.update_connection_state(True)

    @Slot(dict)
    def update_device_info(self, info: dict) -> None:
        """刷新界面上的设备状态显示。"""
        self.mode_label.setText(str(info.get("mode", "-")))
        self.active_slot_label.setText(str(info.get("active_slot", "-")))
        self.switch_slot_label.setText(str(info.get("switch_slot", "-")))
        self.app1_valid_label.setText(str(info.get("app1_valid", "-")))
        self.app2_valid_label.setText(str(info.get("app2_valid", "-")))
        self.upgrade_flag_label.setText(str(info.get("upgrade_flag", "-")))

    @Slot(bool, str)
    def on_task_finished(self, success: bool, message: str) -> None:
        """收尾后台任务并恢复按钮状态。"""
        if success:
            self.append_log(f"任务完成: {message}")
            QMessageBox.information(self, "执行完成", message)
        else:
            self.append_log(f"任务失败: {message}")
            QMessageBox.critical(self, "执行失败", message)

        self.worker = None
        self.worker_thread = None
        self.update_connection_state(self.serial_port is not None and self.serial_port.is_open)

    @Slot()
    def cancel_task(self) -> None:
        """请求中止后台任务。"""
        if self.worker is not None:
            self.worker.cancel()
            self.append_log("已请求中止，等待设备响应")

    @Slot(str)
    def append_log(self, text: str) -> None:
        """追加日志到界面。"""
        self.log_edit.appendPlainText(text)

    def closeEvent(self, event) -> None:
        """关闭窗口时同步关闭串口。"""
        if self.worker is not None:
            QMessageBox.warning(self, "任务进行中", "请先等待当前任务结束。")
            event.ignore()
            return

        if self.serial_port is not None:
            try:
                self.serial_port.close()
            except Exception:
                pass
            self.serial_port = None
        super().closeEvent(event)



def main() -> int:
    """程序入口。"""
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
