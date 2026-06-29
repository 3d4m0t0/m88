#!/usr/bin/env python3
"""Sync Qt translation JSON files to the en/ja key set and ordering."""

from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
LOCALES = ("en", "ja", "de", "fr", "es", "ko", "zh_CN", "zh_TW")

SNAPSHOT_NOTE = (
    "Settings on this tab are saved in snapshot files and restored when a "
    "snapshot is loaded, replacing the values shown here."
)

# Non-ASCII strings are written with unicode escapes so this file stays ASCII-safe.
EXTRA: dict[str, dict[str, str]] = {
    "Standard settings (&D)": {
        "en": "Standard settings (&D)",
        "ja": "\u6a19\u6e96\u8a2d\u5b9a(&D)",
        "de": "Standard-Einstellungen (&D)",
        "fr": "Param\u00e8tres standard (&D)",
        "es": "Configuraci\u00f3n est\u00e1ndar (&D)",
        "ko": "\ud45c\uc900 \uc124\uc815 (&D)",
        "zh_CN": "\u6807\u51c6\u8bbe\u7f6e (&D)",
        "zh_TW": "\u6a19\u6e96\u8a2d\u5b9a (&D)",
    },
    SNAPSHOT_NOTE: {
        "en": SNAPSHOT_NOTE,
        "ja": (
            "\u3053\u306e\u30bf\u30d6\u306e\u8a2d\u5b9a\u306f\u30b9\u30ca\u30c3\u30d7\u30b7\u30e7\u30c3\u30c8\u306b"
            "\u4fdd\u5b58\u3055\u308c\u307e\u3059\u3002\u30b9\u30ca\u30c3\u30d7\u30b7\u30e7\u30c3\u30c8\u3092\u8aad\u307f\u8fbc\u3080\u3068\u3001"
            "\u3053\u3053\u306b\u8868\u793a\u3055\u308c\u3066\u3044\u308b\u8a2d\u5b9a\u304c\u30b9\u30ca\u30c3\u30d7\u30b7\u30e7\u30c3\u30c8\u306e"
            "\u5185\u5bb9\u3067\u7f6e\u304d\u63db\u3048\u3089\u308c\u307e\u3059\u3002"
        ),
        "de": (
            "Die Einstellungen auf diesem Reiter werden in Snapshots gespeichert und beim "
            "Laden eines Snapshots wiederhergestellt, wobei die hier angezeigten Werte "
            "ersetzt werden."
        ),
        "fr": (
            "Les param\u00e8tres de cet onglet sont enregistr\u00e9s dans les snapshots et "
            "restaur\u00e9s lors du chargement d'un snapshot, rempla\u00e7ant les valeurs "
            "affich\u00e9es ici."
        ),
        "es": (
            "Los ajustes de esta pesta\u00f1a se guardan en instant\u00e1neas y se restauran al "
            "cargar una instant\u00e1nea, sustituyendo los valores mostrados aqu\u00ed."
        ),
        "ko": (
            "\uc774 \ud0ed\uc758 \uc124\uc815\uc740 \uc2a4\ub0b5\uc0f7 \ud30c\uc77c\uc5d0 \uc800\uc7a5\ub418\uba70, "
            "\uc2a4\ub0b5\uc0f7\uc744 \ubd88\ub7ec\uc624\uba74 \uc5ec\uae30\uc5d0 \ud45c\uc2dc\ub41c \uc124\uc815\uc774 "
            "\uc2a4\ub0b5\uc0f7 \ub0b4\uc6a9\uc73c\ub85c \ubc14\ub011\ub2c8\ub2e4."
        ),
        "zh_CN": (
            "\u6b64\u6807\u7b7e\u9875\u4e2d\u7684\u8bbe\u7f6e\u4f1a\u4fdd\u5b58\u5728\u5feb\u7167\u6587\u4ef6\u4e2d\u3002"
            "\u52a0\u8f7d\u5feb\u7167\u65f6\uff0c\u8fd9\u4e9b\u8bbe\u7f6e\u5c06\u88ab\u5feb\u7167\u5185\u5bb9\u66ff\u6362\u3002"
        ),
        "zh_TW": (
            "\u6b64\u6a19\u7c64\u9801\u4e2d\u7684\u8a2d\u5b9a\u6703\u4fdd\u5b58\u5728\u5feb\u7167\u6a94\u6848\u4e2d\u3002"
            "\u8f09\u5165\u5feb\u7167\u6642\uff0c\u9019\u4e9b\u8a2d\u5b9a\u5c07\u88ab\u5feb\u7167\u5167\u5bb9\u53d6\u4ee3\u3002"
        ),
    },
    "Hardware": {
        "en": "Hardware",
        "ja": "\u30cf\u30fc\u30c9\u30a6\u30a7\u30a2",
        "de": "Hardware",
        "fr": "Mat\u00e9riel",
        "es": "Hardware",
        "ko": "\ud558\ub4dc\uc6e8\uc5b4",
        "zh_CN": "\u786c\u4ef6",
        "zh_TW": "\u786c\u9ad4",
    },
    "Display hardware": {
        "en": "Display hardware",
        "ja": "\u8868\u793a\u30cf\u30fc\u30c9\u30a6\u30a7\u30a2",
        "de": "Anzeige-Hardware",
        "fr": "Mat\u00e9riel d'affichage",
        "es": "Hardware de pantalla",
        "ko": "\ub514\uc2a4\ud50c\ub808\uc774 \ud558\ub4dc\uc6e8\uc5b4",
        "zh_CN": "\u663e\u793a\u786c\u4ef6",
        "zh_TW": "\u986f\u793a\u786c\u9ad4",
    },
    "Sound board": {
        "en": "Sound board",
        "ja": "\u30b5\u30a6\u30f3\u30c9\u30dc\u30fc\u30c9",
        "de": "Soundkarte",
        "fr": "Carte son",
        "es": "Placa de sonido",
        "ko": "\uc0ac\uc6b4\ub4dc \ubcf4\ub4dc",
        "zh_CN": "\u58f0\u5361",
        "zh_TW": "\u97f3\u6548\u5361",
    },
    "FM ($44h):": {
        "en": "FM ($44h):",
        "ja": "FM ($44h):",
        "de": "FM ($44h):",
        "fr": "FM ($44h) :",
        "es": "FM ($44h):",
        "ko": "FM ($44h):",
        "zh_CN": "FM ($44h):",
        "zh_TW": "FM ($44h):",
    },
    "FM ($A8h):": {
        "en": "FM ($A8h):",
        "ja": "FM ($A8h):",
        "de": "FM ($A8h):",
        "fr": "FM ($A8h) :",
        "es": "FM ($A8h):",
        "ko": "FM ($A8h):",
        "zh_CN": "FM ($A8h):",
        "zh_TW": "FM ($A8h):",
    },
    "Audio/Video": {
        "en": "Audio/Video",
        "ja": "\u6620\u50cf\u30fb\u97f3\u58f0",
        "de": "Audio/Video",
        "fr": "Audio/Vid\u00e9o",
        "es": "Audio/V\u00eddeo",
        "ko": "\uc601\uc0c1/\uc74c\uc131",
        "zh_CN": "\u5f71\u50cf/\u97f3\u9891",
        "zh_TW": "\u5f71\u50cf/\u97f3\u8a0a",
    },
    "\u30ab\u30caON": {
        "en": "Kana ON",
        "ja": "\u30ab\u30caON",
        "de": "Kana EIN",
        "fr": "Kana ACT.",
        "es": "Kana ACT.",
        "ko": "\uac00\ub098 ON",
        "zh_CN": "\u5047\u540d\u5f00",
        "zh_TW": "\u5047\u540d\u958b",
    },
    "\u30ab\u30caOFF": {
        "en": "Kana OFF",
        "ja": "\u30ab\u30caOFF",
        "de": "Kana AUS",
        "fr": "Kana INACT.",
        "es": "Kana DES.",
        "ko": "\uac00\ub098 OFF",
        "zh_CN": "\u5047\u540d\u5173",
        "zh_TW": "\u5047\u540d\u95dc",
    },
    "Half-width kana via host IME (Settings > Other).": {
        "en": "Half-width kana via host IME (Settings > Other).",
        "ja": "\u30db\u30b9\u30c8 IME \u306b\u3088\u308b\u534a\u89d2\u30ab\u30ca\uff08\u8a2d\u5b9a > \u305d\u306e\u4ed6\uff09\u3002",
        "de": "Halbbreite Kana \u00fcber Host-IME (Einstellungen > Sonstiges).",
        "fr": "Kana demi-chasse via IME h\u00f4te (Param\u00e8tres > Autre).",
        "es": "Kana de ancho medio mediante IME del host (Configuraci\u00f3n > Otro).",
        "ko": "\ud638\uc2a4\ud2b8 IME \ubc18\uac01 \uac00\ub098 (\uc124\uc815 > \uae30\ud0c0).",
        "zh_CN": "\u901a\u8fc7\u4e3b\u673a IME \u8f93\u5165\u534a\u89d2\u5047\u540d\uff08\u8bbe\u7f6e > \u5176\u4ed6\uff09\u3002",
        "zh_TW": "\u900f\u904e\u4e3b\u6a5f IME \u8f38\u5165\u534a\u89d2\u5047\u540d\uff08\u8a2d\u5b9a > \u5176\u4ed6\uff09\u3002",
    },
    "Mode: %1": {
        "en": "Mode: %1",
        "ja": "\u30e2\u30fc\u30c9: %1",
        "de": "Modus: %1",
        "fr": "Mode : %1",
        "es": "Modo: %1",
        "ko": "\ubaa8\ub4dc: %1",
        "zh_CN": "\u6a21\u5f0f: %1",
        "zh_TW": "\u6a21\u5f0f: %1",
    },
    "closed": {
        "en": "closed",
        "ja": "\u7d42\u4e86",
        "de": "geschlossen",
        "fr": "ferm\u00e9",
        "es": "cerrado",
        "ko": "\uc885\ub8cc",
        "zh_CN": "\u5df2\u5173\u95ed",
        "zh_TW": "\u5df2\u95dc\u9589",
    },
    "inactive": {
        "en": "inactive",
        "ja": "\u7121\u52b9",
        "de": "inaktiv",
        "fr": "inactif",
        "es": "inactivo",
        "ko": "\ube44\ud65c\uc131",
        "zh_CN": "\u672a\u6fc0\u6d3b",
        "zh_TW": "\u672a\u555f\u7528",
    },
    "active": {
        "en": "active",
        "ja": "\u6709\u52b9",
        "de": "aktiv",
        "fr": "actif",
        "es": "activo",
        "ko": "\ud65c\uc131",
        "zh_CN": "\u5df2\u6fc0\u6d3b",
        "zh_TW": "\u5df2\u555f\u7528",
    },
    "unknown": {
        "en": "unknown",
        "ja": "\u4e0d\u660e",
        "de": "unbekannt",
        "fr": "inconnu",
        "es": "desconocido",
        "ko": "\uc54c \uc218 \uc5c6\uc74c",
        "zh_CN": "\u672a\u77e5",
        "zh_TW": "\u672a\u77e5",
    },
    "(none)": {
        "en": "(none)",
        "ja": "\uff08\u306a\u3057\uff09",
        "de": "(keine)",
        "fr": "(aucun)",
        "es": "(ninguno)",
        "ko": "(\uc5c6\uc74c)",
        "zh_CN": "\uff08\u65e0\uff09",
        "zh_TW": "\uff08\u7121\uff09",
    },
    "Host IM: %1": {
        "en": "Host IM: %1",
        "ja": "\u30db\u30b9\u30c8 IM: %1",
        "de": "Host-IME: %1",
        "fr": "IME h\u00f4te : %1",
        "es": "IME del host: %1",
        "ko": "\ud638\uc2a4\ud2b8 IM: %1",
        "zh_CN": "\u4e3b\u673a\u8f93\u5165\u6cd5: %1",
        "zh_TW": "\u4e3b\u6a5f\u8f38\u5165\u6cd5: %1",
    },
    "Host IM: %1 (direct)": {
        "en": "Host IM: %1 (direct)",
        "ja": "\u30db\u30b9\u30c8 IM: %1\uff08\u76f4\u63a5\uff09",
        "de": "Host-IME: %1 (direkt)",
        "fr": "IME h\u00f4te : %1 (direct)",
        "es": "IME del host: %1 (directo)",
        "ko": "\ud638\uc2a4\ud2b8 IM: %1 (\uc9c1\uc811)",
        "zh_CN": "\u4e3b\u673a\u8f93\u5165\u6cd5: %1\uff08\u76f4\u63a5\uff09",
        "zh_TW": "\u4e3b\u6a5f\u8f38\u5165\u6cd5: %1\uff08\u76f4\u63a5\uff09",
    },
    "fcitx CurrentInputMethod (global D-Bus)\nIM: %1\nState: %2": {
        "en": "fcitx CurrentInputMethod (global D-Bus)\nIM: %1\nState: %2",
        "ja": "fcitx CurrentInputMethod (global D-Bus)\nIM: %1\n\u72b6\u614b: %2",
        "de": "fcitx CurrentInputMethod (global D-Bus)\nIM: %1\nZustand: %2",
        "fr": "fcitx CurrentInputMethod (D-Bus global)\nIM : %1\n\u00c9tat : %2",
        "es": "fcitx CurrentInputMethod (D-Bus global)\nIM: %1\nEstado: %2",
        "ko": "fcitx CurrentInputMethod (global D-Bus)\nIM: %1\n\uc0c1\ud0dc: %2",
        "zh_CN": "fcitx CurrentInputMethod (global D-Bus)\n\u8f93\u5165\u6cd5: %1\n\u72b6\u6001: %2",
        "zh_TW": "fcitx CurrentInputMethod (global D-Bus)\n\u8f38\u5165\u6cd5: %1\n\u72c0\u614b: %2",
    },
}


