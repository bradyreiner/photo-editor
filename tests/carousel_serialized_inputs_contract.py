"""Keep fast equality's field coverage synchronized with Carousel JSON writers."""
from pathlib import Path
import re

source = (Path(__file__).resolve().parents[1] / "apps/desktop/src/carousel_document_controller.cpp").read_text()


def body(signature):
    start = source.index(signature)
    start = source.index("{", start)
    cursor, depth = start + 1, 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[start + 1:cursor - 1]


for kind, writer, argument in (
    ("MediaItem", "mediaObject", "media"),
    ("Slide", "slideObject", "slide"),
    ("LinkedGroup", "groupObject", "group"),
    ("CanvasItem", "itemObject", "item"),
):
    serialized = set(re.findall(r"\b" + argument + r"\.([a-z_0-9]+)", body(
        "QJsonObject CarouselDocumentController::" + writer + "(")))
    compared = set(re.findall(r"\bleft\.([a-z_0-9]+)", body(
        "bool CarouselDocumentController::sameSerializedInputs(const " + kind + "&")))
    assert serialized == compared, (kind, "missing", serialized - compared,
                                   "unexpected", compared - serialized)
    print(f"{kind}: all {len(serialized)} serialized input fields covered")
