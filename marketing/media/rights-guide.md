# Marketing media rights guide

Use `sources.json` as the rights and provenance record for every photo or clip in a Looksy campaign. Keep the original source page, direct file URL, license page, source-page snapshot, exact local SHA-256, file size, acquisition timestamp, creator, and any known release status together. If the source acquisition time was not retained, record that as unknown; a file's modification time is not proof of when it was downloaded.

## What the current records establish

Three files have an asset-specific Wikimedia Commons page that names the creator and displays a CC0 1.0 dedication: the Hong Kong night skyline, the mountain panorama, and the empty drinking glass. The page for “Mountain and Landscape” also displays CC0, but names no author and links to Pikwizard; that provenance has not been reconfirmed. Its manifest status is `internal-review`, with unresolved source provenance recorded in `rights_notes`, so do not use that file in external ads until the original creator and source terms are checked.

For CC0 assets, the page's copyright permission covers commercial copying, modification, and distribution. Attribution is not a CC0 condition, but preserving the creator and source credit is useful provenance. Do not imply that a photographer, pictured person, place, building, or maker endorses Looksy.

## Ad-use checks beyond copyright

CC0 does not clear publicity, privacy, trademark, patent, property, or other third-party rights. The official deed also says it makes no warranties and warns against implying endorsement. An asset-specific license page therefore establishes the copyright license claim, not every permission needed for a campaign.

Before paid placement, inspect the final crop and context for identifiable people, logos, artwork, distinctive product designs, restricted venues, or text that could suggest endorsement. Obtain and retain model or property releases when the campaign needs them. If the release status is unknown, choose a different image, crop out the concern where appropriate, or keep the asset in internal production. The mountain panorama has tiny distant people and ski-area structures, so its paid-ad use remains review-pending. The glass image shows a drinking vessel on a decorated table; use it to demonstrate editing and avoid presenting it as a glassware endorsement. The Hong Kong image shows a skyline and waterfront; visible architectural or location rights are not separately cleared.

## Repeatable intake and campaign record

1. Choose the exact asset page and confirm that its license applies to the exact original file. Prefer a named creator and a direct license statement from the rights holder or a well-documented CC0 page.
2. Save a dated, human-readable snapshot or revision permalink for the asset page and a snapshot of the license terms. Record the direct source URL and the time the file was acquired. Keep source-download time separate from a later copy or export time.
3. Hash the downloaded bytes with SHA-256. Store the file without silently replacing it; if the source file changes, create a new record and repeat the license review.
4. Record copyright license, creator, attribution preference, modifications, model/property release status, visible marks, ad-use status, and the reviewer. Use a status such as `internal_review` or `release_review_pending` whenever a material right is unknown.
5. For each campaign output, record the source IDs, crops or edits, final file hash, placement, and any required attribution. Recheck likeness, property, trademarks, music, and endorsement implications in the finished ad, since editing can change the context.

Current source-page and CC0 deed snapshots are in `marketing/media/licenses/`. The stock files are in `marketing/media/stock/`. All four photos were freshly downloaded from their recorded Wikimedia original-file URLs and verified against the existing corpus hashes. `sources.json` records acquisition timestamps for those new downloads, original-file hashes, and source-page revision permalinks. License snapshots are readable transcriptions, not full archived HTML pages.

## Practical footage bank

For the first campaign, shoot a simple original set: an unbranded cup/plant beside a window, a walk outdoors, and your desk. Collect 8–10 camera stills and 4–6 short clips from the same scene. Preserve genuine RAW/log originals for Looksy demonstrations; ordinary stock and synthetic imagery cannot prove those formats. Record your real edit, revision, and export as the app-footage layer.

For extra B-roll, [Pexels videos](https://www.pexels.com/videos/) supplies free footage under the [Pexels license](https://www.pexels.com/license/) (checked 2026-10-07 UTC). Its license permits modification and marketing use, with restrictions including implied endorsement and redistribution as stock. It is a commercial-use license, not CC0. Save the exact clip page and license when acquiring a clip, choose scenery/textures/unbranded objects, and record the actual file hash. No Pexels clip is bundled in this kit.

For synthetic source visuals, generate landscapes, abstract optical textures, or unbranded fictional product scenes. Keep the prompt and provider terms with the asset. A slow pan/zoom can create motion from a still; label it as source animation. The bundled `generated/alpine-source-motion.mp4` is such an animation and a decoder test, not footage created by Looksy. For the review soundtrack, this kit uses an original sample-free composition; keep a separately cleared track for any trend-audio experiment.
