# RAW Audio

In this directory raw audio (pcm_f32le , 16000hz, 1 channel) files can be stored for output. 

# Convert including volume adjustment, with:
ffmpeg -i wawawawa.wav -f f32le -c:a pcm_f32le -ac 1 -ar 16000 -filter:a volume=0.3  wawawawa.raw

# Or a whole directory
for file in *.wav; do ffmpeg -i  $file -f f32le -c:a pcm_f32le -ac 1 -ar 16000 -filter:a volume=0.3 "$(basename $file .wav).raw"; done