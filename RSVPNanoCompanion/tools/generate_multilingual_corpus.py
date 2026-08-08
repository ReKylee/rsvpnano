#!/usr/bin/env python3
from __future__ import annotations

import argparse
import io
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "RSVPNanoCompanion" / "testdata" / "multilingual"
TITLE = "Multilingual Reader Corpus"
PARAGRAPHS = (
    ("en", "ltr", "Alice was beginning to get very tired of sitting by her sister on the bank."),
    ("es", "ltr", "En un lugar de la Mancha, de cuyo nombre no quiero acordarme."),
    ("fr", "ltr", "Il y avait en Westphalie, dans le château de monsieur le baron."),
    ("de", "ltr", "Habe nun, ach! Philosophie, Juristerei und Medizin mit heißem Bemühn studiert."),
    ("ro", "ltr", "A fost odată ca-n povești, a fost ca niciodată, din rude mari împărătești."),
    ("pl", "ltr", "Litwo! Ojczyzno moja! ty jesteś jak zdrowie."),
    ("ru", "ltr", "12-го августа, ровно в третий день после дня моего рождения, я проснулся рано."),
    ("he", "rtl", "שָׁלוֹם רָב שׁוּבֵךְ, צִפּוֹרָה נֶחְמֶדֶת, מֵאַרְצוֹת הַחֹם אֶל חַלּוֹנִי."),
    ("ar", "rtl", "بلغني أيها الملك السعيد، ذو الرأي الرشيد، أن 123 حكاية رويت."),
    ("ja", "ltr", "吾輩は猫である。名前はまだ無い。"),
    ("zh-Hans", "ltr", "上古之世，人民少而禽兽众，人民不胜禽兽虫蛇。"),
    ("en", "ltr", "English 123 (עברית 45) العربية 67; 日本語と中文; ∀x∈ℝ, x²≥0 and ∫₀¹x²dx=⅓."),
)


def text_document() -> str:
    return f"# {TITLE}\n\n" + "\n\n".join(text for _locale, _direction, text in PARAGRAPHS) + "\n"


def html_document(xhtml: bool = False) -> str:
    declaration = '<?xml version="1.0" encoding="utf-8"?>\n' if xhtml else "<!doctype html>\n"
    namespace = ' xmlns="http://www.w3.org/1999/xhtml"' if xhtml else ""
    body = "\n".join(
        f'    <p lang="{locale}" dir="{direction}">{escape_html(text)}</p>'
        for locale, direction, text in PARAGRAPHS
    )
    return (
        f'{declaration}<html{namespace} lang="en">\n<head><meta charset="utf-8"/>'
        f"<title>{TITLE}</title></head>\n<body>\n  <h1>{TITLE}</h1>\n{body}\n</body>\n</html>\n"
    )


def rsvp_document() -> str:
    lines = ["@rsvp 1", f"@title {TITLE}", "@source public-domain multilingual excerpts", "", f"@chapter {TITLE}"]
    current_locale = ""
    current_direction = "auto"
    for index, (locale, direction, text) in enumerate(PARAGRAPHS):
        if index:
            lines.extend(("", "@para"))
        if locale != current_locale:
            lines.append(f"@language {locale}")
            current_locale = locale
        if direction != current_direction:
            lines.append(f"@direction {direction}")
            current_direction = direction
        lines.append(text)
    return "\n".join(lines) + "\n"


def epub(version: int) -> bytes:
    content = html_document(xhtml=True).encode()
    container = b'''<?xml version="1.0"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>
'''
    if version == 2:
        package = f'''<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" unique-identifier="id" version="2.0">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="id">urn:rsvpnano:multilingual-corpus</dc:identifier>
    <dc:title>{TITLE}</dc:title><dc:language>en</dc:language>
  </metadata>
  <manifest><item id="chapter" href="chapter.xhtml" media-type="application/xhtml+xml"/></manifest>
  <spine><itemref idref="chapter"/></spine>
</package>
'''.encode()
        entries = (("META-INF/container.xml", container), ("OEBPS/content.opf", package), ("OEBPS/chapter.xhtml", content))
    else:
        package = f'''<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" unique-identifier="id" version="3.0">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="id">urn:rsvpnano:multilingual-corpus</dc:identifier>
    <dc:title>{TITLE}</dc:title><dc:language>en</dc:language>
  </metadata>
  <manifest>
    <item id="chapter" href="chapter.xhtml" media-type="application/xhtml+xml"/>
    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
  </manifest>
  <spine><itemref idref="chapter"/></spine>
</package>
'''.encode()
        nav = f'''<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<head><title>Contents</title></head><body><nav epub:type="toc"><ol>
<li><a href="chapter.xhtml">{TITLE}</a></li></ol></nav></body></html>
'''.encode()
        entries = (("META-INF/container.xml", container), ("OEBPS/content.opf", package),
                   ("OEBPS/chapter.xhtml", content), ("OEBPS/nav.xhtml", nav))

    output = io.BytesIO()
    with zipfile.ZipFile(output, "w") as archive:
        write_zip(archive, "mimetype", b"application/epub+zip", zipfile.ZIP_STORED)
        for name, data in entries:
            write_zip(archive, name, data, zipfile.ZIP_DEFLATED)
    return output.getvalue()


def write_zip(archive: zipfile.ZipFile, name: str, data: bytes, compression: int) -> None:
    info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
    info.compress_type = compression
    info.external_attr = 0o644 << 16
    archive.writestr(info, data)


def escape_html(text: str) -> str:
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def outputs() -> dict[str, bytes]:
    return {
        "multilingual.txt": text_document().encode(),
        "multilingual.md": text_document().encode(),
        "multilingual.html": html_document().encode(),
        "multilingual.xhtml": html_document(xhtml=True).encode(),
        "multilingual-epub2.epub": epub(2),
        "multilingual-epub3.epub": epub(3),
        "multilingual.rsvp": rsvp_document().encode(),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate the deterministic multilingual format corpus.")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    generated = outputs()
    if args.check:
        stale = [name for name, data in generated.items() if not (OUTPUT / name).is_file() or (OUTPUT / name).read_bytes() != data]
        if stale:
            raise SystemExit("Stale multilingual corpus: " + ", ".join(stale))
        return 0
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for name, data in generated.items():
        (OUTPUT / name).write_bytes(data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
