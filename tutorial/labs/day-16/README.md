# Day 16 sound prerequisites

The day-16 service check looks for three isolated lab assets:

- `sounds/playback.wav`
- `sounds/phrase.wav`
- `sounds/ivr-main.wav`

These files are intentionally not copied from `conf/vanilla` and are not
included in the repository. Place the lab-approved sound package under
`tutorial/labs/day-16/sounds/`, or set `sound_root` in the teaching-service
configuration to another controlled lab directory. The check reports
`unavailable` with this install action when any file is missing; it never
claims that a learner heard audio merely because a file exists.
