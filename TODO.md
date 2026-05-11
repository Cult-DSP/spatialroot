# TO DO

## immediate

## next tasks

- fix bugs in transcoder tab:

* old failed runs must be cleared
* diagnostic must be more robust, text in log is hard to read. "exit code 1" is unspecific
* did recent api fixes change transcoder wiring? something isn't functional anymore
  - ADM -> lusid package has serious issues - failed while writing package stems.
  -

\*need a progress bar + list stages, I know this is available in the transcoder

- -packaging bugs

## audit

api audit
gui audit
dsp audit
cmake wiring audit
build system audit
git submodule fetching audit
overall codebase audit
OS AND COMPATIBILITY

- windows
- linux
- audio backend
- glfw

## docs

- duplicate api docs in internal and public, which is most up to date? ---
- clean up documentation
  - public facing
  - consolidate dev history and testing docs

  - test speaker layout config on windows, validate code

## Other Tasks for final packaging:

OS AND COMPATIBILITY TESTING
PACKAGING

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
