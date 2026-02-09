# TO DO

-FIX CHANNEL REMAPPING - on the player end -- update atmos player
-FIX PYTHON / PYTHON 3 VENV ISSUE --

    -MAKE SURE ALL SUBMODULE / PIP INSTALL / BREW INSTALL ARE INSTALLING A SPECIFIC STABLE BUILD

- FIX ATMOS MIXES

- fix commenting prints at the end of pipeline

- transition to render without stem splitting, shouldnt be necessary

- switch to internal datastructures instead of many json's, but keep a single debugging json with info

* change everything to be stable build and git submodules instead of libraries?

- only clone parts of submodules / libs that we are actually using?

- fix hardcoded paths of procesed dir in parser.py and packageForRender, will have to update notebook and runPipeline potentially

# Less Immediate

\*change all instances of deleting a file(s) before writing to use one helper function from utils

- this is relevant in checkAudioChannels, extractMetaData, and others
  \*deal with static objects in LUSID scene (direct_speaker nodes at t=0)

# to consider

- start testing examples from https://zenodo.org/records/15268471
- width parameter - relevant for dbap or reverb considerations

- bundle as one nice tool that can be accessed from command line

- build player in allolib that can cleanly handle these

- package the whole decoder into 1 alloapp - uses speaker layout json
