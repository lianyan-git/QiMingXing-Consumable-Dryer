#!/usr/bin/env python3

import argparse
import copy
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PROJECT = REPO_ROOT / "project" / "MDK(V5)" / "Project.uvprojx"
OPTIONS = REPO_ROOT / "project" / "MDK(V5)" / "Project.uvoptx"

COMMON_SOURCES = (
    ("System", "system_stm32f10x.c", 1, r"..\..\libraries\CMSIS\CM3\DeviceSupport\ST\STM32F10x\system_stm32f10x.c"),
    ("Startup", "startup_stm32f10x_md.s", 2, r"..\..\libraries\CMSIS\CM3\DeviceSupport\ST\STM32F10x\startup\arm\startup_stm32f10x_md.s"),
    ("Board", "board.c", 1, r"..\..\board\board.c"),
    ("Module", "system_time.c", 1, r"..\..\module\system_time.c"),
    ("Module", "stm32f10x_it.c", 1, r"..\..\module\stm32f10x_it.c"),
    ("Shared", "ota_contract.c", 1, r"..\..\shared\ota_contract.c"),
    ("BSP", "bsp_spi1_bus.c", 1, r"..\..\bsp\bsp_spi1_bus.c"),
    ("BSP", "bsp_w25q128.c", 1, r"..\..\bsp\bsp_w25q128.c"),
    ("BSP", "bsp_tft_st7789.c", 1, r"..\..\bsp\bsp_tft_st7789.c"),
    ("BSP", "bsp_tft_port_stm32.c", 1, r"..\..\bsp\bsp_tft_port_stm32.c"),
    ("Module", "ota_display.c", 1, r"..\..\module\ota_display.c"),
    ("Module", "ota_metadata_store.c", 1, r"..\..\module\ota_metadata_store.c"),
    ("Module", "ota_boot_request_store.c", 1, r"..\..\module\ota_boot_request_store.c"),
    ("BSP", "bsp_internal_flash_stm32.c", 1, r"..\..\bsp\bsp_internal_flash_stm32.c"),
    ("StdPeriph", "misc.c", 1, r"..\..\libraries\STM32F10x_StdPeriph_Driver\src\misc.c"),
    ("StdPeriph", "stm32f10x_rcc.c", 1, r"..\..\libraries\STM32F10x_StdPeriph_Driver\src\stm32f10x_rcc.c"),
    ("StdPeriph", "stm32f10x_gpio.c", 1, r"..\..\libraries\STM32F10x_StdPeriph_Driver\src\stm32f10x_gpio.c"),
    ("StdPeriph", "stm32f10x_iwdg.c", 1, r"..\..\libraries\STM32F10x_StdPeriph_Driver\src\stm32f10x_iwdg.c"),
    ("StdPeriph", "stm32f10x_spi.c", 1, r"..\..\libraries\STM32F10x_StdPeriph_Driver\src\stm32f10x_spi.c"),
    ("StdPeriph", "stm32f10x_flash.c", 1, r"..\..\libraries\STM32F10x_StdPeriph_Driver\src\stm32f10x_flash.c"),
)

