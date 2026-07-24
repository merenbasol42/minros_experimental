"""host/test ortak fixture ve yardımcıları.

Buradaki testler gerçek donanım gerektirmez: minrospy'nin core protokol
katmanını (Parser/Framer/wireframe), bellek içi byte akışları besleyerek
sınarlar. `lib/minrospy` host'a git submodule + editable pip install ile
bağlı olduğu için doğrudan import edilebilir olmalı; bulunamazsa (venv
kurulmamışsa) submodule'ü doğrudan path'e ekleyerek devam ederiz.

Çalıştırma:
    cd host && python -m pytest test
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "lib", "minrospy"))

import pytest

from minrospy.core.framer import Framer
from minrospy.core.parser import Parser


class Collector:
    """Parser'ın on_frame_completed / on_error callback'lerini sıraya göre toplar."""

    def __init__(self, parser: Parser):
        self.frames: list[bytes] = []
        self.errors: list = []
        parser.on_frame_completed = self.frames.append
        parser.on_error = self.errors.append


@pytest.fixture
def framer() -> Framer:
    return Framer()


@pytest.fixture
def parser() -> Parser:
    return Parser()


@pytest.fixture
def collector(parser: Parser) -> Collector:
    return Collector(parser)
