# Auto horizon photo fixtures

These NOAA photographs exercise automatic horizon detection on real
sea-and-sky images. Keep the captions and credits below with the fixtures if
they are copied or redistributed.

- `noaa-sun-peeks-clouds.jpg`: a clear, slightly tilted open-ocean horizon.
  Source page: <https://oceanexplorer.noaa.gov/multimedia/sun-peeks-through-the-clouds/>
  Credit: NOAA Ocean Exploration, 2024 Beyond the Blue.
  The caption describes the Pacific Ocean during the Beyond the Blue: Hawai‘i
  Mapping expedition aboard NOAA Ship Okeanos Explorer.
- `noaa-open-ocean.jpg`: a low-contrast open-ocean horizon used to verify that
  uncertain images are rejected rather than rotated automatically.
  Source page: <https://olympiccoast.noaa.gov/science/habitat/open-ocean.html>
  Credit: NOAA.

NOAA's National Ocean Service says that NOAA images are generally public
domain unless otherwise noted, and requests NOAA credit. NOAA Ocean
Exploration also asks users to include the credit from each image caption and
states that captioned copyright images require additional permissions. Neither
fixture caption carries a copyright notice. See
<https://oceanservice.noaa.gov/about/faq.html> and
<https://oceanexplorer.noaa.gov/about/media-kit/>. The image sizes, source
downloads, caption text, and SHA-256 hashes are recorded in `provenance.json`.

The positive fixture's rough visual horizon trace is about -0.9 degrees. This
is only an approximate photographic reference; the test also checks known
relative rotations of the same image and does not treat the rough trace as
exact ground truth.
