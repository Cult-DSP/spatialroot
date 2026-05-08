# TO DO

## immediate

- clean up spatial_engine/src
- fix windows build after cult-allolib change
- re organize custom submodules into internal/ and update cmake and shell scripts,
- make sure all submodules are checked out at specific current commits
- have engine delete old lusid files on startup / boot up / when engine is re run

- debug overall windows build

## next tasks

- check if gain smoothing is necessary
- duplicate api docs in internal and public, which is most up to date?
- move spatial transformation math into seperate file potentially
  - deprecate vbap render

- clean up documentation
  - public facing
  - consolidate dev history and testing docs
- clean up repo
  - move offline render code [spatial_engine/src] into spatial_engine/spatialRender and adjust cmake and other code
  - deprecate vbap renderer

- code signing

# after that:

# Crucial CLI / GUI Features:

- add more info to engine log - terminal output should be similar - option to expand engine log
- make sure all layouts are aailable from gui dropdown
- allow for setting runtime params prior to starting engine
- limit buffer size selectiom - potentially dangerous / produce warnings
- fix fetch add fetch examples to gui - using examples .sh - update to using hugging face links - maybe as a download button instead
- update available example audio files
- add render tab, dont bundle with transcoder

# Other Tasks for final packaging:

# Future Work

## Spatial root:

- make im gui look nicer, add visualizer
- queue multiple files
- configure dbap to have more presense in large spaces
- allow for setting dbap focus metadata in layout or lusid
- add binueral mixdown using ear

## transcoding:

- ambisonic encoding with - https://www.matthiaskronlachner.com/?p=2015
  (standalone app + jack)
