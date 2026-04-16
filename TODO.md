# TO DO

## immediate

- merge transcoder with spatial seed version

- deprecate remap csv - use internal mapping logic (virtual linear channels array remapped to input channels at the end) - redocument and test -- planning in md doc currently

- debug windows build

- test runtime now that everything builds via ci
- autocomp math
- move spatial transformation math into seperate file potentially

- minimal allolib fork - and test builds - remove shallow clone scripts

## tasks

- update LUSID - deprecate old python components. update documentation

- configure dbap to have more presense in large spaces

- have engine delete old lusid files on startup / boot up / when engine is re run

- use fork of allolib that only has necessary components
  - adjust main build to not use all of allolib build components

- clean up documentation
  - public facing
  - consolidate dev history and testing docs
- clean up repo
  - move offline render code [spatial_engine/src] into spatial_engine/spatialRender and adjust cmake and other code

- bugs to fix:
  - auto comp and overal runtime debug focus

- code signing

# Crucial CLI / GUI Features:

- add more info to engine log - terminal output should be similar - option to expand engine log
- add more volume decrease and increase support
- select output at run time - seems to work
- allow for setting runtime params prior to starting engine
- limit buffer size selectiom - potentially dangerous / produce warnings
- add fetch examples to gui - using examples .sh - update to using hugging face links
- update available example audio files
- add render tab, dont bundle with transcoder
- add gui for creating speaker layout
- add binueral mixdown using ear

- make sure all layouts are available from gui dropdown

- allow for setting dbap focus metadata in layout or lusid

- make im gui look nicer

# Transcoder

- update transcoder to reflect paper status
