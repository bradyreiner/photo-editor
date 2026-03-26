# Timeline Editor Roadmap

## Goal

Turn the current single-track timeline into a dedicated video editing workspace with a credible professional workflow.

## Current baseline

The app already supports:

- loading and saving timeline documents
- adding gallery videos as clips
- selecting, trimming, splitting, reordering, and removing clips
- sequence playback and timeline render export

The current gaps are structural:

- timeline editing still lives inside the shared Sequence workspace
- the document model is a flat clip list rather than tracks and clip instances
- the UI is a clip strip, not a real multitrack timeline canvas

## Product target

The first serious milestone is workflow parity for short-form editing, not full Resolve or Premiere feature parity.

That means:

- dedicated Timeline workspace
- source and program monitoring
- multitrack video and audio editing
- precise trim and transport tools
- audio waveforms, meters, and fades
- keyframeable clip properties
- reliable export presets and queueing

## Required UI surfaces

### Timeline workspace shell

- top toolbar for edit tools, snapping, linked selection, ripple state, markers, and timecode
- source monitor for marking clip In and Out before edits
- program monitor for sequence playback, overlays, and safe areas
- media bins and effects browser
- inspector for selected clip, effect stack, motion, audio, and metadata
- multitrack timeline canvas with ruler, playhead, zoom, and track headers
- audio meters and lightweight mixer

### Timeline canvas

- separate video and audio tracks
- drag-and-drop clip placement
- trim handles and edge feedback
- transitions and overlap indicators
- clip labels, thumbnails, and audio waveforms
- markers on clips and on the sequence
- keyframe lanes for transform, opacity, and audio
- track controls for lock, mute, solo, visibility, and targeting

## Required editing features

### Core editorial

- insert and overwrite edits
- lift and extract
- razor tool
- ripple, roll, slip, and slide trims
- snapping and magnet toggle
- linked audio/video selection
- duplicate, replace, and match frame
- copy and paste with insert and overwrite behavior

### Sequence model

- multiple sequences per project
- sequence settings for resolution, frame rate, and audio sample rate
- video and audio track collections
- gap handling
- linked clip groups
- nested or compound sequences
- markers and notes

### Audio

- waveform generation and caching
- clip gain and track gain
- audio fades
- pan and balance
- meters and clipping indicators
- separate track types for dialogue, music, and effects later

### Effects and motion

- transform controls
- opacity and blend modes
- crop and basic retime
- transitions
- effect stack ordering
- property keyframes

### Media management

- bins and smart collections
- search and filtering
- subclips
- relink and offline states
- proxy attachment and switching
- media metadata columns

### Delivery

- export presets
- queue with progress and status
- codec, format, and destination controls
- background render and failure reporting

## Implementation phases

1. Workspace split
- make Timeline a top-level workspace
- keep Carousel as its own separate workspace
- route video assets and saved timelines directly into Timeline

2. Sequence data model rewrite
- replace the flat clip list with track-based sequence structures
- preserve current save and load behavior through migration

3. Timeline canvas
- build the ruler, playhead, zoom, and track headers
- support clip drag, selection, trimming, and snapping

4. Monitoring and edit operations
- add source monitor and program monitor
- implement insert, overwrite, ripple, and roll workflows

5. Audio lane support
- render waveforms
- add fades, gain, meters, and basic mixing

6. Effect stack and keyframes
- motion, opacity, crop, transitions, and keyframe lanes

7. Playback and export hardening
- proxy playback
- render cache
- export presets and queueing

## Resolve and Premiere parity gaps after phase 7

Still out of scope unless video becomes the primary product focus:

- multicam editing
- shared project collaboration
- transcription and text-based editing
- caption authoring
- advanced color page workflows
- Fairlight-class audio tooling
- Fusion or After Effects class compositing
