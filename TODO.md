# TO DO

## immediate

- pause is not a smooth fade like stop iis
- add more gain control in api / engine then in gui
-
- fix windows build after cult-allolib change
- re organize custom submodules into internal/ and update cmake and shell scripts,
- make sure all submodules are checked out at specific current commits
- audit what can be removed from cult-allolib
- have engine delete old lusid files on startup / boot up / when engine is re run

- debug overall windows build

## next tasks

- duplicate api docs in internal and public, which is most up to date?
- move spatial transformation math into seperate file potentially
- update LUSID - deprecate old python components. update documentation

- use fork of allolib that only has necessary components
  - adjust main build to not use all of allolib build components

  - deprecate vbap render

- clean up documentation
  - public facing
  - consolidate dev history and testing docs
- clean up repo
  - move offline render code [spatial_engine/src] into spatial_engine/spatialRender and adjust cmake and other code
  - deprecate vbap renderer

- bugs to fix:
  - auto comp and overal runtime debug focus
    - focus high can cause artifacts

- code signing

# after that:

- configure dbap to have more presense in large spaces

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

- potentially queue multiple files

# Other Tasks for final packaging:

# Future Work

## transcoding:

- ambisonic encoding with - https://www.matthiaskronlachner.com/?p=2015
  (standalone app + jack)
