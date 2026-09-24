"""Verified, local-only SigLIP encoder shared by the owned worker and proof CLI."""
from __future__ import annotations

import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import socket
import time


def digest(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


class Encoder:
    def __init__(self, root: Path, device: str):
        if device not in ("cpu", "mps"):
            raise ValueError("Unsupported inference device")
        os.environ.update(HF_HUB_OFFLINE="1", TRANSFORMERS_OFFLINE="1",
                          HF_HUB_DISABLE_TELEMETRY="1", TOKENIZERS_PARALLELISM="false")
        spec = importlib.util.spec_from_file_location("pinned_search_model", Path(__file__).with_name("install-search-model.py"))
        installer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(installer)
        started = time.perf_counter()
        verified = installer.verify_model(root)
        self.verification_ms = (time.perf_counter() - started) * 1000

        def deny_network(*_args, **_kwargs):
            raise PermissionError("Network disabled in local search encoder")
        socket.socket.connect = deny_network
        socket.socket.connect_ex = deny_network
        socket.create_connection = deny_network

        import torch
        import transformers
        import PIL
        import numpy
        import tokenizers
        import safetensors
        from transformers import AutoModel, AutoImageProcessor, AutoTokenizer, SiglipProcessor
        actual_versions = (torch.__version__.split("+")[0], transformers.__version__, PIL.__version__,
                           numpy.__version__, tokenizers.__version__, safetensors.__version__)
        if actual_versions != ("2.10.0", "4.57.6", "12.1.1", "2.5.3", "0.22.2", "0.8.0"):
            raise ValueError("Encoder dependencies do not match search-model-proof-requirements.txt")
        if device == "mps" and not torch.backends.mps.is_available():
            raise RuntimeError("MPS requested but unavailable; no silent CPU fallback")
        torch.set_num_threads(4)
        self.torch, self.device = torch, device
        started = time.perf_counter()
        self.model = AutoModel.from_pretrained(str(root), local_files_only=True, trust_remote_code=False,
                                              use_safetensors=True, attn_implementation="sdpa").eval().to(device)
        self.processor = SiglipProcessor(
            image_processor=AutoImageProcessor.from_pretrained(str(root), local_files_only=True,
                                                               trust_remote_code=False, use_fast=False),
            tokenizer=AutoTokenizer.from_pretrained(str(root), local_files_only=True,
                                                    trust_remote_code=False, use_fast=True))
        self.synchronize()
        self.load_ms = (time.perf_counter() - started) * 1000
        self.contract = {"schema": "siglip2-photo-retrieval-v1", "model": verified,
                         "transformers": transformers.__version__, "torch": torch.__version__, "pillow": PIL.__version__,
                         "numpy": numpy.__version__, "tokenizers": tokenizers.__version__, "safetensors": safetensors.__version__,
                         "pixels": "exif-transpose-icc-to-srgb-rgb8-pinned-slow-processor",
                         "text": "This is a photo of {query}.", "tokenizer": "pinned-tokenizer-json-fast",
                         "padding": "max_length", "maxLength": 64, "truncation": False,
                         "normalization": "l2-float32", "device": device}
        self.space_key = digest(json.dumps(self.contract, sort_keys=True, separators=(",", ":")).encode())

    def synchronize(self):
        if self.device == "mps":
            self.torch.mps.synchronize()

    def vector(self, tensor):
        if tuple(tensor.shape) != (1, 768) or not self.torch.isfinite(tensor).all().item():
            raise ValueError("Encoder produced an invalid vector")
        norm = tensor.float().norm(dim=-1, keepdim=True)
        if not self.torch.isfinite(norm).all().item() or norm.item() <= 0:
            raise ValueError("Encoder produced a zero vector")
        result = (tensor.float() / norm).cpu()[0].tolist()
        self.synchronize()
        return result

    def image(self, path: Path) -> dict:
        from PIL import Image, ImageCms, ImageOps
        if not path.is_absolute() or not path.is_file():
            raise ValueError("Image must be an explicit existing local file")
        started = time.perf_counter()
        # One bounded byte snapshot makes source hash and decoded content agree
        # even if the path is replaced during inference. The coordinator must
        # additionally reject results from superseded catalog/recipe generations.
        with path.open("rb") as stream:
            content = stream.read(64 * 1024 * 1024 + 1)
        if len(content) > 64 * 1024 * 1024:
            raise ValueError("Use a rendered preview smaller than 64 MiB")
        with Image.open(io.BytesIO(content)) as source:
            if source.width * source.height > 200_000_000:
                raise ValueError("Image exceeds decode limit")
            profile = source.info.get("icc_profile")
            image = ImageOps.exif_transpose(source).convert("RGB")
            if profile:
                image = ImageCms.profileToProfile(image, ImageCms.ImageCmsProfile(io.BytesIO(profile)),
                                                 ImageCms.createProfile("sRGB"), outputMode="RGB")
            content_key = digest(f"sRGB8:{image.width}x{image.height}:".encode() + image.tobytes())
            inputs = self.processor(images=image, return_tensors="pt").to(self.device)
        with self.torch.inference_mode():
            vector = self.vector(self.model.get_image_features(**inputs))
        return {"vector": vector, "contentKey": content_key, "sourceSha256": digest(content),
                "milliseconds": (time.perf_counter() - started) * 1000}

    def text(self, query: str) -> dict:
        if not isinstance(query, str) or not query.strip() or len(query) > 2000:
            raise ValueError("Query must contain 1-2000 characters")
        started = time.perf_counter()
        prompt = f"This is a photo of {query}."
        inputs = self.processor(text=[prompt], padding="max_length", max_length=64,
                                truncation=False, return_tensors="pt").to(self.device)
        if inputs["input_ids"].shape[-1] != 64:
            raise ValueError("Query exceeds the text context; refusing silent truncation")
        with self.torch.inference_mode():
            vector = self.vector(self.model.get_text_features(**inputs))
        return {"vector": vector, "contentKey": digest(prompt.encode()),
                "milliseconds": (time.perf_counter() - started) * 1000}
