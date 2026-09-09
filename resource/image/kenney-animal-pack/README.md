# Kenney Animal Pack

2D raster assets supplied for image rendering benchmarks.

- **Author:** Kenney Vleugels for Kenney.
- **Source:** [Animal Pack](https://kenney.nl/assets/animal-pack).
- **License:** [Creative Commons Zero (CC0 1.0 Universal)](https://creativecommons.org/publicdomain/zero/1.0/).
- **Original notice:** [License.txt](License.txt), preserved unchanged from the download.

The author's notice permits personal and commercial use and states that credit
is optional. These third-party assets are provided under CC0, separately from
the repository's MIT license.

## Included assets

`PNG/` contains all 80 original PNG files: ten animals in eight style variants.
The original filenames, subdirectories, and image bytes are preserved. All
images use 8-bit RGBA; dimensions vary by animal and style. The PNGs total
470,705 bytes.

Spritesheets, vector sources, previews, website shortcuts, and `Thumbs.db` files
are omitted because they are not needed for the raster-image benchmark.

## Benchmark use

The pack contains 72 unique file contents by SHA-256; some style variants are
byte-identical. When selecting a set of at least 25 different images, count
unique contents rather than filenames. Keep the chosen asset order fixed and
use the same seed and selection procedure for both rendering engines.

The `multiimagebench` executables use a fixed manifest of 25 byte-distinct PNGs
from this pack. Each of the benchmark's 5,000 rendered instances selects one
of those files using the command-line seed; the same generated sequence is
used by both rendering engines. The manifest order is documented in
`src/common/multi_image_layout.hpp` and should remain stable for comparisons.
The multi-image scene uses alpha values in the 253–255 range.