TARGETS = {
    "Bootloader": {
        "start": "0x08000000",
        "size": "0x00003000",
        "output": "dryer_bootloader",
        "entry_name": "bl_main.c",
        "entry_path": r"..\..\bootloader\bl_main.c",
        "defines": "STM32F10X_MD,USE_STDPERIPH_DRIVER,BOOTLOADER_BUILD",
        "sources": (
            ("Bootloader", "bl_esp01s.c", 1, r"..\..\bootloader\bl_esp01s.c"),
            ("Bootloader", "bl_tft.c", 1, r"..\..\bootloader\bl_tft.c"),
            ("Bootloader", "flash_ops.c", 1, r"..\..\bootloader\flash_ops.c"),
            ("Bootloader", "upgrade_flag.c", 1, r"..\..\bootloader\upgrade_flag.c"),
            ("Module", "ota_staging.c", 1, r"..\..\module\ota_staging.c"),
            ("BSP", "bsp_esp_uart_stm32.c", 1, r"..\..\bsp\bsp_esp_uart_stm32.c"),
        ),
    },
    "APP": {
        "start": "0x08003400",
        "size": "0x0001CC00",
        "output": "dryer_app",
        "entry_name": "main.c",
        "entry_path": r"..\..\app\main.c",
        "defines": "STM32F10X_MD,USE_STDPERIPH_DRIVER,VECT_TAB_OFFSET=0x3400",
        "sources": (
            ("App", "system_config.c", 1, r"..\..\app\system_config.c"),
            ("App", "ui_manager.c", 1, r"..\..\app\ui_manager.c"),
            ("App", "ui_comp.c", 1, r"..\..\app\ui_comp.c"),
            ("BSP", "bsp_esp_uart_stm32.c", 1, r"..\..\bsp\bsp_esp_uart_stm32.c"),
            ("BSP", "bsp_aht20.c", 1, r"..\..\bsp\bsp_aht20.c"),
            ("BSP", "bsp_buzzer.c", 1, r"..\..\bsp\bsp_buzzer.c"),
            ("BSP", "bsp_cs1237.c", 1, r"..\..\bsp\bsp_cs1237.c"),
            ("BSP", "bsp_encoder.c", 1, r"..\..\bsp\bsp_encoder.c"),
            ("BSP", "bsp_fan.c", 1, r"..\..\bsp\bsp_fan.c"),
            ("BSP", "bsp_ntc.c", 1, r"..\..\bsp\bsp_ntc.c"),
            ("BSP", "bsp_ptc.c", 1, r"..\..\bsp\bsp_ptc.c"),
            ("BSP", "bsp_rgb_led.c", 1, r"..\..\bsp\bsp_rgb_led.c"),
            ("BSP", "bsp_stepper.c", 1, r"..\..\bsp\bsp_stepper.c"),
            ("BSP", "bsp_system_reset_stm32.c", 1, r"..\..\bsp\bsp_system_reset_stm32.c"),
            ("Module", "esp_at.c", 1, r"..\..\module\esp_at.c"),
            ("Module", "http_server.c", 1, r"..\..\module\http_server.c"),
            ("Module", "esp_http_bridge.c", 1, r"..\..\module\esp_http_bridge.c"),
            ("Module", "ota_upload.c", 1, r"..\..\module\ota_upload.c"),
            ("Module", "ota_http.c", 1, r"..\..\module\ota_http.c"),
            ("Module", "ota_staging.c", 1, r"..\..\module\ota_staging.c"),
            ("Module", "ota_update_controller.c", 1, r"..\..\module\ota_update_controller.c"),
            ("Module", "mod_ota.c", 1, r"..\..\module\mod_ota.c"),
            ("Module", "mod_web_server.c", 1, r"..\..\module\mod_web_server.c"),
            ("Module", "mod_wifi_config.c", 1, r"..\..\module\mod_wifi_config.c"),
            ("Module", "mod_wifi_manager.c", 1, r"..\..\module\mod_wifi_manager.c"),
            ("StdPeriph", "stm32f10x_adc.c", 1, r"..\..\libraries\STM32F10x_StdPeriph_Driver\src\stm32f10x_adc.c"),
            ("StdPeriph", "stm32f10x_bkp.c", 1, r"..\..\libraries\STM32F10x_StdPeriph_Driver\src\stm32f10x_bkp.c"),
            ("StdPeriph", "stm32f10x_can.c", 1, r"..\..\libraries\STM32F10x_StdPeriph_Driver\src\stm32f10x_can.c"),
            ("StdPeriph", "stm32f10x_dma.c", 1, r"..\..\libraries\STM32F10x_StdPeriph_Driver\src\stm32f10x_dma.c"),
            ("StdPeriph", "stm32f10x_exti.c", 1, r"..\..\libraries\STM32F10x_StdPeriph_Driver\src\stm32f10x_exti.c"),
            ("StdPeriph", "stm32f10x_i2c.c", 1, r"..\..\libraries\STM32F10x_StdPeriph_Driver\src\stm32f10x_i2c.c"),
            ("StdPeriph", "stm32f10x_pwr.c", 1, r"..\..\libraries\STM32F10x_StdPeriph_Driver\src\stm32f10x_pwr.c"),
            ("StdPeriph", "stm32f10x_rtc.c", 1, r"..\..\libraries\STM32F10x_StdPeriph_Driver\src\stm32f10x_rtc.c"),
            ("StdPeriph", "stm32f10x_tim.c", 1, r"..\..\libraries\STM32F10x_StdPeriph_Driver\src\stm32f10x_tim.c"),
            ("StdPeriph", "stm32f10x_usart.c", 1, r"..\..\libraries\STM32F10x_StdPeriph_Driver\src\stm32f10x_usart.c"),
        ),
    },
}


