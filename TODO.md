# TO DO

## immediate

- clean up old documentation and consolidate
- deprecate remap csv - use internal mapping logic (virtual linear channels array remapped to input channels at the end) - redocument and test
- test runtime now that everything builds via ci
- autocomp math

- minimal allolib fork - and test builds

## tasks

- use fork of allolib that only has necessary components
  - adjust main build to not use all of allolib build components

- clean up documentation
  - public facing
  - consolidate dev history and testing docs
- clean up repo
  - move offline render code [spatial_engine/src] into spatial_engine/spatialRender and adjust cmake and other code
  - clean up the random run files and shell scripts at project root
  - clean up old branches
- bugs to fix:
  - auto comp and overal runtime debug focus

- windows support

# Crucial CLI / GUI Features:

- add more volume decrease support
- select output at run time - seems to work
- limit buffer size selectiom - potentially dangerous / produce warnings
- add render tab, dont bundle with transcoder
- add gui for creating speaker layout
- add binueral mixdown using ear

# Transcoder

- update transcoder to reflect paper status
