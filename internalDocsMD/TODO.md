# TO DO

- use fork of allolib that only has necessary components
  - adjust main build to not use all of allolib build components
- move entirely to cpp, figure how system / gui will be orchestrated. alloapp? external gui lib?
- clean up documentation
  - public facing
  - consolidate dev history and testing docs
- clean up repo
  - move offline render code [spatial_engine/src] into spatial_engine/spatialRender and adjust cmake and other code
  - clean up the random run files and shell scripts at project root
- bugs to fix:
  - main engine playback (reloc and pops)
  - auto comp

- windows support

# Crucial CLI / GUI Features:

- select output at run time - seems to work
- limit buffer size selectiom - potentially dangerous / produce warnings

# Transcoder

- update transcoder to reflect paper status
