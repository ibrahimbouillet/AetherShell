# Showcase correction log

Checkpoint: 2026-10-23; current local candidate `C5FF64A3E03848EFA7FCD18A0F236221`.
The [QC report](07_QC_REPORT.md) binds observations to the exact exported bytes. “Corrected” below
means the stated implementation or inspected-frame fault was addressed; it does not certify the
whole film at normal playback speed or establish the owner's 100/100 target.

## Rejected takes and implemented corrections

| Fault and evidence | Change | Current evidence / remaining limit |
|---|---|---|
| Black wallpaper in the first two takes; approximately 17 and 12.44 captured samples/s respectively | Restored the available cached scenic wallpaper after the originally referenced source file was unavailable | Owner confirmed restoration. Current opening and ending keys show the train wallpaper; no >99%-black frame in the 1,030-frame decode. |
| Second rejected take contained a 5.95-second capture gap and unrelated private content in the final frame | Added before/after-frame visible-surface guards, bounded offline recipes, and capture buffering | Rejected material is excluded. Current contact review found only the allowed desktop, Aether, pointer, and clean Code scene; guards still have races and cannot validate the contents of an allowed app. |
| Synchronous disk writes and repeated frame reads/uploads reduced acquisition cadence | Bounded tile-based RAM buffer, nonblocking DXGI acquisition, immutable frame-ID cache; restore user apps before archive flush/render | Current source: 985 samples, 57.3721 sampled FPS, native-receipt P95 interval 29.973 ms. This is not 60 distinct desktop updates/s; 236 desktop presents and 316 acquisitions are separate counters. |
| A subsequent take lacked a visible pointer | Capture the real system cursor explicitly and move it through real `SendInput` input | Pointer is present in the inspected current keys and all-frame contact review; final key is neutral with cleared Dock magnification/tooltip. |
| H.264 output was vertically flipped | Correct physical row orientation for the encoder's bottom-up RGB conversion | Synthetic orientation test and current image comparison: upright-source MAE 3.68 versus flipped-source MAE 39.81; inspected keys are upright. |
| GIF red and blue channels were exchanged | Use the WIC RGBA format converter, including the resized path | End-to-end synthetic resize test and current derivative comparison: true-color MAE 6.09 versus swapped-channel MAE 13.85; selected GIF keys inspected. |
| Fresh Code scene showed an “extensions disabled” setup toast | Remove `--disable-extensions`; keep the isolated profile and dedicated empty extensions directory | No disabled-extensions toast observed in the current contact sheets or selected keys. No OS or application security setting was disabled. |
| Close-up/reveal could crop the application's footer logo | Add camera source padding | Footer clipping not observed in the inspected close-up and loaded-window keys. Whole-sequence normal-speed legibility is pending. |
| Pointer remained on a Dock target at the ending | Return the real pointer to its neutral opening position before the final empty hold | Final frame 1029, at 17.150 s, shows neutral pointer, no tooltip, and no residual magnification. |
| Nominal three-second ending included the application's DWM disappearance | Start the final hold after the window is absent and two captured body/backdrop frames match; then record three seconds | Central source ROI matches continuously for 3.0238984 s. This is a sampled large-region comparison, not full-frame equality. |
| Native taskbar could briefly reappear during a surface transition | Let the existing Aether guard settle for at most 150 ms and discard stale acquired frames | Runner does not independently hide the Windows taskbar. Current reviewed frames do not show a second taskbar; interrupted/crash recovery remains a separate OS lifecycle concern. |

The earlier approximately 34.6-sample/s take and a later 964-frame export were also rejected during
iteration: cursor/format faults, setup toast, and an insufficient visually empty ending prevented
acceptance. Old rejected raw archives were selectively removed, recovering approximately 14.68 GB;
local QC evidence was retained. No rejected image is an approved public fixture or presentation asset.

## Remaining acceptance work

| Priority | Interval | Action needed before acceptance |
|---|---|---|
| Blocking | 0.000–17.166667 s | Watch the exact latest MP4 at normal speed from start to end; listen to its audio and verify action synchronization. No such playback/listening has been performed. |
| Blocking | Whole film | Complete temporal flash/comfort review and all unresolved [60 checks and ten gates](06_SCORECARD.md). Numeric luminance steps alone do not establish an accessibility pass. |
| Major | Approach, traverse, launch, reveal, close, ending | Judge inertia, perceived continuity, blur, readability, pacing, and ending at delivery size; use actual playback rather than contact sheets to accept motion. |
| Major | Whole film | Compare appropriate references actually watched and obtain an independent creative critique when available. No unviewed reference receives an invented rating. |
| Publication | Whole film | Complete distribution/asset review and the owner's media selection. Detailed captures and QC stay local. |
| Delivery optimization | GIF derivative | The 98,379,380-byte GIF is expensive for a README/web page; select or optimize a derivative and recheck its exact bytes before public use. |

Any further picture, audio, or export change creates a new candidate: update the hash, regenerate the
affected evidence, and repeat final-file review. The current technical and contact-sheet checks do
not certify “100/100,” external studio quality, or universal recovery of applications and unsaved work.