def require(element, path):
    found = element.find(path)
    if found is None:
        raise RuntimeError(f"missing XML element: {path}")
    return found


def set_text(element, path, value):
    require(element, path).text = value


def replace_groups(target, config):
    groups = require(target, "./Groups")
    groups.clear()
    sources = list(COMMON_SOURCES)
    sources.insert(1, ("Entry", config["entry_name"], 1, config["entry_path"]))
    sources.extend(config.get("sources", ()))

    by_group = {}
    for group_name, file_name, file_type, file_path in sources:
        by_group.setdefault(group_name, []).append((file_name, file_type, file_path))

    for group_name, files in by_group.items():
        group = ET.SubElement(groups, "Group")
        ET.SubElement(group, "GroupName").text = group_name
        file_list = ET.SubElement(group, "Files")
        for file_name, file_type, file_path in files:
            file_element = ET.SubElement(file_list, "File")
            ET.SubElement(file_element, "FileName").text = file_name
            ET.SubElement(file_element, "FileType").text = str(file_type)
            ET.SubElement(file_element, "FilePath").text = file_path


def configure_project():
    tree = ET.parse(PROJECT)
    root = tree.getroot()
    targets = require(root, "./Targets")
    template = require(targets, "./Target")
    targets.clear()

    for name, config in TARGETS.items():
        target = copy.deepcopy(template)
        set_text(target, "./TargetName", name)
        set_text(target, ".//Cpu", f'IRAM(0x20000000,0x00005000) IROM({config["start"]},{config["size"]}) CPUTYPE("Cortex-M3") CLOCK(12000000) ELITTLE')
        set_text(target, ".//Define", config["defines"])
        flash_driver = require(target, ".//FlashDriverDll")
        flash_driver.text = flash_driver.text.replace("-FL010000", "-FL020000")
        set_text(target, ".//TargetStatus/InvalidFlash", "0")
        set_text(target, ".//OutputDirectory", f'.\\Objects\\{name}\\')
        set_text(target, ".//OutputName", config["output"])
        set_text(target, ".//CreateHexFile", "1")
        set_text(target, ".//ListingPath", f'.\\Listings\\{name}\\')
        set_text(target, ".//AfterMake/RunUserProg1", "1")
        set_text(target, ".//AfterMake/UserProg1Name", f'fromelf.exe --bin --output ".\\Objects\\{name}\\{config["output"]}.bin" ".\\Objects\\{name}\\{config["output"]}.axf"')
        set_text(target, ".//AfterMake/nStopA1X", "1")
        set_text(target, ".//SelectedForBatchBuild", "1")
        set_text(target, ".//OnChipMemories/IROM/StartAddress", config["start"])
        set_text(target, ".//OnChipMemories/IROM/Size", config["size"])
        set_text(target, ".//OnChipMemories/OCR_RVCT4/StartAddress", config["start"])
        set_text(target, ".//OnChipMemories/OCR_RVCT4/Size", config["size"])
        set_text(target, ".//LDads/TextAddressRange", config["start"])
        set_text(target, ".//ArmAdsMisc/AdsLmap", "1")
        replace_groups(target, config)
        targets.append(target)

    ET.indent(tree, space="  ")
    tree.write(PROJECT, encoding="UTF-8", xml_declaration=True)


