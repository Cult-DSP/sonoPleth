# 3 - 27 - 26 Realtime Testing

- Pre test agent notes:
  Done. The (void)angleDelta suppresses any unused-variable warning since the comparison is now dead.

What to watch for in the test run:

Render-relocation and device-relocation messages should decrease or disappear if Fix 2 is the cause
Pops may also reduce, or may remain if they have a secondary source
Spatial image may be slightly less smooth for fast-moving sources (expected — sub-stepping is disabled)
Interpret the result:

Relocations gone → Fix 2 confirmed, proceed with hysteresis
Relocations remain → look elsewhere (DBAP focus changes, OutputRemap edge case, or something upstream in how poses are fed)
Relocations reduced but not eliminated → Fix 2 is a partial contributor, still need to look for another cause

- Ascent Test 1

* still resulted in reloc console messages almost immediately
* very quickly channels seemed to fully relocate

- Ascent Test 2

* still resulted in reloc console messages almost immediately
* channels didnt relocate as drastically but when they came back into focus - agressive pops and high pitched noise
