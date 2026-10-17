# BLACK cutscene captions

The runtime uses the launcher's **Game > Menu language** for the subtitle track. English is the transcript source and Brazilian Portuguese was the initial translation. Spanish, French, German, and Italian cutscene tracks have received an editorial pass for dialogue, context, speaker labels, and obvious translation defects. They have not been verified by native speakers or against every audio cue.

| Video stem | English | pt-BR | es | fr | de | it |
| --- | --- | --- | --- | --- | --- | --- |
| `03_N` | yes | yes | yes | yes | yes | yes |
| `05_N` | yes | yes | yes | yes | yes | yes |
| `07_N` | yes | yes | yes | yes | yes | yes |
| `09_N` | yes | yes | yes | yes | yes | yes |
| `11_N` | yes | yes | yes | yes | yes | yes |
| `13_N` | yes | yes | yes | yes | yes | yes |
| `15_N` | yes | yes | yes | yes | yes | yes |
| `17_N` | yes | yes | yes | yes | yes | yes |
| `18_N` | yes | yes | yes | yes | yes | yes |

The files live at `subtitles/<stem>.srt` for English and `subtitles/<locale>/<stem>.srt` for other languages. Video stems without spoken dialogue, including `AK_N` and `MP5_N`, intentionally have no subtitle track.

The new locale tracks preserve the English cue indices and timestamps, aside from the existing merged cue in pt-BR `07_N`. The non-English dialogue tracks have received an AI editorial pass, but still need native-speaker review and comparison against actual playback. The English source has several uncertain transcript passages. Inclusion does not claim an official transcript or final synchronization.

All nine spoken cutscenes now prefix each subtitle cue with a localized speaker label. The opening exchange identifies the first voice as the Interrogator and Kellar's replies as Sgt. Kellar. The pt-BR `07_N` track has two speakers inside one existing cue; both are labeled while its original timing is retained. A few speaker assignments are contextual estimates and are listed in `subtitles/SPEAKER_ATTRIBUTION_REVIEW.csv` for review against the cutscene audio. The exact pre-label SRT files and SHA-256 values are preserved in `.work/srt-backup-before-speaker-tags-20261011/` (local rollback copy).


Subtitle rendering wraps long cues to the selected presentation aspect (4:3, 16:9, 21:9, or another configured ratio), keeps a safe horizontal margin inside the displayed picture, and supports up to five visible lines. The runtime text buffer accepts up to 255 UTF-16 characters so current long localized cues are not truncated before wrapping. Cue timing remains the SRT timing; this layout change does not extend or retime the spoken cue.