def configure_options():
    tree = ET.parse(OPTIONS)
    root = tree.getroot()
    existing = root.findall("./Target")
    if not existing:
        raise RuntimeError("Project.uvoptx has no Target template")
    template = existing[0]
    insertion_index = list(root).index(template)
    for target in existing:
        root.remove(target)

    for offset, (name, config) in enumerate(TARGETS.items()):
        target = copy.deepcopy(template)
        set_text(target, "./TargetName", name)
        set_text(target, ".//OPTLEX/ListingPath", f'.\\Listings\\{name}\\')
        set_text(target, ".//OPTFL/IsCurrentTarget", "1" if name == "APP" else "0")
        for registry_name in target.findall(".//TargetDriverDllRegistry/SetRegEntry/Name"):
            if registry_name.text:
                registry_name.text = registry_name.text.replace("-FL010000", "-FL020000")
        root.insert(insertion_index + offset, target)

    ET.indent(tree, space="  ")
    tree.write(OPTIONS, encoding="UTF-8", xml_declaration=True)


def normalized_hex(value):
    return int(value, 16)


def validate_project():
    errors = []
    root = ET.parse(PROJECT).getroot()
    targets = root.findall("./Targets/Target")
    actual_names = [require(target, "./TargetName").text for target in targets]
    if actual_names != list(TARGETS):
        errors.append(f"target names/order are {actual_names!r}")

    for target in targets:
        name = require(target, "./TargetName").text
        if name not in TARGETS:
            continue
        config = TARGETS[name]
        expected_sources = ({entry[3] for entry in COMMON_SOURCES} |
                            {config["entry_path"]} |
                            {entry[3] for entry in config.get("sources", ())})
        actual_sources = {item.text for item in target.findall("./Groups/Group/Files/File/FilePath")}
        if actual_sources != expected_sources:
            errors.append(f"{name} source set differs: {sorted(actual_sources ^ expected_sources)}")
        for source in actual_sources:
            source_path = (PROJECT.parent / source.replace("\\", "/")).resolve()
            if not source_path.is_file():
                errors.append(f"{name} references missing source {source}")

        checks = {
            ".//OnChipMemories/IROM/StartAddress": normalized_hex(config["start"]),
            ".//OnChipMemories/IROM/Size": normalized_hex(config["size"]),
            ".//OnChipMemories/OCR_RVCT4/StartAddress": normalized_hex(config["start"]),
            ".//OnChipMemories/OCR_RVCT4/Size": normalized_hex(config["size"]),
        }
        for path, expected in checks.items():
            actual = require(target, path).text
            if normalized_hex(actual) != expected:
                errors.append(f"{name} {path} is {actual}, expected 0x{expected:X}")

        scalar_checks = {
            ".//OutputDirectory": f'.\\Objects\\{name}\\',
            ".//OutputName": config["output"],
            ".//CreateHexFile": "1",
            ".//ListingPath": f'.\\Listings\\{name}\\',
            ".//AfterMake/RunUserProg1": "1",
            ".//SelectedForBatchBuild": "1",
            ".//ArmAdsMisc/AdsLmap": "1",
        }
        for path, expected in scalar_checks.items():
            actual = require(target, path).text or ""
            if actual != expected:
                errors.append(f"{name} {path} is {actual!r}, expected {expected!r}")

        command = require(target, ".//AfterMake/UserProg1Name").text or ""
        if "fromelf.exe --bin" not in command or f'{config["output"]}.bin' not in command:
            errors.append(f"{name} BIN after-build command is invalid")
        flash_driver = require(target, ".//FlashDriverDll").text or ""
        if "-FL020000" not in flash_driver or "-FL010000" in flash_driver:
            errors.append(f"{name} Flash download range is not 128 KiB")

    option_names = [item.text for item in ET.parse(OPTIONS).getroot().findall("./Target/TargetName")]
    if option_names != list(TARGETS):
        errors.append(f"uvoptx targets are {option_names!r}")

    if errors:
        for error in errors:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print("PASS: Keil project has isolated Bootloader and APP targets")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="validate without changing files")
    args = parser.parse_args()
    if args.check:
        return validate_project()
    configure_project()
    configure_options()
    return validate_project()


if __name__ == "__main__":
    raise SystemExit(main())
