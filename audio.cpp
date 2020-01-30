#include "darmstadt.h"

bool AudioEngine::init(string devName) {
  return false;
}

bool AudioEngine::start() {
  return false;
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
