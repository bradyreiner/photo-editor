# Approved local SigLIP 2 retrieval proof

## Outcome and scope

The user approved the most recent request: download the approximately 1.54 GB
SigLIP 2 search model into an isolated test directory. That download is complete
and verified. This was **not** interpreted as approval to clone the existing
20.9 GB chat model; those weights were not touched.

Real image/text inference now works on CPU and Apple MPS, and actual generated
vectors pass through the production C++ `VisualEmbeddingIndex` persistence and
ranking API. Five labeled queries rank an expected image first on five inputs
covering four scenes. This is a small local smoke test, not corpus-quality proof,
packaged embedded inference, an enabled Find photos feature or calibrated
fine-detail/negation verification.

## Pinned installation

- Root: `/private/tmp/framehouse-siglip2-O2ha9I/model`.
- Model: `google/siglip2-base-patch16-224`.
- Revision: `75de2d55ec2d0b4efc50b3e9ad70dba96a7b2fa2`.
- Weight file: 1,500,800,904 bytes, SHA-256
  `612923381c76ec5a9bed335d1c48827e3f2e506ac31b044b63b2031fadee6a0b`.
- `scripts/install-search-model.py` pins all eight artifacts. LFS files use
  upstream SHA-256; small configuration files use their pinned Git blob hashes.
  The final receipt also records SHA-256 for every file. A pre-existing invalid
  artifact is rejected, not overwritten. Downloads require explicit `--download`.
- `/private/tmp/framehouse-siglip2-O2ha9I/download-retry.log` retains verification
  and inventory. The first attempt rejected a tiny config because the server's
  redirect exceeded its exact size limit. Redirect allowance is now at least
  64 KiB, while final content size and hash checks remain exact.
- Dependencies live only in the sibling `venv/`. The pinned direct requirements
  are in `tests/search-model-proof-requirements.txt`; the full resolved environment
  is retained in `dependencies-freeze.txt`. No system Python, app runtime or user
  catalog configuration was modified. Temporary storage is not a durable packaged
  installation; preserve/reinstall it through the explicit installer when needed.

