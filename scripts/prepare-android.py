#!/usr/bin/env python3
"""Apply the pinned Android integration. Safe to rerun on the same checkout."""
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
source = Path(sys.argv[1]).resolve()
revision = (root / 'Android/cemu-revision.txt').read_text().strip()
actual = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
if actual != revision:
    raise SystemExit(f'Expected Cemu {revision}, got {actual}; use a separate pinned checkout.')
patch = root / 'Android/cemu-hooks.patch'
check = subprocess.run(['git', '-C', str(source), 'apply', '--reverse', '--check', str(patch)], capture_output=True)
if check.returncode:
    subprocess.run(['git', '-C', str(source), 'apply', '--check', str(patch)], check=True)
    subprocess.run(['git', '-C', str(source), 'apply', str(patch)], check=True)

def replace(path, before, after):
    file = source / path
    text = file.read_text()
    if after in text:
        return
    if text.count(before) != 1:
        raise SystemExit(f'Integration anchor changed: {path}: {before!r}')
    file.write_text(text.replace(before, after))

# Before project(), APPLE describes the build host. Choose Android first so
# macOS's dynamic libusb overlay is never used for an Android target.
replace('CMakeLists.txt', 'if(UNIX AND NOT APPLE AND NOT VCPKG_TARGET_ANDROID)',
        'if(VCPKG_TARGET_ANDROID)\n        set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_LIST_DIR}/dependencies/vcpkg_overlay_ports")\n    elseif(UNIX AND NOT APPLE)')

app = 'src/android/app/'
java = app + 'src/main/java/info/cemu/cemu/'
shutil.copy2(root/'Android/overlay/HyruleSession.kt', source/java/'HyruleSession.kt')
shutil.copy2(root/'Android/overlay/HyruleBridge.cpp', source/app/'src/main/cpp/HyruleBridge.cpp')
replace(app+'src/main/cpp/CMakeLists.txt', '        NativeLib.cpp', '        NativeLib.cpp\n        HyruleBridge.cpp')
cmake = source/app/'src/main/cpp/CMakeLists.txt'
text = cmake.read_text()
if '# HYRULE_CLIENT' not in text:
    text += '''
# HYRULE_CLIENT: package the native client alongside Cemu, loaded only when enabled.
target_link_libraries(CemuAndroid PRIVATE dl)
target_link_options(CemuAndroid PRIVATE "-Wl,-z,max-page-size=16384"
    "-Wl,-u,memory_getBase" "-Wl,-u,osLib_registerHLEFunction"
    "-Wl,-u,milkbar_isHLEReady" "-Wl,-u,milkbar_markHooksReady" "-Wl,-u,milkbar_isTitleActive")
'''
    cmake.write_text(text)
replace(java+'MainActivity.kt', '        setContent {', '        HyruleSession.settings(this)\n        setContent {')
replace(java+'emulation/EmulationActivity.kt', '        val gamePath = getGamePath()', '''        if (!info.cemu.cemu.HyruleSession.prepare(this, getGamePath())) { finish(); return }
        val gamePath = getGamePath()''')
replace(app+'build.gradle.kts', 'applicationId = "info.cemu.cemu"', 'applicationId = "app.hyruletogether.android"')
replace(app+'build.gradle.kts', 'ndkVersion = "29.0.14206865"', 'ndkVersion = System.getenv("HYRULE_NDK_VERSION") ?: "27.3.13750724"')
replace(app+'src/main/AndroidManifest.xml', 'android:label="@string/app_name"', 'android:label="Hyrule Together"')
assets = source/app/'src/main/assets/hyrule'
assets.mkdir(parents=True, exist_ok=True)
for name in ('ArmorMapping.txt','WeaponDamages.txt','QuestFlags.txt','QuestFlagsNames.txt'):
    shutil.copy2(root/'WPF .NET 6/Breath of the Wild Multiplayer/AppdataFiles'/name, assets/name)
libs = source/app/'src/main/jniLibs/arm64-v8a'
libs.mkdir(parents=True, exist_ok=True)
shutil.copy2(root/'Build/android-client/libMilkBarClient.so', libs/'libMilkBarClient.so')
print(f'Prepared Hyrule Together Android in {source}')

tests = source/app/'src/androidTest/java/info/cemu/cemu'
tests.mkdir(parents=True, exist_ok=True)
shutil.copy2(root/'Android/overlay/HyruleSessionTest.kt', tests/'HyruleSessionTest.kt')

# NDK 27 lacks jthread; this temporary thread is immediately joined upstream.
replace(app+'src/main/cpp/JNIUtils.h',
        'std::jthread([&]() {\n\t\t\tfunc(GetEnv());\n\t\t});',
        'std::thread([&]() {\n\t\t\tfunc(GetEnv());\n\t\t}).join();')

# Driver-loading helpers are shared libraries too; align all packaged ARM64 ELFs.
cmake = source/app/'src/main/cpp/CMakeLists.txt'
text = cmake.read_text()
if '# HYRULE_DRIVER_ALIGNMENT' not in text:
    text += '\n# HYRULE_DRIVER_ALIGNMENT\nforeach(hook IN ITEMS main_hook file_redirect_hook gsl_alloc_hook hook_impl)\n    target_link_options(${hook} PRIVATE "-Wl,-z,max-page-size=16384")\nendforeach()\n'
    cmake.write_text(text)
