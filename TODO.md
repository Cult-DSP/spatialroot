# TO DO

## immediate

- windows ci
- windows dependencies and build
- clean up old documentation and consolidate
- autocomp math

- minimal allolib fork

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
  - main engine playback (reloc and pops)
  - auto comp and overal runtime debug focus

- windows support

# Crucial CLI / GUI Features:

- add more volume decrease support
- select output at run time - seems to work
- limit buffer size selectiom - potentially dangerous / produce warnings
- add render tab, dont bundle with transcoder

# Transcoder

- update transcoder to reflect paper status