Upstream primary sources:
[model card](https://huggingface.co/google/siglip2-base-patch16-224),
[pinned files](https://huggingface.co/google/siglip2-base-patch16-224/tree/75de2d55ec2d0b4efc50b3e9ad70dba96a7b2fa2),
[SigLIP processor guidance](https://huggingface.co/docs/transformers/model_doc/siglip).

## Actual inference and native ranking

`tests/siglip_retrieval_probe.py` verifies the pinned model before loading it,
uses `local_files_only`, disables remote code and hub telemetry, sets offline
environment flags and rejects Python TCP connections during inference. This is
a probe-level safeguard, not a claim of OS-wide network isolation for arbitrary
native dependencies.

It performs EXIF orientation and embedded-profile conversion to sRGB, then uses
the pinned slow image processor and provided fast tokenizer. The initial attempt
accidentally selected a slow tokenizer requiring SentencePiece; selecting image
and tokenizer implementations separately resolved that without extra packages.
Queries use a recorded photo template, 64-token maximum-length padding and no
silent truncation. Model revision, dependency versions, preprocessing and provider
are included in the embedding-space identity.

Fixtures were selected and visually inspected before recording final results:

| Input | Origin / scope |
| --- | --- |
| Portrait JPEG | Existing HDR+ Nexus 6 reference fixture |
| Brick building | Existing Gaussian-workspace South Building photo, `P1180142.JPG` |
| White dog | Small public [PyTorch sample](https://raw.githubusercontent.com/pytorch/hub/master/images/dog.jpg) |
| Two cats on a sofa | Small public [Hugging Face documentation sample](https://huggingface.co/datasets/huggingface/documentation-images/resolve/main/coco_sample.png) |
| RAW-derived portrait | Existing `catalog-reopen-proof-MajJCZ/raw-reopen-qa.jpg` from the documented RAW save/reopen proof |

The last input is a previously rendered preview of the same portrait, not a fifth
independent scene. No new RAW decode occurs in this encoder probe. The two small
public images were downloaded for local testing; no user photos were uploaded.
Fixture paths, expected IDs and image checksums are retained in `fixtures.json`
and the inference receipts. No originals were edited.

All five queries pass on both providers: hat portrait, brick building with trees,
white dog outside, two cats on a sofa and a person with a red collar. Either
portrait rendering is accepted for portrait queries. The native test loads actual
768-dimensional image vectors, stages and commits them to a disposable SQLite
index, queries using actual text vectors, checks all expected first results and
compares the full native ranking/similarities against the encoder dot products.
No synthetic vectors or generated numeric substitutes are used in these runs.

Retained receipts under `/private/tmp/framehouse-siglip2-O2ha9I/`:

- `real-vectors-mps-final.json`, `inference-mps-final.log`.
- `real-vectors-cpu.json`, `inference-cpu.log`.
- `native-real-mps-final.json`, `native-real-cpu.json`: both exit 0 with expected
  first results and native/encoder ranking parity.
- `provider-comparison.json`: maximum absolute CPU/MPS vector difference is
  9.12e-7 for images and 8.05e-7 for text on these fixtures. Provider identities
  remain separate; this observation does not establish universal interchangeability.

## Timing boundaries

In the final MPS run, warm text encoding is approximately 5.4-8.5 ms. CPU warm
encoding is approximately 20.6-23.9 ms. These are synchronized model/tokenizer
measurements, not end-to-end library queries. Native retrieval over the tiny
fixture set is separately recorded in microseconds.

The first successful MPS process paid about 3.61 seconds for its first image and
325 ms for its first text query; later processes benefited from warm host state.
The final MPS receipt separately records 1,665 ms artifact verification and
1,047 ms model/processor loading. None of these should be hidden inside a claim
of instant cold startup. Image timings include file decode/profile processing.

The earlier 573-607 ms exact top-32 scan over 20,000 synthetic vectors remains a
separate index benchmark. It cannot simply be added to these timings and called
a measured real-library workflow. The earlier 50-60 second chat checks perform
query-conditioned verification; retrieval encoding is a different operation,
not proof that fine-detail verification now costs a few milliseconds.

## Reproduction and remaining work

```sh
python3 scripts/install-search-model.py --root /ABSOLUTE/ISOLATED/model --download
/ABSOLUTE/ISOLATED/venv/bin/python tests/siglip_retrieval_probe.py \
  --model-root /ABSOLUTE/ISOLATED/model --manifest /ABSOLUTE/fixtures.json \
  --output /ABSOLUTE/new-receipt.json --device mps
build/dev-macos-debug/tests/visual_embedding_index_test --real-vectors /ABSOLUTE/new-receipt.json
```

Installer tests reject missing, symlinked, size-mismatched and checksum-mismatched
artifacts without any model downloads. They are registered as
`search_model_installer_test`; the actual inference mode remains explicit opt-in.
`final-regressions.log` records six focused suites passing in 8.92 seconds:
installer, model artifacts, managed runtime, visual search, embedding index and
search/edit workflow. `native-final-build.log` and `final-configure-build.log`
record successful test builds. All owned installation, inference, build and test
processes are terminal; no owned inference service remains running.

Next is an owned long-lived encoder process and background catalog coordinator,
with verified model status, cancellation/restart, batching, source/recipe/delete
invalidation, no stale publication and visible indexing errors. Only then should
Find photos use this path, followed by actual native UI and labeled larger-corpus
tests. Packaging, offline installation/recovery and the managed chat runtime are
still separate open gates. The full RAW/rendering, loading, Gaussian 3D/4D and
professional-tool goal remains active.

The export-completion performance investigation was paused when model approval
arrived. It produced no implementation change or accepted performance candidate.
