# LOOKSi director book

The final standalone artifact is `../creative-direction-book.pdf`: twenty landscape pages at 1200×800 points. It uses embedded project typefaces, actual V2 covers/carousel panels, genuine film frames, and the actual campaign storyboards. Source images remain identified by their role and date. The02casebook identifies its prism/brand still from the completed native Timeline export, with the full dated source companion separately retained.

`build_book.py` is the editable builder. Rebuild from the repository root with:

`marketing/.venv/bin/python marketing/campaigns/looksi-launch-2026-11-16/v2/directorbook/build_book.py`

The project marketing virtual environment contains ReportLab, pypdf and pdfplumber. Poppler `pdftoppm` renders the final PDF for visual QA. The artifact-operation marker required by the PDF skill was run once before creation.

`qa/page-01.png` through `page-20.png` are actual Poppler renders of the final PDF. The contact sheets collect every page for visual review. `layout-audit.json` records page titles, exact embedded-image source hashes and basic text-bound checks; `qa/verification.json` records the final structural/source checks and visual review. Four genuine campaign01 film frames are retained in `frames/` for the enlarged phone-art proof on page 6.

The delivered campaign names and captions come from the execution records, rather than stale initial production plans. All literal Unicode/newline escapes are normalized before drawing. Internal proof scope is separated from public-facing copy. Five primary reference links are clickable in the research spread; no third-party reference art was copied into the book.

No post, message, release change or paid spend is performed by this artifact.