def load_entries(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8") as handle:
        data = json.load(handle)
    return data["translations"]


def entry_map(entries: list[dict[str, str]]) -> dict[tuple[str, str], dict[str, str]]:
    return {(entry["context"], entry["source"]): entry for entry in entries}


def merged_key_order(
    en_entries: list[dict[str, str]], ja_entries: list[dict[str, str]]
) -> list[tuple[str, str]]:
    order: list[tuple[str, str]] = []
    seen: set[tuple[str, str]] = set()
    for entries in (ja_entries, en_entries):
        for entry in entries:
            key = (entry["context"], entry["source"])
            if key not in seen:
                order.append(key)
                seen.add(key)
    return order


def translation_for(
    locale: str, source: str, existing: str | None, en_text: str
) -> str:
    if existing is not None:
        return existing
    if source in EXTRA and locale in EXTRA[source]:
        return EXTRA[source][locale]
    if locale == "en":
        return source
    return en_text


def sync_locale(
    locale: str,
    key_order: list[tuple[str, str]],
    locale_map: dict[tuple[str, str], dict[str, str]],
    en_map: dict[tuple[str, str], dict[str, str]],
) -> list[dict[str, str]]:
    synced: list[dict[str, str]] = []
    for context, source in key_order:
        existing = locale_map.get((context, source))
        en_entry = en_map.get((context, source))
        en_text = en_entry["translation"] if en_entry else source
        synced.append(
            {
                "context": context,
                "source": source,
                "translation": translation_for(
                    locale,
                    source,
                    existing["translation"] if existing else None,
                    en_text,
                ),
            }
        )
    return synced


def build_en_map(
    key_order: list[tuple[str, str]],
    en_map: dict[tuple[str, str], dict[str, str]],
) -> dict[tuple[str, str], dict[str, str]]:
    merged = dict(en_map)
    for context, source in key_order:
        key = (context, source)
        if key not in merged:
            merged[key] = {
                "context": context,
                "source": source,
                "translation": EXTRA.get(source, {}).get("en", source),
            }
    return merged


def main() -> None:
    en_entries = load_entries(ROOT / "m88-qt_en.json")
    ja_entries = load_entries(ROOT / "m88-qt_ja.json")
    key_order = merged_key_order(en_entries, ja_entries)
    en_map = build_en_map(key_order, entry_map(en_entries))

    for locale in LOCALES:
        path = ROOT / f"m88-qt_{locale}.json"
        locale_entries = load_entries(path)
        locale_map = entry_map(locale_entries)
        synced = sync_locale(locale, key_order, locale_map, en_map)
        with path.open("w", encoding="utf-8", newline="\n") as handle:
            json.dump({"translations": synced}, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
        print(f"{path.name}: {len(synced)} entries")


if __name__ == "__main__":
    main()
