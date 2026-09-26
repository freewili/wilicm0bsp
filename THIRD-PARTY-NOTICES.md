# Third-party notices

- `libs/onewili`: Free-Wili, MIT, license in the pinned submodule.
- GoogleTest: Google, BSD 3-Clause, fetched at commit
  `f8d7d77c06936315286eb55f8de22cd23c188571` for tests only. Its license remains
  in the fetched source tree.
- Python `result` 0.17.0: MIT. Setup installs its distribution metadata and
  license in `.runtime/`, which is copied into installed Python apps.
- Linux driver builds link the system libgpiod 2.x package (LGPL-2.1-or-later).
  The driver source is part of this MIT-licensed BSP; libgpiod retains its license.

The `fwcm0` driver was extracted from FreeWili's existing CM0 implementation
on 2026-09-26. Its CMake packaging was adapted to work from this standalone
repository. No firmware checkout is required to build it.
