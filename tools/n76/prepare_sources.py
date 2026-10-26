"""Prepare pinned official N76 sources plus a reviewable local safety overlay.

This module never runs a compiler, installs a module, binds an I2C device, or
accesses hardware. Keep it beside sources.json and ugreenctl_n76_identity.h in
the repository and in the installed share/ugreenctl/n76 resource directory.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import stat
from urllib.request import Request, urlopen


RESOURCE_DIR = Path(__file__).resolve().parent
TRIGGERS = ("ledtrig-normal-n76e003.c", "ledtrig-breath-n76e003.c")
MAX_SOURCE_BYTES = 256 * 1024


class PreparationError(ValueError):
    """Pinned input or preparation invariant failed; do not compile output."""


def load_manifest() -> dict:
    manifest = json.loads((RESOURCE_DIR / "sources.json").read_text(encoding="utf-8"))
    if manifest.get("schema_version") != 1:
        raise PreparationError("Unsupported N76 source manifest schema")
    if not re.fullmatch(r"[0-9a-f]{40}", manifest.get("commit", "")):
        raise PreparationError("Invalid pinned source commit")
    for filename, source in manifest["sources"].items():
        if (Path(filename).name != filename or not filename.endswith(".c")
                or not re.fullmatch(r"[0-9a-f]{64}", source.get("sha256", ""))
                or not re.fullmatch(r"drivers/ugreen/[a-z0-9/-]+[.]c", source.get("path", ""))):
            raise PreparationError("Invalid pinned N76 source entry")
    return manifest


def source_plan(model: str) -> dict:
    """Return metadata only; no downloads, directory creation, or mutations."""
    manifest = load_manifest()
    if model not in manifest["models"]:
        raise PreparationError("N76 preparation requires exact DMI DH2600 or DXP480T Plus")
    driver = manifest["models"][model]["driver"]
    base = ("https://raw.githubusercontent.com/ugreen-opensource/kernel-6.12/"
            + manifest["commit"] + "/")
    sources = [dict(filename=name, url=base + manifest["sources"][name]["path"],
                    sha256=manifest["sources"][name]["sha256"])
               for name in (driver, *TRIGGERS)]
    return {"model": model, "upstream_commit": manifest["commit"],
            "patch_revision": manifest["patch_revision"], "sources": sources,
            "modules": [name[:-2] + ".ko" for name in (driver, *TRIGGERS)]}


def _replace_once(text: str, before: str, after: str) -> str:
    if text.count(before) != 1:
        raise PreparationError("Pinned source patch anchor mismatch: " + repr(before[:70]))
    return text.replace(before, after, 1)


def _replace_section(text: str, start: str, end: str, replacement: str) -> str:
    if text.count(start) != 1 or text.count(end) != 1:
        raise PreparationError("Pinned source section mismatch: " + start)
    first, last = text.index(start), text.index(end)
    if first >= last:
        raise PreparationError("Pinned source sections are out of order")
    return text[:first] + replacement.rstrip() + "\n\n" + text[last:]


def _patch_driver(text: str, model: str, settings: dict) -> str:
    text = _replace_once(text, "#include <linux/dmi.h>",
                         '#include <linux/dmi.h>\n#include <linux/version.h>\n'
                         '#define UGREENCTL_N76_MODEL ' + json.dumps(model))
    text = _replace_once(text,
                         "static const unsigned short normal_i2c[] = { I2C_ADDR, I2C_ADDR2, I2C_CLIENT_END };",
                         "/* Explicit device binding only: no adapter/address scan. */\n"
                         "static DEFINE_MUTEX(mcu_bind_lock);\n"
                         "static struct i2c_client *mcu_bound_client;")
    text = _replace_once(text, "\tstruct mutex update_lock;",
                         "\tstruct mutex update_lock;\n\tbool stopping;\n\tint registered_leds;")
    text = _replace_section(text, "/* in most cases, should be called while holding */",
                            "static inline int smbus_write8", '#include "ugreenctl_n76_identity.h"')
    text = _replace_once(text, "\tu8 value1;", "\tint value1;")
    text = _replace_once(text,
                         "\tvalue1 = i2c_smbus_read_byte_data(client, reg);",
                         "\tvalue1 = i2c_smbus_read_byte_data(client, reg);\n"
                         "\tif (value1 < 0)\n\t\treturn value1;")
    text = _replace_once(text, "\tunsigned char mode = -1;", "\tint mode = -1;")
    text = _replace_once(text,
                         "\tstruct mcu_led *led = container_of(led_cdev, struct mcu_led, cdev);\n\n#ifndef USE_WORKQUE",
                         "\tstruct mcu_led *led = container_of(led_cdev, struct mcu_led, cdev);\n\n"
                         "\tif (READ_ONCE(((struct mcu_data *)i2c_get_clientdata(led->i2c_client))->stopping))\n"
                         "\t\treturn;\n\n#ifndef USE_WORKQUE")
    init = r'''static int mcu_init(struct i2c_client *client, struct mcu_data *data)
{
	int i;
	int ret = 0;

	mutex_lock(&data->update_lock);
	for (i = 0; i < @MODE_COUNT@; i++) {
		ret = smbus_write8(client, LED_MODE_BASE_REG + i, MODE_NONE);
		if (ret < 0)
			goto out;
	}
	ret = smbus_write8(client, DATA_CLS_REG, @ORANGE_MASK@);
	if (ret < 0)
		goto out;
	ret = smbus_write8(client, DATA_SET_REG, 0x01);
out:
	mutex_unlock(&data->update_lock);
	return ret;
}'''.replace("@MODE_COUNT@", str(settings["mode_register_count"]))
    init = init.replace("@ORANGE_MASK@", hex(settings["orange_clear_mask"]))
    text = _replace_section(text, "static void mcu_init(", "static int mcu_led_probe(", init)
    probe = r'''static int mcu_led_probe(struct i2c_client *client)
{
	struct mcu_data *data;
	int i;
	int ret;

	mutex_lock(&mcu_bind_lock);
	if (mcu_bound_client) {
		ret = -EBUSY;
		goto unlock;
	}
	/* Probe must validate identity itself: manual binding skips detect(). */
	ret = mcu_verify_identity(client);
	if (ret < 0)
		goto unlock;
	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto unlock;
	}
	data->leds = leds;
	data->i2c_client = client;
	mutex_init(&data->update_lock);
	i2c_set_clientdata(client, data);
	ret = mcu_init(client, data);
	if (ret < 0)
		goto unlock;
	for (i = 0; i < ARRAY_SIZE(leds); i++) {
		leds[i].cdev.name = leds[i].name;
		leds[i].cdev.brightness_set = mcu_led_set;
		leds[i].cdev.brightness = LED_FULL;
		leds[i].cdev.blink_set = mcu_led_set_blink;
		leds[i].cdev.groups = color_groups;
		leds[i].cdev.max_brightness = 254;
		leds[i].color = COLOR_WHITE;
		leds[i].delay_on = 1;
		leds[i].delay_off = 1;
		leds[i].brightness = @INITIAL_BRIGHTNESS@;
		leds[i].i2c_client = client;
		INIT_WORK(&leds[i].work, mcu_work);
		ret = led_classdev_register(&client->dev, &leds[i].cdev);
		if (ret < 0)
			goto unregister;
		data->registered_leds++;
	}
	mcu_bound_client = client;
	dev_info(&client->dev, "MCU LED device enabled\n");
	ret = 0;
	goto unlock;
unregister:
	WRITE_ONCE(data->stopping, true);
	while (data->registered_leds > 0) {
		i = --data->registered_leds;
		led_classdev_unregister(&leds[i].cdev);
		cancel_work_sync(&leds[i].work);
	}
unlock:
	mutex_unlock(&mcu_bind_lock);
	return ret;
}'''.replace("@INITIAL_BRIGHTNESS@", settings["initial_brightness"])
    text = _replace_section(text, "static int mcu_led_probe(",
                            "/* Return 0 if detection is successful, -ENODEV otherwise */", probe)
    text = _replace_section(text, "/* Return 0 if detection is successful, -ENODEV otherwise */",
                            "static void mcu_remove(", "/* No legacy I2C auto-detection or broad bus scanning. */")
    remove = r'''static void mcu_cleanup(struct i2c_client *client)
{
	struct mcu_data *data = i2c_get_clientdata(client);
	int i;

	mutex_lock(&mcu_bind_lock);
	if (mcu_bound_client != client)
		goto out;
	WRITE_ONCE(data->stopping, true);
	while (data->registered_leds > 0) {
		i = --data->registered_leds;
		led_classdev_unregister(&data->leds[i].cdev);
		cancel_work_sync(&data->leds[i].work);
	}
	mcu_bound_client = NULL;
out:
	mutex_unlock(&mcu_bind_lock);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
static int mcu_remove(struct i2c_client *client)
{
	mcu_cleanup(client);
	return 0;
}
#else
static void mcu_remove(struct i2c_client *client)
{
	mcu_cleanup(client);
}
#endif

static void mcu_shutdown(struct i2c_client *client)
{
	mcu_cleanup(client);
}'''
    text = _replace_section(text, "static void mcu_remove(", "static const struct i2c_device_id mcu_id[]", remove)
    text = _replace_once(text, "\t.class\t\t= I2C_CLASS_HWMON,\n", "")
    text = _replace_once(text, "\t.probe\t\t= mcu_led_probe,",
                         "#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)\n"
                         "\t.probe_new\t= mcu_led_probe,\n#else\n"
                         "\t.probe\t\t= mcu_led_probe,\n#endif")
    text = _replace_once(text, "\t.detect = mcu_detect,\n\t.address_list = normal_i2c,\n", "")
    return text


def _patch_trigger(text: str, model: str, filename: str) -> str:
    trigger = "normal" if "normal" in filename else "breath"
    text = _replace_once(text, "#include <linux/dmi.h>",
                         "#include <linux/dmi.h>\n#include <linux/string.h>")
    before = ("\tled_trigger_register(&" + trigger + "_led_trigger);\n\treturn 0;")
    after = ("\tconst char *product = dmi_get_system_info(DMI_PRODUCT_NAME);\n\n"
             "\tif (!product || strcmp(product, " + json.dumps(model) + "))\n"
             "\t\treturn -ENODEV;\n\treturn led_trigger_register(&" + trigger + "_led_trigger);")
    return _replace_once(text, before, after)


def _reject_symlink_components(path: Path) -> None:
    """Do this on lexical paths: resolve() alone would hide a cache symlink."""
    for component in (path, *path.parents):
        try:
            info = component.lstat()
        except FileNotFoundError:
            continue
        if (stat.S_ISLNK(info.st_mode)
                or getattr(info, "st_file_attributes", 0)
                & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)):
            raise PreparationError("Refusing symlink/reparse path: " + str(component))


def _validate_cache_directory(cache_dir: Path) -> bool:
    _reject_symlink_components(cache_dir)
    try:
        info = cache_dir.lstat()
    except FileNotFoundError:
        return False
    if not stat.S_ISDIR(info.st_mode):
        raise PreparationError("Source cache is not a directory: " + str(cache_dir))
    if os.name == "posix" and (info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) & 0o077):
        raise PreparationError("Source cache must be owned by this user with private 0700 permissions: " + str(cache_dir))
    return True


def _file_signature(info) -> tuple:
    # CPython on Windows can report creation time in lstat().st_ctime_ns but
    # metadata-change time in fstat().st_ctime_ns for the same unchanged file.
    # Birth time agrees across those APIs; POSIX keeps its full ctime invariant.
    change_time = (getattr(info, "st_birthtime_ns", info.st_ctime_ns)
                   if os.name == "nt" else info.st_ctime_ns)
    return (info.st_dev, info.st_ino, info.st_mode, info.st_nlink, info.st_size,
            info.st_mtime_ns, change_time, info.st_uid)


def _cached_bytes(target: Path) -> bytes | None:
    """Read only a bounded, unchanged regular file; FIFOs are rejected pre-open."""
    try:
        before = target.lstat()
    except FileNotFoundError:
        return None
    if (not stat.S_ISREG(before.st_mode) or before.st_nlink != 1
            or before.st_size > MAX_SOURCE_BYTES):
        raise PreparationError("Source cache entry must be a small regular file with one link: " + str(target))
    if os.name == "posix" and before.st_uid != os.getuid():
        raise PreparationError("Source cache entry belongs to another user: " + str(target))
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    flags |= getattr(os, "O_BINARY", 0) | getattr(os, "O_CLOEXEC", 0)
    descriptor = os.open(target, flags)
    try:
        opened = os.fstat(descriptor)
        if _file_signature(opened) != _file_signature(before):
            raise PreparationError("Source cache entry changed before read: " + str(target))
        data = bytearray()
        while len(data) <= MAX_SOURCE_BYTES:
            chunk = os.read(descriptor, min(65536, MAX_SOURCE_BYTES + 1 - len(data)))
            if not chunk:
                break
            data.extend(chunk)
        after = os.fstat(descriptor)
        if (_file_signature(after) != _file_signature(before)
                or after.st_ctime_ns != opened.st_ctime_ns
                or _file_signature(target.lstat()) != _file_signature(before)):
            raise PreparationError("Source cache entry changed during read: " + str(target))
    finally:
        os.close(descriptor)
    if len(data) != before.st_size or len(data) > MAX_SOURCE_BYTES:
        raise PreparationError("Source cache entry size changed during read: " + str(target))
    return bytes(data)


def _read_pinned(source: dict, cache_dir: Path, allow_download: bool) -> bytes:
    _validate_cache_directory(cache_dir)
    target = cache_dir / source["filename"]
    cached = _cached_bytes(target)
    if cached is not None:
        data = cached
    elif allow_download:
        request = Request(source["url"], headers={"User-Agent": "ugreenctl-n76-source-preparation/1"})
        with urlopen(request, timeout=30) as response:
            if response.geturl() != source["url"]:
                raise PreparationError("Refusing redirected N76 source URL")
            data = response.read(MAX_SOURCE_BYTES + 1)
    else:
        raise PreparationError("Pinned source missing in offline cache: " + str(target))
    if len(data) > MAX_SOURCE_BYTES or hashlib.sha256(data).hexdigest() != source["sha256"]:
        raise PreparationError("SHA-256 mismatch for pinned source: " + str(target))
    if cached is None:
        cache_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
        _validate_cache_directory(cache_dir)
        # Exclusive creation: do not overwrite original inputs or another run's cache.
        try:
            flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
            flags |= getattr(os, "O_BINARY", 0) | getattr(os, "O_CLOEXEC", 0)
            with os.fdopen(os.open(target, flags, 0o600), "wb") as stream:
                stream.write(data)
        except FileExistsError:
            if _cached_bytes(target) != data:
                raise PreparationError("Source cache changed concurrently: " + str(target))
    _validate_cache_directory(cache_dir)
    return data


def prepare_sources(model: str, cache_dir: Path, output_dir: Path, *, allow_download: bool = True) -> dict:
    """Verify/download cache inputs, then prepare one model in an empty directory.

    On failure, any new cache originals remain intact for diagnosis. The source
    directory is written only after every input has passed its hash and every
    deterministic patch has succeeded. No upstream sources belong in Git.
    """
    plan = source_plan(model)
    manifest = load_manifest()
    settings = manifest["models"][model]
    cache_dir, output_dir = Path(cache_dir).absolute(), Path(output_dir).absolute()
    _reject_symlink_components(cache_dir)
    _reject_symlink_components(output_dir)
    cache_dir, output_dir = cache_dir.resolve(), output_dir.resolve()
    _validate_cache_directory(cache_dir)
    if (output_dir == cache_dir or output_dir in cache_dir.parents or cache_dir in output_dir.parents):
        raise PreparationError("Source output and original-source cache must be separate directories")
    if output_dir.exists() and (not output_dir.is_dir() or any(output_dir.iterdir())):
        raise PreparationError("Source output directory must be empty: " + str(output_dir))
    generated = {}
    for source in plan["sources"]:
        original = _read_pinned(source, cache_dir, allow_download).decode("utf-8")
        if source["filename"] == settings["driver"]:
            patched = _patch_driver(original, model, settings)
        else:
            patched = _patch_trigger(original, model, source["filename"])
        generated[source["filename"]] = patched.encode("utf-8")
    generated["ugreenctl_n76_identity.h"] = (RESOURCE_DIR / "ugreenctl_n76_identity.h").read_bytes()
    kbuild = ("# Generated by ugreenctl N76 source preparation; build only.\n"
              "ifeq ($(filter y m,$(CONFIG_I2C)),)\n$(error CONFIG_I2C is required)\nendif\n"
              "ifeq ($(filter y m,$(CONFIG_LEDS_CLASS)),)\n$(error CONFIG_LEDS_CLASS is required)\nendif\n"
              "ifeq ($(CONFIG_LEDS_TRIGGERS),)\n$(error CONFIG_LEDS_TRIGGERS is required)\nendif\n"
              + "".join("obj-m += " + module[:-3] + ".o\n" for module in plan["modules"]))
    generated["Kbuild"] = kbuild.encode("ascii")
    result = dict(plan, generated_sources={name: hashlib.sha256(data).hexdigest()
                                          for name, data in generated.items()})
    generated["preparation.json"] = (json.dumps(result, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    # Downloads may have taken time: recheck lexical output before creating it.
    _reject_symlink_components(output_dir)
    if output_dir.exists() and (not output_dir.is_dir() or any(output_dir.iterdir())):
        raise PreparationError("Source output changed during preparation: " + str(output_dir))
    output_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
    _reject_symlink_components(output_dir)
    for name, data in generated.items():
        with (output_dir / name).open("xb") as stream:
            stream.write(data)
    return result
