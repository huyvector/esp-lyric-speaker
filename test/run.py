#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 for name in ['core','request_policy','bass_tone','metadata','controls','mono_queue','display_sync','link','quote_art','diagnostic_tone','record_animation','visualizer','lcd_options','bass_store']:
  target=Path(tmp)/name
  subprocess.run(['c++','-std=c++17','-O2','-fsanitize=address,undefined','-I'+str(root/'shared'),'-I'+str(root/'audio/lib/ESP32-A2DP/src'),str(root/'test'/f'{name}_test.cpp'),'-o',str(target)],check=True)
  subprocess.run([str(target)],check=True)
 for name in ['image_update','bundle','volume_pipeline','catalog_match','lookup_workflow','buffered_stream','cache_stream','audio_link','display_link','encoder_controls','art_decode','audio_queue','audio_feedback_queue','audio_lifecycle','audio_buffer','command_queue','portal_page']:
  subprocess.run([sys.executable,str(root/'test'/f'{name}_test.py')],check=True)
print('All portable checks passed. Hardware/audio/RF/OTA end-to-end not covered by native doubles.')
