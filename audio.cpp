#include "darmstadt.h"

bool AudioEngine::init(string devName) {
  return true;
}

bool AudioEngine::start() {
  return true;
}

AudioPacket* AudioEngine::read() {
  return NULL;
}

int AudioEngine::sampleRate() {
  return 48000;
}

int AudioEngine::channels() {
  return 1;
}
