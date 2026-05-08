# TO DO

## immediate

- clean up spatial_engine/src -> spatial render // deprecate vbap render
- debug overall windows build

## next tasks

- create a cache for default speaker layout
- check if gain smoothing is necessary
- duplicate api docs in internal and public, which is most up to date?

- clean up documentation
  - public facing
  - consolidate dev history and testing docs

## Crucial CLI / GUI Features:

- update transcoding tab to use full features of trancoder
- add more info to engine log - terminal output should be similar - option to expand engine log - mostly upon failer - more info
- allow for setting runtime params prior to starting engine, add reset button
- limit buffer size selectiom - potentially dangerous / produce warnings

## Other Tasks for final packaging:

- add render tab, dont bundle with transcoder ? fix rendering code in spatial_engine/spatialRender and spatial_engine/src

# Future Work - move to future work md

- code signing

## Spatial root:

- move spatial transformation math into seperate file potentially
- make im gui look nicer, add visualizer
- queue multiple files
- configure dbap to have more presense in large spaces
- allow for setting dbap focus metadata in layout or lusid
- add binueral mixdown using ear

## transcoding:

- ambisonic encoding with - https://www.matthiaskronlachner.com/?p=2015
  (standalone app + jack)
